#include "openbl/formats/error.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/phy.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>

using namespace openbl;
using namespace openbl::formats;

namespace {

void put(std::vector<std::byte>& d, std::size_t at, const void* src, std::size_t n) {
    if (d.size() < at + n) {
        d.resize(at + n);
    }
    std::memcpy(d.data() + at, src, n);
}
void i32(std::vector<std::byte>& d, std::size_t at, std::int32_t v) {
    put(d, at, &v, 4);
}
void f32(std::vector<std::byte>& d, std::size_t at, float v) {
    put(d, at, &v, 4);
}

// One solid holding one tetrahedron, plus a text block.
std::vector<std::byte> build_phy() {
    std::vector<std::byte> d(16);
    i32(d, 0, 16);
    i32(d, 8, 1);
    i32(d, 12, 0x1234);
    const std::size_t solid = 20;
    const std::size_t piece = solid + 48;
    const std::size_t points = piece + 16 + 4 * 16;
    const std::size_t tree = points + 4 * 16;
    const std::size_t end = tree + 28;
    i32(d, 16, static_cast<std::int32_t>(end - solid));
    f32(d, solid + 4, 0.0254f); // mass center y (physics) -> model -z
    f32(d, solid + 24, 0.0254f);
    i32(d, solid + 32, static_cast<std::int32_t>(tree - solid));
    put(d, solid + 44, "IVPS", 4);
    i32(d, piece, static_cast<std::int32_t>(points - piece));
    const std::uint16_t count = 4;
    put(d, piece + 12, &count, 2);
    const std::uint16_t tris[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    for (std::size_t t = 0; t < 4; ++t) {
        for (std::size_t e = 0; e < 3; ++e) {
            put(d, piece + 16 + 16 * t + 4 + 4 * e, &tris[t][e], 2);
        }
    }
    const float pts[4][3] = {{0, 0, 0}, {0.0254f, 0, 0}, {0, 0.0254f, 0}, {0, 0, 0.0254f}};
    for (std::size_t p = 0; p < 4; ++p) {
        for (std::size_t k = 0; k < 3; ++k) {
            f32(d, points + 16 * p + 4 * k, pts[p][k]);
        }
    }
    i32(d, tree + 4, static_cast<std::int32_t>(piece) - static_cast<std::int32_t>(tree));
    const std::string text = "solid {\n\"index\" \"0\"\n\"name\" \"Bip01 Pelvis\"\n\"mass\" \"10\"\n\"origin\" \"1 2 3\"\n}\n"
                             "ragdollconstraint {\n\"parent\" \"0\"\n\"child\" \"1\"\n\"zmin\" \"-120\"\n"
                             "\"zmax\" \"4\"\n\"zfriction\" \"1\"\n}\n"
                             "editparams {\n\"totalmass\" \"10\"\n\"jointmerge\" \"Bip01,Bip01 Spine\"\n}\n"
                             "break {\n\"model\" \"gibs\\\\head\"\n\"health\" \"100\"\n\"offset\" \"0 0 103\"\n}\n";
    put(d, end, text.data(), text.size() + 1);
    return d;
}

} // namespace

TEST_CASE("PHY solids and text") {
    const auto phy = parse_phy(build_phy());
    CHECK(phy.checksum == 0x1234);
    REQUIRE(phy.solids.size() == 1);
    const auto& solid = phy.solids[0];
    CHECK(solid.mass_center[2] == doctest::Approx(-1.0f));
    CHECK(solid.radius == doctest::Approx(1.0f));
    REQUIRE(solid.pieces.size() == 1);
    const auto& piece = solid.pieces[0];
    CHECK(piece.triangles.size() == 4);
    REQUIRE(piece.vertices.size() == 4);
    // Physics (0, 1, 0) metres-inches -> model (0, 0, -1); (0, 0, 1) -> (0, 1, 0).
    CHECK(piece.vertices[2][2] == doctest::Approx(-1.0f));
    CHECK(piece.vertices[3][1] == doctest::Approx(1.0f));
    REQUIRE(phy.text.roots.size() == 4);
    CHECK(phy.text.roots[0].find("mass")->value == "10");
    REQUIRE(phy.solid_info.size() == 1);
    CHECK(phy.solid_info[0].name == "Bip01 Pelvis");
    CHECK(phy.solid_info[0].mass == 10.0f);
    CHECK(phy.solid_info[0].origin[2] == 3.0f);
    REQUIRE(phy.constraints.size() == 1);
    CHECK(phy.constraints[0].child == 1);
    CHECK(phy.constraints[0].limits[2].min == -120.0f);
    CHECK(phy.constraints[0].limits[2].friction == 1.0f);
    CHECK(phy.constraints[0].limits[0].max == 0.0f);
    CHECK(phy.edit.total_mass == 10.0f);
    REQUIRE(phy.edit.joint_merges.size() == 1);
    CHECK(phy.edit.joint_merges[0].second == "Bip01 Spine");
    REQUIRE(phy.breaks.size() == 1);
    CHECK(phy.breaks[0].model == "gibs\\\\head");
    CHECK(phy.breaks[0].offset[2] == 103.0f);
}

TEST_CASE("PHY rejects malformed files") {
    auto d = build_phy();
    SUBCASE("header size") {
        i32(d, 0, 20);
    }
    SUBCASE("solid size") {
        i32(d, 16, 1 << 20);
    }
    SUBCASE("tree outside the solid") {
        i32(d, 20 + 32, 1 << 20);
    }
    CHECK_THROWS_AS(parse_phy(d), FormatError);
}

TEST_CASE("PHY integration: every collision model in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, solids = 0, pieces = 0, triangles = 0, stale = 0, text_issues = 0;
    std::size_t solid_infos = 0, constraints = 0, breaks = 0, joint_merges = 0, bad_text = 0, bad_limits = 0;
    std::vector<float> errors, quantized_errors;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".phy")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        PhyFile phy;
        REQUIRE_NOTHROW(phy = parse_phy(*data));
        ++files;
        solids += phy.solids.size();
        text_issues += !phy.text.diagnostics.empty();
        solid_infos += phy.solid_info.size();
        constraints += phy.constraints.size();
        breaks += phy.breaks.size();
        joint_merges += phy.edit.joint_merges.size();
        const auto solid_count = static_cast<std::int32_t>(phy.solids.size());
        for (const auto& s : phy.solid_info) {
            bad_text += s.index < 0 || s.index >= solid_count || s.name.empty();
        }
        for (const auto& c : phy.constraints) {
            bad_text += c.parent < 0 || c.parent >= solid_count || c.child < 0 || c.child >= solid_count ||
                        c.parent == c.child;
            for (const auto& l : c.limits) {
                bad_limits += l.min > l.max || l.min < -180.0f || l.max > 180.0f;
            }
        }
        for (const auto& s : phy.solids) {
            pieces += s.pieces.size();
            for (const auto& p : s.pieces) {
                triangles += p.triangles.size();
            }
        }
        // Single-solid props: the collision bounds should match the model's.
        const auto mdl_data = gfs.read(path.substr(0, path.size() - 4) + ".mdl");
        if (!mdl_data) {
            continue;
        }
        const auto mdl = parse_mdl(*mdl_data);
        if (mdl.checksum != phy.checksum) {
            ++stale;
            continue;
        }
        if (phy.solids.size() != 1 || mdl.bones.size() != 1 || mdl.body_parts.empty() ||
            mdl.body_parts[0].models.empty() || mdl.body_parts[0].models[0].vertices.empty()) {
            continue;
        }
        std::array<float, 3> lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f}, mlo = lo, mhi = hi;
        for (const auto& p : phy.solids[0].pieces) {
            for (const auto& v : p.vertices) {
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], v[k]);
                    hi[k] = std::max(hi[k], v[k]);
                }
            }
        }
        for (const auto& v : mdl.body_parts[0].models[0].vertices) {
            for (int k = 0; k < 3; ++k) {
                mlo[k] = std::min(mlo[k], v.position[k]);
                mhi[k] = std::max(mhi[k], v.position[k]);
            }
        }
        float err = 0, extent = 1;
        for (int k = 0; k < 3; ++k) {
            err += std::abs(lo[k] - mlo[k]) + std::abs(hi[k] - mhi[k]);
            extent = std::max(extent, mhi[k] - mlo[k]);
        }
        (mdl.body_parts[0].models[0].vertex_format == MdlVertexFormat::Float ? errors : quantized_errors)
            .push_back(err / extent);
    }
    MESSAGE(files << " collision models, " << solids << " solids, " << pieces << " convex pieces, " << triangles
                  << " triangles; " << stale << " with a checksum that differs from their model; " << text_issues
                  << " with text diagnostics");
    CHECK(files == 2854);
    MESSAGE(solid_infos << " solid blocks, " << constraints << " ragdoll constraints, " << joint_merges
                        << " joint merges, " << breaks << " break pieces; " << bad_limits << " inverted limits");
    CHECK(solid_infos == 6785);
    CHECK(constraints == 3929);
    CHECK(breaks == 737);
    CHECK(bad_text == 0);
    CHECK(bad_limits == 0);
    // The axis conversion is right if a typical prop's collision bounds match its
    // model's; many hulls are deliberately simplified, so only the median counts.
    REQUIRE(!errors.empty());
    std::nth_element(errors.begin(), errors.begin() + static_cast<std::ptrdiff_t>(errors.size() / 2), errors.end());
    const float median = errors[errors.size() / 2];
    std::nth_element(quantized_errors.begin(),
                     quantized_errors.begin() + static_cast<std::ptrdiff_t>(quantized_errors.size() / 2),
                     quantized_errors.end());
    MESSAGE("median bounds error of " << errors.size() << " single-solid props with float vertices: " << median
                                      << "; of " << quantized_errors.size() << " with quantized vertices: "
                                      << quantized_errors[quantized_errors.size() / 2]);
    CHECK(median < 0.02f);
}
