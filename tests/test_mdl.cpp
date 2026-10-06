#include "openbl/formats/error.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/vtx.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <map>

using namespace openbl;
using namespace openbl::formats;

namespace {

// Little-endian byte buffer with patchable fields.
struct Buf {
    std::vector<std::byte> d;

    std::size_t size() const { return d.size(); }
    void grow(std::size_t n) { d.resize(std::max(d.size(), n)); }
    void put(std::size_t at, const void* src, std::size_t n) {
        grow(at + n);
        std::memcpy(d.data() + at, src, n);
    }
    void i32(std::size_t at, std::int32_t v) { put(at, &v, 4); }
    void u16(std::size_t at, std::uint16_t v) { put(at, &v, 2); }
    void u8(std::size_t at, std::uint8_t v) { put(at, &v, 1); }
    void f32(std::size_t at, float v) { put(at, &v, 4); }
    std::size_t str(const std::string& s) {
        const std::size_t at = d.size();
        put(at, s.c_str(), s.size() + 1);
        return at;
    }
};

// Builds a one-triangle model with the given vertex format.
std::vector<std::byte> build_mdl(MdlVertexFormat format) {
    Buf b;
    b.grow(0x1a8);
    b.put(0, "IDST", 4);
    b.i32(4, kMdlVersion);
    b.i32(8, 0x1234);
    b.put(0x0c, "test/tri.mdl", 12);
    b.f32(0xc0, 10.0f);

    const std::size_t bone = 0x1a8;
    b.i32(0xf0, 1);
    b.i32(0xf4, static_cast<std::int32_t>(bone));
    b.grow(bone + 160);
    b.i32(bone + 4, -1);
    b.f32(bone + 0x20, 1.0f);
    b.f32(bone + 0x38, 1.0f); // quaternion w
    b.f32(bone + 0x58, 1.0f);
    b.f32(bone + 0x58 + 20, 1.0f);
    b.f32(bone + 0x58 + 40, 1.0f);

    const std::size_t part = bone + 160;
    const std::size_t model = part + 16;
    const std::size_t mesh = model + 224;
    const std::size_t verts = mesh + 60;
    b.i32(0x140, 1);
    b.i32(0x144, static_cast<std::int32_t>(part));
    b.grow(verts);
    b.i32(part + 4, 1);
    b.i32(part + 8, 1);
    b.i32(part + 12, 16);
    b.put(model, "tri.smd", 8);
    b.i32(model + 0x88, 1);
    b.i32(model + 0x8c, 224);
    b.i32(model + 0x90, 3);
    b.i32(model + 0x94, 224 + 60);
    b.i32(model + 0x9c, static_cast<std::int32_t>(format));
    b.i32(mesh + 4, -224);
    b.i32(mesh + 8, 3);

    const float px[3] = {0.0f, 4.0f, 0.0f};
    const float py[3] = {0.0f, 0.0f, 2.0f};
    switch (format) {
    case MdlVertexFormat::Float:
        for (std::size_t i = 0; i < 3; ++i) {
            const std::size_t v = verts + 44 * i;
            b.u8(v, 255);
            b.f32(v + 12, px[i]);
            b.f32(v + 16, py[i]);
            b.f32(v + 20, 1.0f);
            b.f32(v + 32, 1.0f); // normal +z
            b.f32(v + 36, 0.5f);
            b.f32(v + 40, 0.25f);
        }
        break;
    case MdlVertexFormat::Quantized16:
        b.f32(model + 0xa0, -1.0f); // origin
        b.f32(model + 0xac, 5.0f / 65535.0f);
        b.f32(model + 0xb0, 3.0f / 65535.0f);
        b.f32(model + 0xb4, 1.0f);
        for (std::size_t i = 0; i < 3; ++i) {
            const std::size_t v = verts + 12 * i;
            b.u16(v, static_cast<std::uint16_t>(std::lround((px[i] + 1.0f) / 5.0f * 65535.0f)));
            b.u16(v + 2, static_cast<std::uint16_t>(std::lround(py[i] / 3.0f * 65535.0f)));
            b.u16(v + 6, 12);
            b.u16(v + 8, 65535);
        }
        break;
    case MdlVertexFormat::Quantized8:
        b.f32(model + 0xa8, 1.0f);  // center z
        b.f32(model + 0xac, 4.0f);  // half extents
        b.f32(model + 0xb0, 2.0f);
        b.f32(model + 0xb4, 1.0f);
        for (std::size_t i = 0; i < 3; ++i) {
            const std::size_t v = verts + 8 * i;
            b.u8(v, static_cast<std::uint8_t>(std::lround((px[i] / 4.0f + 1.0f) * 127.5f)));
            b.u8(v + 1, static_cast<std::uint8_t>(std::lround((py[i] / 2.0f + 1.0f) * 127.5f)));
            b.u8(v + 2, 255);
            b.u8(v + 3, 7);
        }
        break;
    }

    const std::size_t tex = b.size();
    b.grow(tex + 20);
    b.i32(0x124, 1);
    b.i32(0x128, static_cast<std::int32_t>(tex));
    const std::size_t tex_name = b.str("tri_skin");
    b.i32(tex, static_cast<std::int32_t>(tex_name - tex));
    const std::size_t dirs = b.size();
    b.grow(dirs + 4);
    b.i32(0x12c, 1);
    b.i32(0x130, static_cast<std::int32_t>(dirs));
    b.i32(dirs, static_cast<std::int32_t>(b.str("models/test/")));
    const std::size_t skins = b.size();
    b.u16(skins, 0);
    b.i32(0x134, 1);
    b.i32(0x138, 1);
    b.i32(0x13c, static_cast<std::int32_t>(skins));
    // Animation scales: 1/256 per position unit, 1/32768 per quaternion component.
    for (std::size_t k = 0; k < 3; ++k) {
        b.f32(bone + 0x3c + 4 * k, 1.0f / 256.0f);
    }
    for (std::size_t k = 0; k < 4; ++k) {
        b.f32(bone + 0x48 + 4 * k, 1.0f / 32768.0f);
    }

    // One 3-frame animation: position x runs 1.0, 2.0, (2.0 repeated); rotation z
    // is a constant 0.5; every other channel keeps the bone default.
    const std::size_t anim = b.size();
    b.grow(anim + 72 + 32);
    b.i32(0x108, 1);
    b.i32(0x10c, static_cast<std::int32_t>(anim));
    b.f32(anim + 4, 30.0f);
    b.i32(anim + 8, 1);
    b.i32(anim + 12, 3);
    b.i32(anim + 48, 72);
    const std::size_t record = anim + 72;
    b.f32(record, 1.0f);
    const std::size_t pos_x = b.size();
    b.u8(pos_x, 2); // valid
    b.u8(pos_x + 1, 3); // total
    b.u16(pos_x + 2, 256);
    b.u16(pos_x + 4, 512);
    const std::size_t rot_z = b.size();
    b.u8(rot_z, 1);
    b.u8(rot_z + 1, 3);
    b.u16(rot_z + 2, 16384);
    b.i32(record + 4, static_cast<std::int32_t>(pos_x - record));
    b.i32(record + 4 + 4 * 5, static_cast<std::int32_t>(rot_z - record));

    const std::size_t seq = b.size();
    b.grow(seq + 764);
    b.i32(0x110, 1);
    b.i32(0x114, static_cast<std::int32_t>(seq));
    b.i32(seq + 8, 1);
    b.i32(seq + 12, -1);
    b.i32(seq + 16, 1);
    b.i32(seq + 52, 1);
    b.i32(seq + 56, 0);

    const std::size_t include = b.size();
    b.grow(include + 116);
    b.i32(0x194, 1);
    b.i32(0x198, static_cast<std::int32_t>(include));

    b.i32(anim, static_cast<std::int32_t>(b.str("@idle") - anim));
    b.i32(seq, static_cast<std::int32_t>(b.str("idle") - seq));
    b.i32(seq + 4, static_cast<std::int32_t>(b.str("ACT_IDLE") - seq));
    b.i32(include, static_cast<std::int32_t>(b.str("Models\\Shared\\Anims.mdl") - include));
    b.i32(bone, static_cast<std::int32_t>(b.str("root") - bone));
    b.i32(part, static_cast<std::int32_t>(b.str("body") - part));
    b.i32(0x8c, static_cast<std::int32_t>(b.size()));
    return b.d;
}

std::vector<std::byte> build_vtx(std::uint8_t group_flags) {
    Buf b;
    b.i32(0, kVtxVersion);
    b.i32(0x10, 0x1234);
    b.i32(0x14, 1);
    b.i32(0x1c, 1);
    b.i32(0x20, 0x24);
    b.i32(0x24, 1); // body part: 1 model
    b.i32(0x28, 8);
    b.i32(0x2c, 1); // model: 1 lod
    b.i32(0x30, 8);
    b.i32(0x34, 1); // lod: 1 mesh
    b.i32(0x38, 12);
    b.u16(0x40, 1); // mesh: 1 strip group
    b.i32(0x44, 8);
    const std::size_t g = 0x48;
    const std::size_t record = (group_flags & vtx_flags::CompactVertices) ? 2 : 12;
    const std::size_t verts = g + 20 + 16;
    const std::size_t indices = verts + 3 * record;
    b.u16(g, 3);
    b.u16(g + 2, 3);
    b.u16(g + 4, 1);
    b.u8(g + 6, group_flags);
    b.i32(g + 8, static_cast<std::int32_t>(verts - g));
    b.i32(g + 12, static_cast<std::int32_t>(indices - g));
    b.i32(g + 16, 20);
    b.u16(g + 20, 3); // strip: 3 indices at 0, 3 vertices at 0
    b.u16(g + 24, 3);
    const std::uint16_t refs[3] = {2, 0, 1};
    for (std::size_t i = 0; i < 3; ++i) {
        b.u16(verts + record * i, 0xAAAA); // bone palette bytes for full records
        b.u16(verts + record * i + record - 2, refs[i]);
        b.u16(indices + 2 * i, static_cast<std::uint16_t>(i));
    }
    return b.d;
}

// Tolerance covers 8-bit quantization (4 / 127.5 units per step).
void check_triangle(const MdlModel& m) {
    REQUIRE(m.vertices.size() == 3);
    CHECK(std::abs(m.vertices[1].position[0] - 4.0f) < 0.02f);
    CHECK(std::abs(m.vertices[2].position[1] - 2.0f) < 0.02f);
    CHECK(std::abs(m.vertices[0].position[0]) < 0.02f);
}

} // namespace

TEST_CASE("MDL float vertices") {
    const auto mdl = parse_mdl(build_mdl(MdlVertexFormat::Float));
    CHECK(mdl.name == "test/tri.mdl");
    CHECK(mdl.checksum == 0x1234);
    REQUIRE(mdl.bones.size() == 1);
    CHECK(mdl.bones[0].name == "root");
    CHECK(mdl.bones[0].parent == -1);
    CHECK(mdl.bones[0].quaternion[3] == 1.0f);
    CHECK(mdl.bones[0].pose_to_bone[2][2] == 1.0f);
    CHECK(mdl.textures == std::vector<std::string>{"tri_skin"});
    CHECK(mdl.texture_dirs == std::vector<std::string>{"models/test/"});
    CHECK(mdl.texture_for(0) == 0);
    CHECK(mdl.texture_for(1) == -1);
    REQUIRE(mdl.body_parts.size() == 1);
    CHECK(mdl.body_parts[0].name == "body");
    const auto& model = mdl.body_parts[0].models.at(0);
    CHECK(model.name == "tri.smd");
    check_triangle(model);
    CHECK(model.vertices[0].has_normal);
    CHECK(model.vertices[0].normal[2] == 1.0f);
    CHECK(model.vertices[0].uv[1] == 0.25f);
    CHECK(model.vertices[0].weights[0] == 255);
}

TEST_CASE("MDL quantized vertices") {
    const auto q16 = parse_mdl(build_mdl(MdlVertexFormat::Quantized16));
    const auto& m16 = q16.body_parts[0].models[0];
    CHECK(m16.vertex_format == MdlVertexFormat::Quantized16);
    check_triangle(m16);
    CHECK_FALSE(m16.vertices[0].has_normal);
    CHECK(m16.vertices[0].packed_normal == 12);
    CHECK(m16.vertices[0].uv[0] == 1.0f);
    CHECK(m16.vertices[0].position[2] == doctest::Approx(0.0f));

    const auto q8 = parse_mdl(build_mdl(MdlVertexFormat::Quantized8));
    const auto& m8 = q8.body_parts[0].models[0];
    check_triangle(m8);
    CHECK(m8.vertices[0].packed_normal == 7);
    CHECK(m8.vertices[0].position[2] == doctest::Approx(2.0f));
}

TEST_CASE("MDL animations, sequences and include models") {
    const auto mdl = parse_mdl(build_mdl(MdlVertexFormat::Float));
    REQUIRE(mdl.animations.size() == 1);
    CHECK(mdl.animations[0].name == "@idle");
    CHECK(mdl.animations[0].frame_count == 3);
    CHECK(mdl.animations[0].fps == 30.0f);
    REQUIRE(mdl.sequences.size() == 1);
    CHECK(mdl.sequences[0].label == "idle");
    CHECK(mdl.sequences[0].activity_name == "ACT_IDLE");
    CHECK((mdl.sequences[0].flags & sequence_flags::Looping) != 0);
    CHECK(mdl.sequences[0].animation == 0);
    CHECK(mdl.include_models == std::vector<std::string>{"models/shared/anims.mdl"});
    CHECK(mdl.bones[0].position_scale[0] == doctest::Approx(1.0f / 256.0f));

    const auto f0 = mdl.sample_animation(0, 0);
    const auto f1 = mdl.sample_animation(0, 1);
    const auto f2 = mdl.sample_animation(0, 2);
    REQUIRE(f0.size() == 1);
    // Positions are offsets from the bind position (x = 1.0 for this bone).
    CHECK(f0[0].position[0] == doctest::Approx(2.0f));
    CHECK(f1[0].position[0] == doctest::Approx(3.0f));
    CHECK(f2[0].position[0] == doctest::Approx(3.0f)); // last value repeats
    CHECK(f0[0].position[1] == 0.0f);                  // bone default
    CHECK(f0[0].rotation[2] == doctest::Approx(0.5f)); // rotations are absolute
    CHECK(f0[0].rotation[3] == 1.0f);                  // default quaternion w
    CHECK(mdl.sample_animation(0, 99)[0].position[0] == doctest::Approx(3.0f)); // clamped
    CHECK_THROWS_AS(mdl.sample_animation(1, 0), FormatError);
}

TEST_CASE("MDL world and skinning transforms") {
    // Root at the origin; child 10 units along x, rotated 90 degrees about z;
    // grandchild flagged to rotate relative to the root.
    std::vector<MdlBone> bones(3);
    const float s = std::sqrt(0.5f);
    bones[0].parent = -1;
    bones[0].quaternion = {0, 0, 0, 1};
    bones[1].parent = 0;
    bones[1].position = {10, 0, 0};
    bones[1].quaternion = {0, 0, s, s};
    bones[2].parent = 1;
    bones[2].position = {5, 0, 0};
    bones[2].quaternion = {0, 0, 0, 1};
    for (auto& b : bones) {
        b.pose_to_bone = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
    }

    auto world = mdl_world_transforms(bones, mdl_bind_pose(bones));
    // The child's rotation turns the grandchild's offset onto +y.
    CHECK(world[2][0][3] == doctest::Approx(10.0f));
    CHECK(world[2][1][3] == doctest::Approx(5.0f));
    CHECK(world[2][0][0] == doctest::Approx(0.0f).epsilon(1e-6)); // inherits the 90 degree turn

    bones[2].flags = bone_flags::RotationRelativeToRoot;
    world = mdl_world_transforms(bones, mdl_bind_pose(bones));
    CHECK(world[2][1][3] == doctest::Approx(5.0f)); // position still follows the parent
    CHECK(world[2][0][0] == doctest::Approx(1.0f)); // rotation ignores the parent's turn

    const auto skin = mdl_skinning_matrices(bones, world);
    CHECK(skin[1][0][3] == doctest::Approx(10.0f));
    CHECK_THROWS_AS(mdl_world_transforms(bones, {}), FormatError);
}

TEST_CASE("MDL rejects malformed files") {
    auto data = build_mdl(MdlVertexFormat::Float);
    SUBCASE("signature") {
        data[0] = std::byte{'X'};
    }
    SUBCASE("version") {
        data[4] = std::byte{0x25};
    }
    SUBCASE("length field") {
        data.push_back(std::byte{0});
    }
    SUBCASE("vertex format") {
        data[0x1a8 + 160 + 16 + 0x9c] = std::byte{9};
    }
    SUBCASE("vertex count beyond file") {
        data[0x1a8 + 160 + 16 + 0x90] = std::byte{0xff};
    }
    SUBCASE("truncated") {
        data.resize(0x100);
    }
    CHECK_THROWS_AS(parse_mdl(data), FormatError);
}

TEST_CASE("VTX strip groups") {
    for (std::uint8_t flags : {std::uint8_t{0x12}, std::uint8_t{0x0a}}) {
        CAPTURE(int{flags});
        const auto vtx = parse_vtx(build_vtx(flags));
        CHECK(vtx.checksum == 0x1234);
        const auto& mesh = vtx.body_parts.at(0).models.at(0).lods.at(0).meshes.at(0);
        CHECK(mesh.strip_groups.at(0).vertex_refs == std::vector<std::uint16_t>{2, 0, 1});
        CHECK(mesh.triangles() == std::vector<std::uint32_t>{2, 0, 1});
    }
}

TEST_CASE("VTX rejects malformed files") {
    auto data = build_vtx(0x12);
    SUBCASE("version") {
        data[0] = std::byte{7};
    }
    SUBCASE("index out of range") {
        data[data.size() - 2] = std::byte{9};
    }
    SUBCASE("body part offset") {
        data[0x20] = std::byte{0xff};
        data[0x21] = std::byte{0xff};
    }
    CHECK_THROWS_AS(parse_vtx(data), FormatError);
}

TEST_CASE("MDL integration: every model in the real install loads") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t models = 0, vtx_files = 0, triangles = 0, vertices = 0, outside_hull = 0;
    std::size_t animations = 0, sequences = 0, samples = 0, non_unit = 0, missing_includes = 0, broken_sequences = 0;
    std::map<MdlVertexFormat, std::size_t> formats;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".mdl")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        MdlFile mdl;
        REQUIRE_NOTHROW(mdl = parse_mdl(*data));
        ++models;
        animations += mdl.animations.size();
        sequences += mdl.sequences.size();
        for (const auto& s : mdl.sequences) {
            broken_sequences += s.animation < 0;
        }
        for (const auto& inc : mdl.include_models) {
            missing_includes += !gfs.exists(inc);
        }
        for (std::size_t a = 0; a < mdl.animations.size(); ++a) {
            const std::uint32_t frames = std::max(1u, mdl.animations[a].frame_count);
            for (std::uint32_t frame : {0u, frames / 2, frames - 1}) {
                std::vector<MdlBonePose> pose;
                REQUIRE_NOTHROW(pose = mdl.sample_animation(a, frame));
                for (const auto& p : pose) {
                    const auto& q = p.rotation;
                    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                    non_unit += std::abs(n - 1.0f) > 0.02f;
                    ++samples;
                }
            }
        }
        for (const auto& part : mdl.body_parts) {
            for (const auto& m : part.models) {
                ++formats[m.vertex_format];
                vertices += m.vertices.size();
                for (const auto& v : m.vertices) {
                    bool inside = true;
                    for (int k = 0; k < 3; ++k) {
                        inside = inside && v.position[k] >= mdl.hull_min[k] - 0.5f && v.position[k] <= mdl.hull_max[k] + 0.5f;
                    }
                    outside_hull += !inside;
                }
            }
        }
        const std::string base = path.substr(0, path.size() - 4);
        for (const char* suffix : {".dx80.vtx", ".dx7_2bone.vtx"}) {
            const auto vtx_data = gfs.read(base + suffix);
            if (!vtx_data) {
                continue;
            }
            INFO(suffix);
            VtxFile vtx;
            REQUIRE_NOTHROW(vtx = parse_vtx(*vtx_data));
            ++vtx_files;
            CHECK(vtx.checksum == mdl.checksum);
            REQUIRE(vtx.body_parts.size() == mdl.body_parts.size());
            for (std::size_t b = 0; b < mdl.body_parts.size(); ++b) {
                REQUIRE(vtx.body_parts[b].models.size() == mdl.body_parts[b].models.size());
                for (std::size_t m = 0; m < mdl.body_parts[b].models.size(); ++m) {
                    const auto& model = mdl.body_parts[b].models[m];
                    for (const auto& lod : vtx.body_parts[b].models[m].lods) {
                        REQUIRE(lod.meshes.size() == model.meshes.size());
                        for (std::size_t me = 0; me < model.meshes.size(); ++me) {
                            const auto tris = lod.meshes[me].triangles();
                            bool in_range = true;
                            for (auto t : tris) {
                                in_range = in_range && t < model.meshes[me].vertex_count;
                            }
                            CHECK(in_range);
                            if (suffix[3] == '8') {
                                triangles += tris.size() / 3;
                            }
                        }
                    }
                }
            }
        }
    }
    MESSAGE(models << " models, " << vtx_files << " vtx files, " << vertices << " vertices, " << triangles
                   << " dx80 triangles (all LODs); formats float/q16/q8 = " << formats[MdlVertexFormat::Float] << "/"
                   << formats[MdlVertexFormat::Quantized16] << "/" << formats[MdlVertexFormat::Quantized8] << "; "
                   << outside_hull << " vertices outside the header hull");
    MESSAGE(animations << " animations, " << sequences << " sequences, " << samples << " bone samples ("
                       << non_unit << " non-unit rotations), " << missing_includes << " missing include models, "
                       << broken_sequences << " sequences with broken animation references");
    // Models compiled with their animations stripped but sequences kept
    // (throwtaxi, woodbox/lid, drknobantique*_static): 8 in the Steam release.
    CHECK(broken_sequences <= 8);
    CHECK(non_unit == 0);
    CHECK(models > 4000);
    CHECK(vtx_files >= 2 * models - 1);
}
