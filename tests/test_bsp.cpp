#include "openbl/formats/bsp.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/zip.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

using namespace openbl;
using namespace openbl::formats;

namespace {

struct Writer {
    std::vector<std::byte> d;
    template <typename T> void put(T v) {
        const auto* p = reinterpret_cast<const std::byte*>(&v);
        d.insert(d.end(), p, p + sizeof(T));
    }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        d.insert(d.end(), b, b + n);
    }
    void vec(float x, float y, float z) {
        put(x);
        put(y);
        put(z);
    }
};

// A map with one 64x64 quad at z=0 (face 0) and one power-1 displacement on the same quad (face 1).
std::vector<std::byte> build_bsp() {
    std::array<Writer, bsp_lump::Count> lumps;
    const std::string entities = "{\n\"classname\" \"worldspawn\"\n\"skyname\" \"la\"\n}\n"
                                 "{\n\"classname\" \"light\"\n\"origin\" \"0 0 64\"\n}\n";
    lumps[bsp_lump::Entities].raw(entities.c_str(), entities.size() + 1);

    lumps[bsp_lump::Planes].vec(0, 0, 1);
    lumps[bsp_lump::Planes].put(0.0f);
    lumps[bsp_lump::Planes].put(2);

    const float corners[4][2] = {{0, 0}, {0, 64}, {64, 64}, {64, 0}};
    for (const auto& c : corners) {
        lumps[bsp_lump::Vertices].vec(c[0], c[1], 0);
    }
    for (std::uint16_t e = 0; e < 4; ++e) {
        lumps[bsp_lump::Edges].put(e);
        lumps[bsp_lump::Edges].put(static_cast<std::uint16_t>((e + 1) % 4));
    }
    for (std::int32_t e = 0; e < 4; ++e) {
        lumps[bsp_lump::SurfEdges].put(e);
    }

    const char name[] = "Brick\\Wall01";
    lumps[bsp_lump::TexDataStringData].raw(name, sizeof(name));
    lumps[bsp_lump::TexDataStringTable].put(0);
    lumps[bsp_lump::TexData].vec(0.5f, 0.5f, 0.5f);
    lumps[bsp_lump::TexData].put(0);
    for (std::int32_t v : {128, 128, 128, 128}) {
        lumps[bsp_lump::TexData].put(v);
    }
    // texinfo: u = x, v = y (texels), lightmap 1 luxel per 16 units.
    for (float f : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1 / 16.f, 0.f, 0.f, 0.f, 0.f, 1 / 16.f, 0.f, 0.f}) {
        lumps[bsp_lump::TexInfo].put(f);
    }
    lumps[bsp_lump::TexInfo].put(0);
    lumps[bsp_lump::TexInfo].put(0);

    for (std::int16_t disp : {std::int16_t{-1}, std::int16_t{0}}) {
        Writer& f = lumps[bsp_lump::Faces];
        for (int i = 0; i < 8; ++i) {
            f.put(std::uint32_t{0});
        }
        f.put(std::uint16_t{0});
        f.put(std::uint8_t{0});
        f.put(std::uint8_t{0});
        f.put(std::int32_t{0});
        f.put(std::uint16_t{4});
        f.put(std::uint16_t{0});
        f.put(disp);
        f.put(std::int16_t{-1});
        const std::uint8_t styles[8] = {0, 255, 255, 255, 255, 255, 255, 255};
        f.raw(styles, 8);
        f.raw(std::array<std::byte, 16>{}.data(), 16);
        f.put(std::int32_t{0}); // light offset
        f.put(4096.0f);
        f.put(std::int32_t{0});
        f.put(std::int32_t{0});
        f.put(std::int32_t{4}); // 5x5 luxels
        f.put(std::int32_t{4});
        f.put(std::int32_t{0});
        f.put(std::uint32_t{0});
    }
    lumps[bsp_lump::Lighting].d.resize(5 * 5 * 4);

    lumps[bsp_lump::Models].vec(0, 0, 0);
    lumps[bsp_lump::Models].vec(64, 64, 0);
    lumps[bsp_lump::Models].vec(0, 0, 0);
    for (std::int32_t v : {0, 0, 2}) {
        lumps[bsp_lump::Models].put(v);
    }

    // BSP tree: one node on plane 0 (z = 0); empty leaf above, solid leaf below.
    lumps[bsp_lump::Nodes].put(std::int32_t{0});
    lumps[bsp_lump::Nodes].put(std::int32_t{-1});
    lumps[bsp_lump::Nodes].put(std::int32_t{-2});
    lumps[bsp_lump::Nodes].d.resize(32);
    for (std::int32_t contents : {0, 1}) {
        Writer& l = lumps[bsp_lump::Leafs];
        const std::size_t at = l.d.size();
        l.put(contents);
        l.put(std::int16_t{static_cast<std::int16_t>(contents ? -1 : 0)});
        l.d.resize(at + 32);
        const std::int16_t no_water = -1;
        std::memcpy(l.d.data() + at + 28, &no_water, 2);
    }

    // One point light (88-byte record).
    {
        Writer& w = lumps[bsp_lump::WorldLights];
        w.vec(0, 0, 100);   // origin
        w.vec(500, 400, 300); // intensity
        w.vec(0, 0, -1);    // normal
        w.put(std::int32_t{0}); // cluster
        w.put(std::int32_t{1}); // type: point
        w.put(std::int32_t{0}); // style
        for (float f : {0.0f, 0.0f, 0.0f, 256.0f, 0.0f, 0.0f, 1.0f}) {
            w.put(f);
        }
        for (std::int32_t v : {0, -1, 0}) {
            w.put(v);
        }
    }

    // Displacement: power 1 (3x3), starting at corner (64, 64), centre raised by 10.
    Writer& di = lumps[bsp_lump::DispInfo];
    di.vec(64, 64, 0);
    for (std::int32_t v : {0, 0, 1, 0}) {
        di.put(v);
    }
    di.put(0.0f);
    di.put(1);
    di.put(std::uint16_t{1});
    di.d.resize(176);
    for (int i = 0; i < 9; ++i) {
        lumps[bsp_lump::DispVerts].vec(0, 0, 1);
        lumps[bsp_lump::DispVerts].put(i == 4 ? 10.0f : 0.0f);
        lumps[bsp_lump::DispVerts].put(i == 4 ? 255.0f : 0.0f);
    }

    // Game lump with one static prop.
    {
        Writer sprp;
        sprp.put(1);
        char model[128] = "Models/Props/Chair.mdl";
        sprp.raw(model, sizeof(model));
        sprp.put(0);
        sprp.put(1);
        sprp.vec(10, 20, 30);
        sprp.vec(0, 90, 0);
        sprp.put(std::uint16_t{0});
        sprp.put(std::uint16_t{0});
        sprp.put(std::uint16_t{0});
        sprp.put(std::uint8_t{6});
        sprp.put(std::uint8_t{0});
        sprp.put(std::int32_t{1});
        sprp.put(0.0f);
        sprp.put(500.0f);
        sprp.vec(0, 0, 0);
        lumps[bsp_lump::GameLump].put(1);
        lumps[bsp_lump::GameLump].put(std::uint32_t{0x73707270});
        lumps[bsp_lump::GameLump].put(std::uint16_t{0});
        lumps[bsp_lump::GameLump].put(std::uint16_t{4});
        lumps[bsp_lump::GameLump].put(std::int32_t{0}); // patched below
        lumps[bsp_lump::GameLump].put(static_cast<std::int32_t>(sprp.d.size()));
        lumps[bsp_lump::GameLump].raw(sprp.d.data(), sprp.d.size());
    }

    Writer out;
    out.raw("VBSP", 4);
    out.put(kBspVersion);
    const std::size_t header = 8 + 16 * bsp_lump::Count + 4;
    std::size_t offset = header;
    std::size_t game_lump_offset = 0;
    for (std::size_t i = 0; i < bsp_lump::Count; ++i) {
        out.put(static_cast<std::int32_t>(offset));
        out.put(static_cast<std::int32_t>(lumps[i].d.size()));
        out.put(std::int32_t{0});
        out.put(std::int32_t{0});
        if (i == bsp_lump::GameLump) {
            game_lump_offset = offset;
        }
        offset += lumps[i].d.size();
    }
    out.put(std::int32_t{7});
    for (const auto& l : lumps) {
        out.raw(l.d.data(), l.d.size());
    }
    const auto sprp_offset = static_cast<std::int32_t>(game_lump_offset + 4 + 16);
    std::memcpy(out.d.data() + game_lump_offset + 12, &sprp_offset, 4);
    return out.d;
}

// A zip with one stored and one deflated entry.
std::vector<std::byte> build_zip() {
    struct File {
        std::string name;
        std::string data;
        bool deflate;
    };
    const std::vector<File> files = {{"Materials\\a.vmt", "stored text", false},
                                     {"materials/b.vmt", std::string(200, 'x'), true}};
    Writer local, central;
    for (const File& f : files) {
        std::vector<Bytef> packed(compressBound(static_cast<uLong>(f.data.size())) + 16);
        uLong packed_size = 0;
        if (f.deflate) {
            z_stream zs{};
            deflateInit2(&zs, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(f.data.data()));
            zs.avail_in = static_cast<uInt>(f.data.size());
            zs.next_out = packed.data();
            zs.avail_out = static_cast<uInt>(packed.size());
            deflate(&zs, Z_FINISH);
            packed_size = zs.total_out;
            deflateEnd(&zs);
        } else {
            std::memcpy(packed.data(), f.data.data(), f.data.size());
            packed_size = static_cast<uLong>(f.data.size());
        }
        const auto local_offset = static_cast<std::uint32_t>(local.d.size());
        const std::uint16_t method = f.deflate ? 8 : 0;
        local.put(std::uint32_t{0x04034b50});
        local.put(std::uint16_t{20});
        local.put(std::uint16_t{0});
        local.put(method);
        local.put(std::uint32_t{0}); // time, date
        local.put(std::uint32_t{0}); // crc (not checked)
        local.put(static_cast<std::uint32_t>(packed_size));
        local.put(static_cast<std::uint32_t>(f.data.size()));
        local.put(static_cast<std::uint16_t>(f.name.size()));
        local.put(std::uint16_t{0});
        local.raw(f.name.data(), f.name.size());
        local.raw(packed.data(), packed_size);

        central.put(std::uint32_t{0x02014b50});
        central.put(std::uint16_t{20});
        central.put(std::uint16_t{20});
        central.put(std::uint16_t{0});
        central.put(method);
        central.put(std::uint32_t{0});
        central.put(std::uint32_t{0});
        central.put(static_cast<std::uint32_t>(packed_size));
        central.put(static_cast<std::uint32_t>(f.data.size()));
        central.put(static_cast<std::uint16_t>(f.name.size()));
        central.put(std::uint16_t{0});
        central.put(std::uint16_t{0});
        central.put(std::uint16_t{0});
        central.put(std::uint16_t{0});
        central.put(std::uint32_t{0});
        central.put(local_offset);
        central.raw(f.name.data(), f.name.size());
    }
    Writer out;
    out.raw(local.d.data(), local.d.size());
    out.raw(central.d.data(), central.d.size());
    out.put(std::uint32_t{0x06054b50});
    out.put(std::uint32_t{0});
    out.put(static_cast<std::uint16_t>(files.size()));
    out.put(static_cast<std::uint16_t>(files.size()));
    out.put(static_cast<std::uint32_t>(central.d.size()));
    out.put(static_cast<std::uint32_t>(local.d.size()));
    out.put(std::uint16_t{0});
    return out.d;
}

} // namespace

TEST_CASE("BSP entities") {
    const auto ents = parse_bsp_entities("{\n\"classname\" \"worldspawn\"\n}\n{ \"a\" \"b c\" \"origin\" \"1 2 3\" }");
    REQUIRE(ents.size() == 2);
    CHECK(ents[0].at("classname") == "worldspawn");
    CHECK(ents[1].at("a") == "b c");
    const auto escaped = parse_bsp_entities("{ \"python_script\" \"IsDead(\\\"Mandarin\\\")\" \"model\" \"a\\b.mdl\" }");
    CHECK(escaped[0].at("python_script") == "IsDead(\"Mandarin\")");
    CHECK(escaped[0].at("model") == "a\\b.mdl");
    const auto commented = parse_bsp_entities("{\n\"a\" \"1\"\n//\"OnTrigger\" \"x,Kill,,0,1,,\"\n\"b\" \"2\"\n}");
    CHECK(commented[0].size() == 2);
    CHECK(commented[0].at("b") == "2");
    const auto glued = parse_bsp_entities("{\n\"a\" \"1\"mop\n\"b\" \"2\"\n}");
    CHECK(glued[0].at("a") == "1");
    CHECK(glued[0].at("b") == "2");
    CHECK_THROWS_AS(parse_bsp_entities("{ \"a\" }"), FormatError);
    CHECK_THROWS_AS(parse_bsp_entities("{ \"a\" \"b\""), FormatError);
}

TEST_CASE("BSP parse") {
    const auto bsp = parse_bsp(build_bsp());
    CHECK(bsp.version == kBspVersion);
    CHECK(bsp.map_revision == 7);
    REQUIRE(bsp.entities.size() == 2);
    CHECK(bsp.entities[0].at("skyname") == "la");
    REQUIRE(bsp.texdatas.size() == 1);
    CHECK(bsp.texdatas[0].material == "brick/wall01");
    REQUIRE(bsp.faces.size() == 2);
    CHECK(bsp.faces[0].style_count() == 1);
    CHECK(bsp.faces[0].lightmap_size[0] == 4);
    CHECK(bsp.faces[1].dispinfo == 0);
    CHECK(bsp.face_polygon(bsp.faces[0]).size() == 4);
    REQUIRE(bsp.static_props.size() == 1);
    CHECK(bsp.static_prop_models[0] == "models/props/chair.mdl");
    CHECK(bsp.static_props[0].origin[2] == 30.0f);
    CHECK(bsp.static_props[0].angles[1] == 90.0f);
    CHECK(bsp.static_props[0].fade_max == 500.0f);
}

TEST_CASE("BSP face meshes") {
    const auto bsp = parse_bsp(build_bsp());
    const auto quad = build_face_mesh(bsp, 0);
    CHECK(quad.positions.size() == 4);
    CHECK(quad.indices.size() == 6);
    CHECK(quad.uvs[2][0] == doctest::Approx(0.5f)); // x = 64 over a 128-texel texture

    const auto disp = build_face_mesh(bsp, 1);
    REQUIRE(disp.positions.size() == 9);
    CHECK(disp.indices.size() == 2 * 2 * 6);
    // The grid starts at the corner nearest the start position (64, 64).
    CHECK(disp.positions[0][0] == 64.0f);
    CHECK(disp.positions[0][1] == 64.0f);
    // The centre vertex is displaced 10 units along +z, with full alpha.
    CHECK(disp.positions[4][0] == 32.0f);
    CHECK(disp.positions[4][2] == 10.0f);
    CHECK(disp.alphas[4] == 1.0f);
    // Texture coordinates use the undisplaced surface.
    CHECK(disp.uvs[4][0] == doctest::Approx(0.25f));
}

TEST_CASE("BSP world lights and line of sight") {
    const auto bsp = parse_bsp(build_bsp());
    REQUIRE(bsp.world_lights.size() == 1);
    const auto& light = bsp.world_lights[0];
    CHECK(light.type == BspLightType::Point);
    CHECK(light.origin[2] == 100.0f);
    CHECK(light.intensity[1] == 400.0f);
    CHECK(light.radius == 256.0f);
    CHECK(light.quadratic_attn == 1.0f);
    CHECK(light.texinfo == -1);

    REQUIRE(bsp.nodes.size() == 1);
    REQUIRE(bsp.leafs.size() == 2);
    CHECK(bsp.leaf_at({0, 0, 10}) == 0);
    CHECK(bsp.leaf_at({0, 0, -10}) == 1);
    CHECK_FALSE(bsp.segment_blocked({0, 0, 10}, {50, 50, 5}));
    CHECK(bsp.segment_blocked({0, 0, 10}, {0, 0, -5}));
    CHECK(bsp.segment_blocked({0, 0, -10}, {0, 0, -20}));
    CHECK_FALSE(bsp.segment_blocked({0, 0, -10}, {0, 0, -20}, contents::Water));
}

TEST_CASE("BSP lightmap coordinates and RGBE") {
    const auto bsp = parse_bsp(build_bsp());
    const auto quad = build_face_mesh(bsp, 0);
    // 1 luxel per 16 units, mins 0: corner (64, 64) is luxel (4, 4).
    CHECK(quad.lightmap_uvs[2][0] == doctest::Approx(4.0f));
    CHECK(quad.lightmap_uvs[2][1] == doctest::Approx(4.0f));
    CHECK(quad.lightmap_uvs[0][0] == doctest::Approx(0.0f));

    const std::byte luxel[4] = {std::byte{255}, std::byte{128}, std::byte{0}, std::byte{0xFF}}; // exponent -1
    const auto rgb = decode_rgbe(luxel);
    CHECK(rgb[0] == doctest::Approx(0.5f));
    CHECK(rgb[1] == doctest::Approx(128.0f / 255.0f / 2.0f));
    CHECK(rgb[2] == 0.0f);
}

TEST_CASE("BSP rejects malformed files") {
    auto data = build_bsp();
    SUBCASE("signature") {
        data[0] = std::byte{'X'};
    }
    SUBCASE("version") {
        data[4] = std::byte{20};
    }
    SUBCASE("lump out of range") {
        data[8 + 16 * bsp_lump::Faces + 3] = std::byte{0x7f};
    }
    SUBCASE("truncated") {
        data.resize(100);
    }
    CHECK_THROWS_AS(parse_bsp(data), FormatError);
}

TEST_CASE("Zip archives") {
    const auto data = build_zip();
    const ZipArchive zip(data);
    REQUIRE(zip.entries().size() == 2);
    const auto* a = zip.find("MATERIALS/A.VMT");
    REQUIRE(a);
    const auto bytes_a = zip.read(*a);
    CHECK(std::string(reinterpret_cast<const char*>(bytes_a.data()), bytes_a.size()) == "stored text");
    const auto* b = zip.find("materials/b.vmt");
    REQUIRE(b);
    CHECK(b->method == 8);
    CHECK(zip.read(*b).size() == 200);
    CHECK(zip.find("missing") == nullptr);

    auto bad = data;
    bad.resize(10);
    CHECK_THROWS_AS(ZipArchive{bad}, FormatError);
}

TEST_CASE("BSP brush hulls") {
    // A 2 x 4 x 6 box from six axis planes, plus a diagonal plane cutting a corner.
    BspFile bsp;
    const Vec3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const float dists[] = {1, 1, 2, 2, 3, 3};
    for (std::size_t i = 0; i < 6; ++i) {
        bsp.planes.push_back({normals[i], dists[i]});
    }
    const float r = std::sqrt(0.5f);
    bsp.planes.push_back({{r, r, 0}, 2.0f * r}); // x + y <= 2
    for (std::uint16_t i = 0; i < 7; ++i) {
        bsp.brush_sides.push_back({i});
    }
    bsp.brushes.push_back({0, 7, contents::Solid});
    const auto hull = bsp.brush_hull(0);
    CHECK(hull.faces.size() == 7);
    CHECK(hull.min[0] == doctest::Approx(-1.0f));
    CHECK(hull.max[1] == doctest::Approx(2.0f));
    CHECK(hull.max[2] == doctest::Approx(3.0f));
    for (const auto& face : hull.faces) {
        CHECK(face.size() >= 3);
        for (const auto& v : face) {
            CHECK(bsp.point_in_brush(0, v, 0.01f));
        }
    }
    CHECK(bsp.point_in_brush(0, {0, 0, 0}));
    CHECK_FALSE(bsp.point_in_brush(0, {0.9f, 1.9f, 0}));
    CHECK(bsp.point_in_brush(0, {0.9f, 1.9f, 0}, 1.0f));
}

TEST_CASE("BSP integration: brush hulls match the compiled world collision") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t solid_brushes = 0, empty_hulls = 0, collision_points = 0, covered_points = 0, disp_points = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".bsp")) {
            continue;
        }
        INFO(path);
        const auto bsp = parse_bsp(*gfs.read(path));
        // Bucket brush bounds in 256-unit cells to find candidates quickly.
        constexpr float kCell = 256.0f;
        std::map<std::array<int, 3>, std::vector<std::size_t>> grid;
        for (std::size_t b = 0; b < bsp.brushes.size(); ++b) {
            const auto hull = bsp.brush_hull(b);
            if ((bsp.brushes[b].contents & contents::Solid) != 0) {
                ++solid_brushes;
                empty_hulls += hull.faces.size() < 4;
            }
            if (hull.faces.empty()) {
                continue;
            }
            std::array<int, 3> lo{}, hi{};
            for (std::size_t k = 0; k < 3; ++k) {
                lo[k] = static_cast<int>(std::floor((hull.min[k] - 1.0f) / kCell));
                hi[k] = static_cast<int>(std::floor((hull.max[k] + 1.0f) / kCell));
            }
            for (int x = lo[0]; x <= hi[0]; ++x) {
                for (int y = lo[1]; y <= hi[1]; ++y) {
                    for (int z = lo[2]; z <= hi[2]; ++z) {
                        grid[{x, y, z}].push_back(b);
                    }
                }
            }
        }
        if (bsp.collision.empty() || bsp.collision[0].model != 0) {
            continue;
        }
        // Displacements have collision but no brushes: bucket their vertices too.
        std::map<std::array<int, 3>, std::vector<Vec3>> disp_grid;
        for (std::size_t f = 0; f < bsp.faces.size(); ++f) {
            if (bsp.faces[f].dispinfo < 0) {
                continue;
            }
            for (const auto& v : build_face_mesh(bsp, f).positions) {
                disp_grid[{static_cast<int>(std::floor(v[0] / kCell)), static_cast<int>(std::floor(v[1] / kCell)),
                           static_cast<int>(std::floor(v[2] / kCell))}]
                    .push_back(v);
            }
        }
        // Every vertex of the world's physics hulls should lie on or in a brush.
        for (const auto& s : bsp.collision[0].solids) {
            for (const auto& p : s.pieces) {
                for (const auto& v : p.vertices) {
                    ++collision_points;
                    const std::array<int, 3> cell{static_cast<int>(std::floor(v[0] / kCell)),
                                                  static_cast<int>(std::floor(v[1] / kCell)),
                                                  static_cast<int>(std::floor(v[2] / kCell))};
                    const auto it = grid.find(cell);
                    const bool in_brush =
                        it != grid.end() && std::any_of(it->second.begin(), it->second.end(), [&](std::size_t b) {
                            return bsp.point_in_brush(b, v, 1.0f);
                        });
                    covered_points += in_brush;
                    if (!in_brush) {
                        const auto d = disp_grid.find(cell);
                        disp_points += d != disp_grid.end() &&
                                       std::any_of(d->second.begin(), d->second.end(), [&](const Vec3& w) {
                                           return std::abs(w[0] - v[0]) < 1.0f && std::abs(w[1] - v[1]) < 1.0f &&
                                                  std::abs(w[2] - v[2]) < 1.0f;
                                       });
                    }
                }
            }
        }
    }
    MESSAGE(solid_brushes << " solid brushes (" << empty_hulls << " without a closed hull); " << covered_points
                          << " of " << collision_points << " world collision vertices on or in a brush, "
                          << disp_points << " more on displacements");
    CHECK(empty_hulls == 0);
    // Brushes and displacements together account for the compiled collision.
    CHECK(covered_points + disp_points > collision_points * 9999 / 10000);
}

TEST_CASE("BSP decal projection") {
    // One 128 x 128 floor face at z = 0, facing up.
    BspFile bsp;
    bsp.planes.push_back({{0, 0, 1}, 0});
    bsp.vertices = {{-64, -64, 0}, {-64, 64, 0}, {64, 64, 0}, {64, -64, 0}};
    bsp.edges = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    bsp.surf_edges = {0, 1, 2, 3};
    bsp.texdatas.push_back({});
    bsp.texdatas[0].material = "concrete/floor";
    bsp.texinfos.push_back({});
    bsp.texinfos[0].texdata = 0;
    BspFace face;
    face.num_edges = 4;
    bsp.faces.push_back(face);
    BspModel world;
    world.num_faces = 1;
    bsp.models.push_back(world);

    auto pieces = project_decal(bsp, {10, 0, 2}, 32, 16);
    REQUIRE(pieces.size() == 1);
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (std::size_t i = 0; i < pieces[0].positions.size(); ++i) {
        const auto& p = pieces[0].positions[i];
        for (std::size_t k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
        // On floors the decal's up is world +y, so v = 0 along the +y edge.
        if (p[1] == doctest::Approx(8.0f)) {
            CHECK(pieces[0].uvs[i][1] == doctest::Approx(0.0f));
        }
    }
    CHECK(lo[0] == doctest::Approx(-6.0f));
    CHECK(hi[0] == doctest::Approx(26.0f));
    CHECK(hi[1] == doctest::Approx(8.0f));
    CHECK(hi[2] == doctest::Approx(0.0f));
    CHECK(project_decal(bsp, {10, 0, -2}, 32, 16).empty()); // Behind the face.
    CHECK(project_decal(bsp, {10, 0, 6}, 32, 16).empty());  // Too far in front.
    // Clipped at the face's edge.
    pieces = project_decal(bsp, {60, 0, 0}, 32, 16);
    REQUIRE(pieces.size() == 1);
    float max_x = -1e9f, max_u = -1e9f;
    for (std::size_t i = 0; i < pieces[0].positions.size(); ++i) {
        max_x = std::max(max_x, pieces[0].positions[i][0]);
        max_u = std::max(max_u, pieces[0].uvs[i][0]);
    }
    CHECK(max_x == doctest::Approx(64.0f));
    CHECK(max_u == doctest::Approx(0.625f));
}

TEST_CASE("BSP lightmap trace") {
    // A 64 x 64 floor face at z = 0 on the only node; empty above, solid below.
    BspFile bsp;
    bsp.planes.push_back({{0, 0, 1}, 0});
    bsp.vertices = {{0, 0, 0}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0}};
    bsp.edges = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    bsp.surf_edges = {0, 1, 2, 3};
    bsp.texinfos.push_back({});
    bsp.texinfos[0].lightmap_vecs = {{{1 / 16.f, 0, 0, 0}, {0, 1 / 16.f, 0, 0}}};
    BspFace face;
    face.num_edges = 4;
    face.styles = {0, 255, 255, 255, 255, 255, 255, 255};
    face.light_offset = 0;
    face.lightmap_size = {4, 4};
    bsp.faces.push_back(face);
    bsp.lighting.resize(5 * 5 * 4);
    BspNode node;
    node.children = {-1, -2};
    node.num_faces = 1;
    bsp.nodes.push_back(node);
    bsp.leafs.resize(2);
    bsp.leafs[1].contents = contents::Solid;
    bsp.models.push_back({});

    const auto hit = bsp.trace_lightmap({40, 24, 10}, {40, 24, -10});
    REQUIRE(hit);
    CHECK(hit->face == 0);
    CHECK(hit->point[2] == doctest::Approx(0.0f));
    CHECK(hit->luxel[0] == doctest::Approx(2.5f));
    CHECK(hit->luxel[1] == doctest::Approx(1.5f));
    CHECK_FALSE(bsp.trace_lightmap({100, 24, 10}, {100, 24, -10})); // Off the face: blocked by solid.
    CHECK_FALSE(bsp.trace_lightmap({40, 24, 10}, {50, 24, 20}));    // Never reaches the plane.
    CHECK_FALSE(bsp.trace_lightmap({40, 24, -10}, {40, 24, 10}));   // Starts inside solid.
}

TEST_CASE("BSP integration: infodecals land on world faces") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t decals = 0, placed = 0, pieces = 0, bad_uvs = 0;
    std::map<std::string, std::size_t> unplaced_by_map;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".bsp")) {
            continue;
        }
        const auto bsp = parse_bsp(*gfs.read(path));
        for (const auto& e : bsp.entities) {
            const auto cls = e.find("classname");
            const auto origin = e.find("origin");
            if (cls == e.end() || cls->second != "infodecal" || origin == e.end()) {
                continue;
            }
            Vec3 o{};
            std::sscanf(origin->second.c_str(), "%f %f %f", &o[0], &o[1], &o[2]);
            const auto result = project_decal(bsp, o, 32.0f, 32.0f);
            ++decals;
            placed += !result.empty();
            unplaced_by_map[path] += result.empty();
            pieces += result.size();
            for (const auto& p : result) {
                for (const auto& uv : p.uvs) {
                    bad_uvs += uv[0] < -0.001f || uv[0] > 1.001f || uv[1] < -0.001f || uv[1] > 1.001f;
                }
            }
        }
    }
    std::string worst;
    for (const auto& [map, n] : unplaced_by_map) {
        if (n > 0 && worst.size() < 300) {
            worst += map + " " + std::to_string(n) + ", ";
        }
    }
    MESSAGE(placed << " of " << decals << " infodecals land on a face (" << pieces << " pieces); unplaced: " << worst);
    CHECK(decals == 4609);
    CHECK(bad_uvs == 0);
    // The rest are probably on props, or float 8 to 64 units off the nearest face.
    CHECK(placed >= 4401);
}

TEST_CASE("BSP integration: every map in the real install loads") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t maps = 0, faces = 0, triangles = 0, props = 0, missing_materials = 0, materials = 0,
                missing_props = 0;
    std::size_t brushes = 0, clusters = 0, bad_vis = 0, detail_props = 0, bad_detail_styles = 0, cubemaps = 0,
                collision_models = 0, collision_triangles = 0, world_collision = 0, primitive_faces = 0,
                bad_primitive_area = 0, disp_samples = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".bsp")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        BspFile bsp;
        REQUIRE_NOTHROW(bsp = parse_bsp(*data));
        ++maps;
        REQUIRE_FALSE(bsp.models.empty());
        REQUIRE_FALSE(bsp.entities.empty());
        CHECK(bsp.entities[0].at("classname") == "worldspawn");
        const ZipArchive pak(bsp.pakfile);
        for (const auto& td : bsp.texdatas) {
            const std::string vmt = "materials/" + td.material + ".vmt";
            ++materials;
            missing_materials += !gfs.exists(vmt) && !pak.find(vmt);
        }
        for (const auto& e : pak.entries()) {
            CHECK_NOTHROW(pak.read(e));
        }
        auto area = [](const Vec3& a, const Vec3& b, const Vec3& c) {
            const Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const Vec3 w{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const Vec3 n{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
            return 0.5 * std::sqrt(double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2]);
        };
        for (std::size_t f = 0; f < bsp.faces.size(); ++f) {
            BspFaceMesh mesh;
            REQUIRE_NOTHROW(mesh = build_face_mesh(bsp, f));
            triangles += mesh.indices.size() / 3;
            if (bsp.faces[f].num_primitives > 0 && bsp.faces[f].dispinfo < 0) {
                // Primitive strips must cover exactly the face polygon.
                const auto poly = bsp.face_polygon(bsp.faces[f]);
                double poly_area = 0, mesh_area = 0;
                for (std::size_t i = 2; i < poly.size(); ++i) {
                    poly_area += area(poly[0], poly[i - 1], poly[i]);
                }
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    mesh_area += area(mesh.positions[mesh.indices[i]], mesh.positions[mesh.indices[i + 1]],
                                      mesh.positions[mesh.indices[i + 2]]);
                }
                ++primitive_faces;
                // 512 of 516 match to 1%; four (ash sewer, warrens 3 and 5) lose a
                // 1.5-4% sliver, apparently dropped by the map compiler.
                bad_primitive_area += std::abs(mesh_area - poly_area) > 0.05 * poly_area + 0.01;
            }
        }
        faces += bsp.faces.size();
        props += bsp.static_props.size();
        for (const auto& m : bsp.static_prop_models) {
            missing_props += !gfs.exists(m);
        }

        brushes += bsp.brushes.size();
        detail_props += bsp.detail_props.size();
        cubemaps += bsp.cubemaps.size();
        for (const auto& d : bsp.detail_props) {
            bad_detail_styles += d.first_light_style != 0xFFFFFFFF &&
                                 std::size_t{d.first_light_style} + d.light_style_count > bsp.detail_light_styles.size();
        }
        // Every cluster decompresses and sees itself.
        clusters += static_cast<std::size_t>(bsp.visibility.clusters);
        for (std::int32_t c = 0; c < bsp.visibility.clusters; ++c) {
            std::vector<std::uint8_t> row;
            REQUIRE_NOTHROW(row = bsp.visibility.decompress(c));
            bad_vis += ((row[static_cast<std::size_t>(c) >> 3] >> (c & 7)) & 1) == 0;
        }
        collision_models += bsp.collision.size();
        for (const auto& cm : bsp.collision) {
            for (const auto& s : cm.solids) {
                for (const auto& p : s.pieces) {
                    collision_triangles += p.triangles.size();
                }
            }
        }
        world_collision += !bsp.collision.empty() && bsp.collision[0].model == 0;
        disp_samples += bsp.disp_lightmap_samples.size();
        CHECK(bsp.disp_lightmap_samples.size() == bsp.disp_lightmap_alphas.size());
    }
    MESSAGE(maps << " maps, " << faces << " faces, " << triangles << " triangles, " << props << " static props ("
                 << missing_props << " prop models missing), " << missing_materials << " of " << materials
                 << " materials missing");
    MESSAGE(brushes << " brushes, " << clusters << " visibility clusters, " << detail_props << " detail props, "
                    << cubemaps << " cubemaps, " << collision_models << " collision models with "
                    << collision_triangles << " triangles");
    CHECK(maps > 100);
    CHECK(bad_vis == 0);
    CHECK(bad_detail_styles == 0);
    CHECK(world_collision == maps);
    MESSAGE(disp_samples << " displacement lightmap samples");
    MESSAGE(primitive_faces << " faces triangulated from primitives, " << bad_primitive_area
                            << " whose area differs from the polygon");
    CHECK(bad_primitive_area == 0);
}
