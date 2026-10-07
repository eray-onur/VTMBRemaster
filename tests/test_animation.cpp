#include "openbl/anim/animation.hpp"
#include "openbl/anim/flex.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/keyvalues.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <memory>

using namespace openbl;
using namespace openbl::formats;

namespace {

// A 9 x 1 grid across move_yaw -180..180 (like Andrei's run), animations 0..8.
MdlFile run_grid() {
    MdlFile file;
    file.pose_parameters.push_back({"move_yaw", 0, -180, 180, 360});
    file.animations.resize(9);
    for (auto& a : file.animations) {
        a.frame_count = 17;
        a.fps = 30;
    }
    MdlSequence seq;
    seq.grid = {9, 1};
    for (int i = 0; i < 9; ++i) {
        seq.blend_animations.push_back(i);
    }
    seq.blend_param = {0, -1};
    seq.blend_start = {-180, 0};
    seq.blend_end = {180, 0};
    file.sequences.push_back(seq);
    return file;
}

float weight_of(const anim::BlendSample& s, std::int32_t animation) {
    float w = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        w += s.animations[k] == animation ? s.weights[k] : 0.0f;
    }
    return w;
}

float pose_distance(const std::vector<MdlBonePose>& a, const std::vector<MdlBonePose>& b) {
    float d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            d = std::max(d, std::abs(a[i].position[k] - b[i].position[k]));
        }
        const float dot = std::abs(a[i].rotation[0] * b[i].rotation[0] + a[i].rotation[1] * b[i].rotation[1] +
                                   a[i].rotation[2] * b[i].rotation[2] + a[i].rotation[3] * b[i].rotation[3]);
        d = std::max(d, 1.0f - std::min(dot, 1.0f));
    }
    return d;
}

} // namespace

TEST_CASE("Animation blend grid weights") {
    const MdlFile file = run_grid();
    const MdlSequence& seq = file.sequences[0];
    auto at = [&](float yaw) {
        const float params[] = {yaw};
        return anim::blend_sample(file, seq, params);
    };
    CHECK(weight_of(at(0), 4) == doctest::Approx(1.0f));
    CHECK(weight_of(at(22.5f), 4) == doctest::Approx(0.5f));
    CHECK(weight_of(at(22.5f), 5) == doctest::Approx(0.5f));
    CHECK(weight_of(at(-180), 0) == doctest::Approx(1.0f));
    CHECK(weight_of(at(180), 8) == doctest::Approx(1.0f));
    // Looping parameter: 200 wraps to -160, 4/9 of the way from cell 0 to cell 1.
    CHECK(weight_of(at(200), 1) == doctest::Approx(4.0f / 9.0f));
    CHECK(weight_of(at(200), 0) == doctest::Approx(5.0f / 9.0f));
    // No value given: 0, the centre.
    CHECK(weight_of(anim::blend_sample(file, seq, {}), 4) == doctest::Approx(1.0f));

    // A broken cell drops out and the rest renormalises.
    MdlFile broken = file;
    broken.sequences[0].blend_animations[5] = -1;
    const float params[] = {22.5f};
    const auto s = anim::blend_sample(broken, broken.sequences[0], params);
    CHECK(weight_of(s, 4) == doctest::Approx(1.0f));

    CHECK(anim::sequence_duration(file, seq) == doctest::Approx(16.0f / 30.0f));
}

TEST_CASE("Animation pose blending takes the shorter arc") {
    MdlBonePose a, b;
    a.rotation = {0, 0, 0, 1};
    b.rotation = {0, 0, 0, -1}; // The same rotation, opposite sign.
    const auto m = anim::blend(a, b, 0.5f);
    CHECK(std::abs(m.rotation[3]) == doctest::Approx(1.0f));
    b.position = {10, 0, 0};
    CHECK(anim::blend(a, b, 0.25f).position[0] == doctest::Approx(2.5f));
}

TEST_CASE("Animation integration: Andrei's run blends across move_yaw and fades from idle") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    const auto data = gfs.read("models/character/monster/andrei/andrei.mdl");
    REQUIRE(data);
    const auto file = std::make_shared<MdlFile>(parse_mdl(*data));
    auto find = [&](std::string_view label) {
        for (std::size_t s = 0; s < file->sequences.size(); ++s) {
            if (iequals(file->sequences[s].label, label)) {
                return s;
            }
        }
        FAIL("sequence not found: " << label);
        return std::size_t{0};
    };
    const std::size_t run = find("run"), idle = find("idle");
    REQUIRE(file->sequences[run].grid[0] == 9);
    std::size_t move_yaw = 0;
    while (file->pose_parameters[move_yaw].name != "move_yaw") {
        ++move_yaw;
    }
    std::vector<float> params(file->pose_parameters.size(), 0.0f);
    auto sample = [&](float yaw, float cycle) {
        params[move_yaw] = yaw;
        return anim::sample_sequence(*file, run, cycle, params);
    };
    // Both ends of the grid are run_180.
    CHECK(pose_distance(sample(-180, 0.3f), sample(180, 0.3f)) < 1e-4f);
    // Between two cells the pose lies between them.
    const float half = pose_distance(sample(0, 0.3f), sample(22.5f, 0.3f));
    const float full = pose_distance(sample(0, 0.3f), sample(45, 0.3f));
    CHECK(half > 0);
    CHECK(half < full);

    // The player fades from idle to run over run's fade-in time.
    anim::AnimationPlayer player(file->bones);
    player.play(file, idle, 0.0f);
    player.play(file, run, 1.0f);
    std::vector<float> zero(file->pose_parameters.size(), 0.0f);
    const float fade = file->sequences[run].fade_in;
    REQUIRE(fade > 0);
    const auto idle_pose = anim::sample_sequence(*file, idle, std::fmod(1.0f / anim::sequence_duration(*file, file->sequences[idle]), 1.0f), zero);
    CHECK(pose_distance(player.pose(1.0f), idle_pose) < 1e-4f);
    const float later = 1.0f + fade;
    const auto run_pose = anim::sample_sequence(*file, run, std::fmod(fade / anim::sequence_duration(*file, file->sequences[run]), 1.0f), zero);
    CHECK(pose_distance(player.pose(later), run_pose) < 1e-4f);
    CHECK(player.cycle(later) == doctest::Approx(std::fmod(fade / anim::sequence_duration(*file, file->sequences[run]), 1.0f)));
}

TEST_CASE("Flex rules and mesh flex ramps") {
    // upper_right_raiser = right_lid_raiser * (1 - 0.8 * c1) * (1 - c2), then a flex reading it.
    MdlFile file;
    file.flex_names = {"upper_right_raiser", "half"};
    file.flex_controllers = {{"eyelid", "right_lid_raiser", 0, 1}, {"eyelid", "c1", 0, 1}, {"eyelid", "c2", -1, 1}};
    using Op = MdlFlexOp;
    MdlFlexRule rule{0,
                     {{Op::Controller, 0, 0}, {Op::Const, 0, 1}, {Op::Const, 0, 0.8f}, {Op::Controller, 1, 0}, {Op::Mul, 0, 0},
                      {Op::Sub, 0, 0}, {Op::Mul, 0, 0}, {Op::Const, 0, 1}, {Op::Controller, 2, 0}, {Op::Sub, 0, 0}, {Op::Mul, 0, 0}}};
    MdlFlexRule half{1, {{Op::Flex, 0, 0}, {Op::Const, 0, 2}, {Op::Div, 0, 0}}};
    file.flex_rules = {rule, half};

    auto controllers = anim::default_flex_controllers(file);
    CHECK(anim::set_flex_controller(file, controllers, "RIGHT_LID_RAISER", 1.0f));
    CHECK(anim::set_flex_controller(file, controllers, "c1", 0.5f));
    CHECK(anim::set_flex_controller(file, controllers, "c2", 5.0f)); // Clamped to 1.
    CHECK_FALSE(anim::set_flex_controller(file, controllers, "missing", 1.0f));
    auto w = anim::evaluate_flex_rules(file, controllers);
    CHECK(w[0] == doctest::Approx(0.0f));
    anim::set_flex_controller(file, controllers, "c2", 0.25f);
    w = anim::evaluate_flex_rules(file, controllers);
    CHECK(w[0] == doctest::Approx(1.0f * (1 - 0.4f) * 0.75f));
    CHECK(w[1] == doctest::Approx(w[0] / 2));

    const std::array<float, 4> ramp{0, 1, 10, 11};
    CHECK(anim::mesh_flex_scale(ramp, 0.0f) == 0.0f);
    CHECK(anim::mesh_flex_scale(ramp, 0.5f) == doctest::Approx(0.5f));
    CHECK(anim::mesh_flex_scale(ramp, 5.0f) == 1.0f);
    CHECK(anim::mesh_flex_scale(ramp, 10.5f) == doctest::Approx(0.5f));
    CHECK(anim::mesh_flex_scale({-11, -10, -0.27f, 0.18f}, -5.0f) == 1.0f); // An eyelid's lower ramp.
}

TEST_CASE("Flex integration: every model's rules evaluate to finite weights") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t models = 0, weights = 0, non_finite = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".mdl") || !path.starts_with("models/character")) {
            continue;
        }
        const auto data = gfs.read(path);
        MdlFile mdl;
        try {
            mdl = parse_mdl(*data);
        } catch (const FormatError&) {
            continue;
        }
        if (mdl.flex_rules.empty()) {
            continue;
        }
        ++models;
        auto controllers = anim::default_flex_controllers(mdl);
        for (std::size_t i = 0; i < controllers.size(); ++i) {
            controllers[i] = mdl.flex_controllers[i].max; // Everything at full.
        }
        for (float v : anim::evaluate_flex_rules(mdl, controllers)) {
            ++weights;
            non_finite += !std::isfinite(v);
        }
    }
    MESSAGE(models << " character models with flex rules, " << weights << " flex weights");
    CHECK(models > 100);
    CHECK(non_finite == 0);
}
