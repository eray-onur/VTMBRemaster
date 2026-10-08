#include "openbl/anim/cloth.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace openbl;
using anim::ClothCapsule;
using anim::ClothSettings;
using anim::ClothSimulation;
using anim::Vec3;

namespace {

// A pendulum: particle 0 attached, particle 1 free, 4 units apart, as the
// model files store it (attached end weight 0, the other -2).
formats::MdlCloth pendulum() {
    formats::MdlCloth cloth;
    cloth.particles = {0, 1};
    cloth.attached_particles = 1;
    cloth.constraints.push_back({0, 1, 0.0f, -2.0f, 16.0f});
    cloth.primary_constraints = 1;
    return cloth;
}

float distance(const Vec3& a, const Vec3& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

} // namespace

TEST_CASE("Cloth: a free particle hangs below its attached neighbour at the rest length") {
    ClothSettings settings;
    settings.shape_stiffness = 0.0f;
    settings.iterations = 8;
    // Starts sideways; gravity swings it down and damping settles it.
    const std::vector<Vec3> skinned{{0, 0, 0}, {4, 0, 0}};
    ClothSimulation sim(pendulum(), skinned, settings);
    for (int i = 0; i < 600; ++i) {
        sim.update(1.0f / 60.0f, skinned, {});
    }
    const auto& p = sim.positions();
    CHECK(p[0] == Vec3{0, 0, 0});
    CHECK(distance(p[0], p[1]) == doctest::Approx(4.0f).epsilon(0.03));
    CHECK(p[1][2] < -3.9f);
    CHECK(std::abs(p[1][0]) < 0.2f);
}

TEST_CASE("Cloth: attached particles follow the skeleton") {
    ClothSettings settings;
    settings.shape_stiffness = 0.0f;
    std::vector<Vec3> skinned{{0, 0, 0}, {0, 0, -4}};
    ClothSimulation sim(pendulum(), skinned, settings);
    skinned[0] = {10, 0, 0};
    sim.update(1.0f / 60.0f, skinned, {});
    CHECK(sim.positions()[0] == Vec3{10, 0, 0});
    // The free end is dragged along.
    CHECK(sim.positions()[1][0] > 0.5f);
}

TEST_CASE("Cloth: capsules push free particles out") {
    ClothSettings settings;
    settings.shape_stiffness = 0.0f;
    settings.gravity = 0.0f;
    const std::vector<Vec3> skinned{{0, 0, 0}, {0, 0, -4}};
    ClothSimulation sim(pendulum(), skinned, settings);
    // A capsule along x through the free particle, radius 1.
    const ClothCapsule capsule{{-5, 0, -4.2f}, {5, 0, -4.2f}, 1.0f};
    sim.update(1.0f / 60.0f, skinned, std::span(&capsule, 1));
    const Vec3 p = sim.positions()[1];
    CHECK(std::hypot(p[1], p[2] + 4.2f) >= doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("Cloth: steps are fixed, and long gaps are not caught up") {
    ClothSettings settings;
    settings.shape_stiffness = 0.0f;
    const std::vector<Vec3> skinned{{0, 0, 0}, {4, 0, 0}};
    ClothSimulation a(pendulum(), skinned, settings);
    ClothSimulation b(pendulum(), skinned, settings);
    // Two half steps equal one whole step.
    a.update(0.5f / 60.0f, skinned, {});
    a.update(0.5f / 60.0f, skinned, {});
    b.update(1.0f / 60.0f, skinned, {});
    CHECK(a.positions()[1] == b.positions()[1]);
    // A 10 second gap runs at most max_steps steps.
    ClothSimulation c(pendulum(), skinned, settings);
    ClothSimulation d(pendulum(), skinned, settings);
    c.update(10.0f, skinned, {});
    for (std::uint32_t i = 0; i < settings.max_steps; ++i) {
        d.update(1.0f / 60.0f, skinned, {});
    }
    CHECK(c.positions()[1] == d.positions()[1]);
}

TEST_CASE("Cloth: the second constraint group only keeps particles apart") {
    // Free particles 1 and 2 hang from attached 0; the second-group pair (1, 2)
    // may not come closer than 2 units (rest 4 squared) but may move apart.
    formats::MdlCloth cloth;
    cloth.particles = {0, 1, 2};
    cloth.attached_particles = 1;
    cloth.constraints = {{0, 1, 0.0f, -2.0f, 4.0f}, {0, 2, 0.0f, -2.0f, 4.0f}, {1, 2, 1.0f, -1.0f, 4.0f}};
    cloth.primary_constraints = 2;
    ClothSettings settings;
    settings.shape_stiffness = 0.0f;
    settings.iterations = 8;
    // Starts spread 4 apart: gravity brings them together until 2 apart.
    const std::vector<Vec3> skinned{{0, 0, 0}, {-2, 0, 0}, {2, 0, 0}};
    ClothSimulation sim(cloth, skinned, settings);
    for (int i = 0; i < 600; ++i) {
        sim.update(1.0f / 60.0f, skinned, {});
    }
    CHECK(distance(sim.positions()[1], sim.positions()[2]) == doctest::Approx(2.0f).epsilon(0.05));
    // Spread apart it stays apart (no pull together).
    ClothSettings still = settings;
    still.gravity = 0.0f;
    ClothSimulation spread(cloth, skinned, still);
    spread.update(1.0f / 60.0f, skinned, {});
    CHECK(distance(spread.positions()[1], spread.positions()[2]) == doctest::Approx(4.0f));
}

TEST_CASE("Cloth: a piece flung far from its skinned shape restarts there") {
    ClothSettings settings;
    settings.max_stray = 1.0f;
    std::vector<Vec3> skinned{{0, 0, 0}, {0, 0, -4}};
    ClothSimulation sim(pendulum(), skinned, settings);
    // The skeleton teleports 100 units: the free end is dragged along its
    // constraint but lands about 5 units from its skinned place.
    skinned = {{100, 0, 0}, {100, 0, -4}};
    sim.update(1.0f / 60.0f, skinned, {});
    CHECK(sim.positions()[1] == Vec3{100, 0, -4});
}

TEST_CASE("Cloth: triangle normals per particle") {
    formats::MdlCloth cloth;
    cloth.particles = {0, 1, 2};
    cloth.triangles.push_back({0, 1, 2});
    const std::vector<Vec3> skinned{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    ClothSimulation sim(cloth, skinned);
    const auto n = sim.normals();
    CHECK(n[2][2] == doctest::Approx(1.0f));
}
