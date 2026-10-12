#include "openbl/anim/rope.hpp"

#include <doctest/doctest.h>

using namespace openbl;
using anim::Vec3;

TEST_CASE("Rope: a slack chain settles into its exact rest shape") {
    // Three 50-unit links over a 100-unit span: the middle link level, the
    // outer ones at 60 degrees, so the inner nodes hang 50 sin 60 below.
    const auto nodes = anim::rope_rest_nodes({0, 0, 0}, {100, 0, 0}, 50.0f, 2);
    REQUIRE(nodes.size() == 4);
    CHECK(nodes[0] == Vec3{0, 0, 0});
    CHECK(nodes[3] == Vec3{100, 0, 0});
    CHECK(nodes[1][0] == doctest::Approx(25.0f).epsilon(0.01));
    CHECK(nodes[2][0] == doctest::Approx(75.0f).epsilon(0.01));
    CHECK(nodes[1][2] == doctest::Approx(-43.30f).epsilon(0.01));
    CHECK(nodes[2][2] == doctest::Approx(-43.30f).epsilon(0.01));
    CHECK(nodes[1][1] == doctest::Approx(0.0f));
}

TEST_CASE("Rope: no slack is a straight line") {
    const auto nodes = anim::rope_rest_nodes({0, 0, 0}, {30, 0, 30}, 0.0f, 1);
    REQUIRE(nodes.size() == 3);
    CHECK(nodes[1][0] == doctest::Approx(15.0f));
    CHECK(nodes[1][2] == doctest::Approx(15.0f));
}

TEST_CASE("Rope: the curve passes through every node") {
    const std::vector<Vec3> nodes{{0, 0, 0}, {10, 0, -5}, {20, 0, -5}, {30, 0, 0}};
    const auto curve = anim::rope_curve(nodes, 4);
    REQUIRE(curve.size() == 13);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            CHECK(curve[i * 4][static_cast<std::size_t>(k)] == doctest::Approx(nodes[i][static_cast<std::size_t>(k)]));
        }
    }
}
