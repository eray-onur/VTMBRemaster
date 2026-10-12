#include "openbl/formats/error.hpp"
#include "openbl/formats/vmt_proxies.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <map>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("Proxy Sine writes every component of a vector") {
    // The shape of the shipped marquee bulbs: a scalar into $selfillumtint.
    MaterialProxies p(parse_vmt(R"("VertexLitGeneric" {
        "$selfillumtint" "[1 1 1]"
        "Proxies" { "Sine" { "resultVar" "$selfillumtint" "sineperiod" .8 "sinemin" 0 "sinemax" 1 "timeoffset" .2 } }
    })"));
    REQUIRE_FALSE(p.empty());
    p.evaluate({.time = 0.2f}); // sin(0) -> midpoint.
    const ProxyVar* v = p.var("$selfillumtint");
    REQUIRE(v);
    CHECK(v->count == 3);
    CHECK(v->v[0] == doctest::Approx(0.5f));
    CHECK(v->v[2] == doctest::Approx(0.5f));
    p.evaluate({.time = 0.4f}); // A quarter period later: the maximum.
    CHECK(p.var("$selfillumtint")->v[1] == doctest::Approx(1.0f));
}

TEST_CASE("Proxy chains feed later proxies") {
    MaterialProxies p(parse_vmt(R"("UnlitTwoTexture" {
        "$a" 0  "$limit" 0.5  "$one" 1  "$zero" 0  "$flicker" 0  "$color" "[1 1 1]"
        "Proxies" {
            "LinearRamp" { "rate" 0.25 "initialValue" 0 "resultVar" "$a" }
            "LessOrEqual" { "srcVar1" "$a" "srcVar2" "$limit" "lessEqualVar" "$one" "greaterVar" "$zero" "resultVar" "$flicker" }
            "Subtract" { "srcVar1" "$flicker" "srcVar2" "$limit" "resultVar" "$color[1]" }
            "Abs" { "srcVar1" "$color[1]" "resultVar" "$color[2]" }
            "Exponential" { "srcVar1" "$zero" "resultVar" "$e" }
        }
    })"));
    p.evaluate({.time = 1.0f}); // $a = 0.25 <= 0.5
    CHECK(p.var("$flicker")->v[0] == 1.0f);
    CHECK(p.var("$color")->v[0] == 1.0f);
    CHECK(p.var("$color")->v[1] == doctest::Approx(0.5f));
    p.evaluate({.time = 4.0f}); // $a = 1 > 0.5
    CHECK(p.var("$flicker")->v[0] == 0.0f);
    CHECK(p.var("$color")->v[1] == doctest::Approx(-0.5f));
    CHECK(p.var("$color")->v[2] == doctest::Approx(0.5f));
    CHECK(p.var("$e")->v[0] == doctest::Approx(1.0f));
}

TEST_CASE("Proxy noise stays in range") {
    MaterialProxies p(parse_vmt(R"("VertexLitGeneric" {
        "Proxies" {
            "UniformNoise" { "minVal" 0.2 "maxVal" 0.4 "resultVar" "$alpha" }
            "GaussianNoise" { "mean" 1 "halfwidth" 5 "minVal" 0 "maxVal" 2 "resultVar" "$g" }
        }
    })"));
    for (int i = 0; i < 200; ++i) {
        p.evaluate({.time = i * 0.1f});
        const float a = p.var("$alpha")->v[0], g = p.var("$g")->v[0];
        REQUIRE(a >= 0.2f);
        REQUIRE(a <= 0.4f);
        REQUIRE(g >= 0.0f);
        REQUIRE(g <= 2.0f);
    }
}

TEST_CASE("Proxy texture scroll and transform") {
    MaterialProxies p(parse_vmt(R"("LightmappedGeneric" {
        "$angle" 90  "$offset" "[0.25 0]"
        "Proxies" {
            "TextureScroll" { "texturescrollvar" "$baseTextureTransform" "texturescrollrate" 0.3 "texturescrollangle" 90 }
            "TextureTransform" { "rotateVar" "$angle" "translateVar" "$offset" "resultVar" "$bumptransform" }
        }
    })"));
    p.evaluate({.time = 4.0f}); // 1.2 widths along +v, wrapped to 0.2.
    const ProxyVar* scroll = p.var("$basetexturetransform");
    REQUIRE(scroll);
    CHECK(scroll->is_matrix);
    CHECK(scroll->m[2] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(scroll->m[5] == doctest::Approx(0.2f).epsilon(1e-4));
    // Rotating 90 degrees about (0.5, 0.5), then translating: (1, 0.5) -> (0.75, 1).
    const auto& m = p.var("$bumptransform")->m;
    CHECK(m[0] * 1 + m[1] * 0.5f + m[2] == doctest::Approx(0.75f));
    CHECK(m[3] * 1 + m[4] * 0.5f + m[5] == doctest::Approx(1.0f));
    CHECK(parse_texture_transform("center .5 .5 scale 2 2 rotate 0 translate 0 0") ==
          build_texture_transform({0.5f, 0.5f}, {2, 2}, 0, {0, 0}));
    CHECK_FALSE(parse_texture_transform("models/foo"));
}

TEST_CASE("Proxy animated texture frames") {
    MaterialProxies p(parse_vmt(R"("Water" {
        "$normalmap" "liquids/water_normal"  "$bumpframe" 0
        "Proxies" {
            "AnimatedTexture" { "animatedtexturevar" "$normalmap" "animatedtextureframenumvar" "$bumpframe" "animatedtextureframerate" 30 }
            "WaterLOD" { "dummy" 0 }
        }
    })"));
    REQUIRE(p.animated_textures().size() == 1);
    CHECK(p.animated_textures()[0] == std::pair<std::string, std::string>{"$normalmap", "$bumpframe"});
    CHECK(p.unsupported() == std::vector<std::string>{"waterlod"});
    p.set_frame_count("$normalmap", 29);
    p.evaluate({.time = 0.5f});
    CHECK(p.var("$bumpframe")->v[0] == 15.0f);
    p.evaluate({.time = 1.0f}); // Frame 30 wraps to 1.
    CHECK(p.var("$bumpframe")->v[0] == 1.0f);
}

TEST_CASE("Proxy integration: every proxy material in the real install runs") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t materials = 0, ops_materials = 0, non_finite = 0;
    std::map<std::string, std::size_t> unsupported;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".vmt")) {
            continue;
        }
        const auto data = gfs.read(path);
        Material mat;
        try {
            mat = parse_vmt(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
        } catch (const FormatError&) {
            continue;
        }
        if (mat.proxies.empty()) {
            continue;
        }
        ++materials;
        MaterialProxies p(mat);
        for (const auto& u : p.unsupported()) {
            ++unsupported[u];
        }
        if (p.empty()) {
            continue;
        }
        ++ops_materials;
        for (const auto& [texture, frame_var] : p.animated_textures()) {
            p.set_frame_count(texture, 8);
        }
        for (float t : {0.0f, 1.234f, 1000.0f}) {
            p.evaluate({.time = t, .player_distance = 300.0f});
            for (const auto& [name, v] : p.vars()) {
                for (float c : v.v) {
                    non_finite += !std::isfinite(c);
                }
                for (float c : v.m) {
                    non_finite += !std::isfinite(c);
                }
            }
        }
    }
    std::string list;
    for (const auto& [k, n] : unsupported) {
        list += k + " x" + std::to_string(n) + " ";
    }
    MESSAGE(materials << " materials with proxies, " << ops_materials << " with proxies that run; not run: " << list);
    CHECK(materials > 190); // Loose and archived; map pakfiles hold more.
    CHECK(ops_materials > 180);
    CHECK(non_finite == 0);
}

TEST_CASE("Proxy LampBeam and LampHalo fade by how vertically the lamp is seen") {
    MaterialProxies beam(parse_vmt(R"("Sprite" { "$basetexture" "x" "Proxies" { "lampbeam" { } } })"));
    MaterialProxies halo(parse_vmt(R"("Sprite" { "$basetexture" "x" "Proxies" { "lamphalo" { } } })"));
    beam.evaluate({.view_direction_z = 0.0f}); // Seen side-on.
    halo.evaluate({.view_direction_z = 0.0f});
    CHECK(beam.var("$alpha")->v[0] == doctest::Approx(1.0f));
    CHECK(halo.var("$alpha")->v[0] == doctest::Approx(0.0f));
    beam.evaluate({.view_direction_z = 0.6f}); // Looking up at it.
    halo.evaluate({.view_direction_z = 0.6f});
    CHECK(beam.var("$alpha")->v[0] == doctest::Approx(0.4f));
    CHECK(halo.var("$alpha")->v[0] == doctest::Approx((0.6f - 0.25f) * 1.35f));
    halo.evaluate({.view_direction_z = -1.0f}); // Straight down at it: clamped.
    CHECK(halo.var("$alpha")->v[0] == doctest::Approx(1.0f));
}

TEST_CASE("Proxy PlayerProximity fades the shipped light cones in with distance") {
    // volumelighta_proxyfade: $alpha = distance * 0.001 - 0.2.
    MaterialProxies p(parse_vmt(R"("Sprite" {
        "$temp" 0 "$curve" 0.2 "$alpha" 0.5
        "Proxies" {
            "PlayerProximity" { "resultVar" "$temp" "scale" 0.001 }
            "Subtract" { "srcVar1" "$temp" "srcVar2" "$curve" "resultVar" "$alpha" }
        }
    })"));
    p.evaluate({.player_distance = 100.0f});
    CHECK(p.var("$alpha")->v[0] == doctest::Approx(-0.1f));
    p.evaluate({.player_distance = 700.0f});
    CHECK(p.var("$alpha")->v[0] == doctest::Approx(0.5f));
}
