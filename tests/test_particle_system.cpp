#include "openbl/particles/particle_system.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <map>

using namespace openbl;
using particles::ParticleLibrary;
using particles::ParticleQuad;
using particles::ParticleSystem;

namespace {

ParticleLibrary library(std::map<std::string, std::string> files) {
    return ParticleLibrary([files = std::move(files)](const std::string& path) -> std::optional<std::string> {
        auto it = files.find(path);
        return it == files.end() ? std::nullopt : std::optional(it->second);
    });
}

std::vector<ParticleQuad> quads(const ParticleSystem& s) {
    std::vector<ParticleQuad> out;
    s.collect(out);
    return out;
}

} // namespace

TEST_CASE("Particle library names") {
    CHECK(ParticleLibrary::path_of("Smoke1") == "particles/smoke1.txt");
    CHECK(ParticleLibrary::path_of("particles/Fire1_emitter.txt") == "particles/fire1_emitter.txt");
    auto lib = library({});
    CHECK_FALSE(lib.find("missing"));
}

TEST_CASE("Particle emission by rate and burst") {
    auto lib = library({
        {"particles/emitter.txt", R"(Particle { loop "1" spawn { particle "puff" rate "30" } spawn { particle "puff" burst "5" } })"},
        {"particles/puff.txt", R"(Particle { frames "30" sprite "furball" })"},
    });
    ParticleSystem s(lib, "Emitter", {0, 0, 0});
    REQUIRE(s.valid());
    s.advance(1.0f / 30.0f);
    CHECK(quads(s).size() >= 5); // The burst, plus the first rate particle.
    s.advance(3.0f);
    // Bursts are gone (one-second lives); the rate keeps about 30 alive.
    const auto n = quads(s).size();
    CHECK(n >= 28);
    CHECK(n <= 32);
}

TEST_CASE("Particle motion and placement") {
    auto lib = library({
        {"particles/emitter.txt", R"(Particle { spawn { particle "riser" burst "1" radius "10" theta "90" phi "0" z "5" } })"},
        {"particles/riser.txt",
         R"(Particle { frames "300" sprite "dot" elevation_speed "10" size "0,10" color "255" red "255" green "0" blue "128" mask "51" })"},
    });
    ParticleSystem s(lib, "emitter", {100, 0, 0});
    s.advance(1.0f / 30.0f); // Spawned at the end of the first step.
    auto q = quads(s);
    REQUIRE(q.size() == 1);
    CHECK(q[0].position[0] == doctest::Approx(100.0f).epsilon(1e-4));
    CHECK(q[0].position[1] == doctest::Approx(10.0f));
    CHECK(q[0].position[2] == doctest::Approx(5.0f));
    s.advance(0.5f);
    q = quads(s);
    REQUIRE(q.size() == 1);
    CHECK(q[0].position[2] == doctest::Approx(10.0f).epsilon(0.02)); // 10 units per second for 0.5 s.
    // Half a second into a ten-second life: size 0 -> 10 spread over the life.
    CHECK(q[0].width == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(q[0].color[1] == 0.0f);
    CHECK(q[0].color[2] == doctest::Approx(128.0f / 255.0f));
    CHECK(q[0].mask == doctest::Approx(0.2f));
}

namespace {

// A floor at z = 0 everywhere, open sky above it.
particles::ParticleWorld floor_world() {
    particles::ParticleWorld w;
    w.trace = [](const particles::Vec3& a, const particles::Vec3& b) -> std::optional<particles::ParticleHit> {
        if (a[2] < 0 || b[2] >= 0) {
            return std::nullopt;
        }
        const float t = a[2] / (a[2] - b[2]);
        return particles::ParticleHit{{a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, 0}, {0, 0, 1}};
    };
    w.open_sky = [](const particles::Vec3& p) { return p[2] >= 0; };
    return w;
}

} // namespace

TEST_CASE("Particle collisions: splash and flat decal where a drop lands") {
    // Curly quotes around a name, as some shipped definitions have.
    auto lib = library({
        {"particles/drip.txt", "Particle { sprite \"drop\" frames \"300\" elevation_speed \"-300\" collide { spawn "
                               "{ particle \x93splash\x94 } decal { particle \"ring\" } } }"},
        {"particles/splash.txt", R"(Particle { sprite "dot" frames "30" })"},
        {"particles/ring.txt", R"(Particle { sprite "ringlet" frames "30" })"},
    });
    const auto world = floor_world();
    ParticleSystem s(lib, "drip", {5, 0, 100});
    s.set_placement({}, &world);
    s.advance(0.2f); // 60 units down.
    REQUIRE(quads(s).size() == 1);
    s.advance(0.2f); // Lands at 1/3 s.
    const auto q = quads(s);
    REQUIRE(q.size() == 2);
    const auto& ring = q[0].normal[2] != 0 ? q[0] : q[1];
    const auto& splash = q[0].normal[2] != 0 ? q[1] : q[0];
    CHECK(ring.normal[2] == 1.0f);
    CHECK(ring.position[2] == doctest::Approx(0.5f)); // Lifted off the surface.
    CHECK(splash.position[0] == doctest::Approx(5.0f));
    CHECK(splash.position[2] == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("Particle precipitation, volumes and ramp scale") {
    auto lib = library({
        {"particles/rain.txt", R"(Particle { loop "1" precipitation "1" spawn { particle "drop" rate "300" } })"},
        {"particles/drop.txt", R"(Particle { sprite "drop" frames "300" z_speed "-200" precipitation "1" })"},
    });
    const auto world = floor_world();
    SUBCASE("rain falls in a box around the viewer and ends at the floor") {
        ParticleSystem s(lib, "rain", {0, 0, 0});
        particles::SystemPlacement placement;
        placement.precipitation_bounds = 100;
        s.set_placement(placement, &world);
        s.set_viewer({1000, 2000, 50});
        s.advance(2.0f);
        const auto q = quads(s);
        CHECK(q.size() > 100); // Lives end at the floor (at most 0.75 s), not after 10 s.
        CHECK(q.size() < 300);
        for (const auto& d : q) {
            CHECK(std::abs(d.position[0] - 1000) <= 100);
            CHECK(std::abs(d.position[1] - 2000) <= 100);
            CHECK(d.position[2] >= 0);
            CHECK(d.position[2] <= 150);
        }
    }
    SUBCASE("ramp scale 0 emits nothing") {
        ParticleSystem s(lib, "rain", {0, 0, 0});
        particles::SystemPlacement placement;
        placement.precipitation_bounds = 100;
        placement.rate_scale = 0;
        s.set_placement(placement, &world);
        s.advance(2.0f);
        CHECK(quads(s).empty());
    }
    SUBCASE("a volume spreads the root's children through its box") {
        ParticleSystem s(lib, "rain", {0, 0, 0});
        particles::SystemPlacement placement;
        placement.volume = std::array<particles::Vec3, 2>{{{-50, 10, 200}, {50, 20, 300}}};
        s.set_placement(placement, nullptr);
        s.advance(0.1f);
        const auto q = quads(s);
        REQUIRE(q.size() > 10);
        float lo = 1e9f, hi = -1e9f;
        for (const auto& d : q) {
            CHECK(d.position[1] >= 10);
            CHECK(d.position[1] <= 20);
            lo = std::min(lo, d.position[0]);
            hi = std::max(hi, d.position[0]);
        }
        CHECK(hi - lo > 50);
    }
}

TEST_CASE("Particle integration: every effect in the real install runs") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    ParticleLibrary lib([&](const std::string& path) -> std::optional<std::string> {
        auto data = gfs.read(path);
        if (!data) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(data->data()), data->size());
    });
    std::size_t effects = 0, visible = 0, non_finite = 0, capped = 0;
    std::uint32_t seed = 1;
    for (const auto& path : gfs.list_files()) {
        if (!path.starts_with("particles/") || !path.ends_with(".txt")) {
            continue;
        }
        ParticleSystem s(lib, path, {0, 0, 0}, seed++);
        if (!s.valid()) {
            continue;
        }
        ++effects;
        s.advance(2.0f);
        capped += s.particle_count() >= particles::kMaxParticlesPerSystem;
        std::vector<ParticleQuad> q;
        s.collect(q);
        visible += !q.empty();
        for (const auto& p : q) {
            for (float v : {p.position[0], p.position[1], p.position[2], p.width, p.height, p.color[0], p.mask}) {
                non_finite += !std::isfinite(v);
            }
        }
    }
    MESSAGE(effects << " effects, " << visible << " with visible particles after 2 s, " << capped
                    << " at the particle cap");
    CHECK(effects > 1500);
    CHECK(non_finite == 0);
}
