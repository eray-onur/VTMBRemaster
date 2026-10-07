#include "openbl/formats/particle.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cctype>
#include <map>
#include <string>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("Particle curves") {
    const auto c = parse_particle_curve("0,200(10),0(60)");
    REQUIRE(c);
    REQUIRE(c->keys.size() == 3);
    CHECK(c->keys[1].time == 10.0f);
    CHECK(c->at(0, 60) == 0.0f);
    CHECK(c->at(5, 60) == doctest::Approx(100.0f));
    CHECK(c->at(10, 60) == doctest::Approx(200.0f));
    CHECK(c->at(35, 60) == doctest::Approx(100.0f));
    CHECK(c->at(100, 60) == 0.0f);

    const auto even = parse_particle_curve("-4,-4,4");
    REQUIRE(even);
    CHECK(even->at(15, 20) == doctest::Approx(0.0f)); // Keys at frames 0, 10, 20.

    const auto range = parse_particle_curve("0~360");
    REQUIRE(range);
    CHECK(range->at(0, 10, 0.0f) == 0.0f);
    CHECK(range->at(0, 10, 1.0f) == 360.0f);

    const auto hold = parse_particle_curve("255!,0!,255!,0!");
    REQUIRE(hold);
    CHECK(hold->at(1, 30) == 255.0f); // Steps, no blending.

    CHECK(parse_particle_curve("30,0(15)!,")); // Trailing comma.
    CHECK(parse_particle_curve(".5~1.5"));
    CHECK_FALSE(parse_particle_curve("Dominatedots"));
    CHECK_FALSE(parse_particle_curve("1~"));
    CHECK_FALSE(parse_particle_curve("5(3"));
}

TEST_CASE("Particle blocks") {
    const auto f = parse_particle(R"(Particle {
        "sprite" "Dominatedots" "frames" "30" "Radius_speed" "60,0(15),0"
        spawn { "particle" "D_dominate_cast_fx" "rate" "40" "theta" "0~360" }
        collide { self { "bounce" ".2~.8" } decal { "vdecal_first" "decals/stains/blood1" } }
    })");
    CHECK(f.diagnostics.empty());
    CHECK(f.root.name == "Particle");
    CHECK(f.root.text("SPRITE") == "Dominatedots");
    REQUIRE(f.root.curve("radius_speed"));
    CHECK(f.root.curve("radius_speed")->keys.size() == 3);
    REQUIRE(f.root.child("spawn"));
    CHECK(f.root.child("spawn")->text("particle") == "D_dominate_cast_fx");
    REQUIRE(f.root.child("collide"));
    CHECK(f.root.child("collide")->child("self")->curve("bounce")->keys[0].max == 0.8f);
}

TEST_CASE("Particle integration: every effect parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, malformed = 0, spawns = 0, missing_children = 0, missing_sprites = 0, sprites = 0;
    std::map<std::string, std::size_t> roots;
    std::string first_bad, empty_roots;
    auto exists = [&](const std::string& name) {
        return gfs.exists("particles/" + name + ".txt");
    };
    for (const auto& path : gfs.list_files()) {
        if (!path.starts_with("particles/") || !path.ends_with(".txt")) {
            continue;
        }
        const auto data = gfs.read(path);
        const auto f = parse_particle({reinterpret_cast<const char*>(data->data()), data->size()});
        ++files;
        ++roots[f.root.name];
        if (f.root.name.empty()) {
            empty_roots += path + " ";
        }
        for (const auto& d : f.diagnostics) {
            if (d.message.starts_with("malformed value")) {
                ++malformed;
                if (first_bad.empty()) {
                    first_bad = path + ": " + d.message;
                }
            }
        }
        if (const auto s = f.root.text("sprite"); !s.empty()) {
            ++sprites;
            // Sprites are images next to the definitions: particles/<sprite>.tga.
            std::string lower(s);
            for (char& ch : lower) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            missing_sprites += !gfs.exists("particles/" + lower + ".tga");
        }
        // Child emitters name other particle files.
        auto visit = [&](auto&& self, const ParticleBlock& b) -> void {
            if (const auto p = b.text("particle"); !p.empty()) {
                ++spawns;
                missing_children += !exists(std::string(p));
            }
            for (const auto& c : b.children) {
                self(self, c);
            }
        };
        visit(visit, f.root);
    }
    std::string root_names;
    for (const auto& [n, c] : roots) {
        root_names += n + " x" + std::to_string(c) + ", ";
    }
    MESSAGE(files << " particle files; roots: " << root_names << malformed << " malformed values " << first_bad
                  << "; " << spawns << " child emitters (" << missing_children << " name a missing file); " << sprites
                  << " sprites (" << missing_sprites << " not found); empty roots: " << empty_roots);
    CHECK(files == 1594);
    // Two shipped typos: smoke2 "Rotation" "0,~180~180,360" and
    // d_potence_1bp_bodyglow2-normal "refract" "0,~1,0" (each also in a copy).
    CHECK(malformed <= 4);
}
