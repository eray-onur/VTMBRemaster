#include "openbl/formats/mdl.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

#include <algorithm>
#include <limits>

namespace openbl::formats {

namespace {

// Header layout (offsets from the start of the file).
constexpr std::size_t kNameOffset = 0x0c;
constexpr std::size_t kNameSize = 128;
constexpr std::size_t kLengthOffset = 0x8c;
constexpr std::size_t kHullOffset = 0xb4;
constexpr std::size_t kFlagsOffset = 0xe4;
constexpr std::size_t kBonesOffset = 0xf0;
constexpr std::size_t kAnimationsOffset = 0x108;
constexpr std::size_t kSequencesOffset = 0x110;
constexpr std::size_t kIncludeModelsOffset = 0x194;
constexpr std::size_t kTexturesOffset = 0x124;
constexpr std::size_t kTextureDirsOffset = 0x12c;
constexpr std::size_t kSkinsOffset = 0x134;
constexpr std::size_t kBodyPartsOffset = 0x140;
constexpr std::size_t kHeaderSize = 0x1a8;

constexpr std::size_t kBoneSize = 160;
constexpr std::size_t kTextureSize = 20;
constexpr std::size_t kBodyPartSize = 16;
constexpr std::size_t kModelSize = 224;
constexpr std::size_t kMeshSize = 60;
constexpr std::size_t kAnimationSize = 72;
constexpr std::size_t kSequenceSize = 764;
constexpr std::size_t kIncludeModelSize = 116;

// Sanity limits; the Steam release stays far below these.
constexpr std::uint32_t kMaxCount = 1 << 20;

class Parser {
public:
    explicit Parser(std::span<const std::byte> data) : data_(data), in_(data, "MDL") {}

    MdlFile run() {
        MdlFile mdl;
        if (data_.size() < kHeaderSize) {
            in_.fail("file too small for header");
        }
        const auto magic = in_.bytes(4);
        if (magic[0] != std::byte{'I'} || magic[1] != std::byte{'D'} || magic[2] != std::byte{'S'} ||
            magic[3] != std::byte{'T'}) {
            in_.fail("missing IDST signature");
        }
        mdl.version = i32(4);
        if (mdl.version != kMdlVersion) {
            in_.fail("unsupported version " + std::to_string(mdl.version));
        }
        mdl.checksum = i32(8);
        mdl.name = fixed_string(kNameOffset, kNameSize);
        if (u32(kLengthOffset) != data_.size()) {
            in_.fail("length field does not match file size");
        }
        mdl.hull_min = vec3(kHullOffset);
        mdl.hull_max = vec3(kHullOffset + 12);
        mdl.flags = u32(kFlagsOffset);

        parse_bones(mdl);
        parse_textures(mdl);
        parse_skins(mdl);
        parse_body_parts(mdl);
        parse_animations(mdl);
        return mdl;
    }

private:
    std::int32_t i32(std::size_t at) {
        in_.seek(at);
        return in_.i32();
    }
    std::uint32_t u32(std::size_t at) {
        in_.seek(at);
        return in_.u32();
    }
    std::uint16_t u16(std::size_t at) {
        in_.seek(at);
        return in_.u16();
    }
    std::uint8_t u8(std::size_t at) {
        in_.seek(at);
        return in_.u8();
    }
    float f32(std::size_t at) {
        in_.seek(at);
        return in_.f32();
    }
    std::array<float, 3> vec3(std::size_t at) { return {f32(at), f32(at + 4), f32(at + 8)}; }

    std::uint32_t count(std::size_t at, const char* what) {
        const std::uint32_t n = u32(at);
        if (n > kMaxCount) {
            in_.fail(std::string("implausible ") + what + " count " + std::to_string(n));
        }
        return n;
    }

    // Base offset plus a signed relative offset, bounds-checked against the file.
    std::size_t rel(std::size_t base, std::int32_t offset) {
        const auto target = static_cast<std::int64_t>(base) + offset;
        if (target < 0 || static_cast<std::uint64_t>(target) > data_.size()) {
            in_.fail("offset out of range");
        }
        return static_cast<std::size_t>(target);
    }

    void need(std::size_t at, std::uint64_t bytes) {
        if (at > data_.size() || bytes > data_.size() - at) {
            in_.fail("data out of range at offset " + std::to_string(at));
        }
    }

    std::string c_string(std::size_t at) {
        if (at >= data_.size()) {
            in_.fail("string offset out of range");
        }
        std::string out;
        for (std::size_t i = at; i < data_.size() && data_[i] != std::byte{0}; ++i) {
            out.push_back(static_cast<char>(data_[i]));
        }
        return out;
    }

    std::string fixed_string(std::size_t at, std::size_t size) {
        need(at, size);
        std::string out;
        for (std::size_t i = at; i < at + size && data_[i] != std::byte{0}; ++i) {
            out.push_back(static_cast<char>(data_[i]));
        }
        return out;
    }

    void parse_bones(MdlFile& mdl) {
        const std::uint32_t n = count(kBonesOffset, "bone");
        const std::size_t base = u32(kBonesOffset + 4);
        need(base, std::uint64_t{n} * kBoneSize);
        mdl.bones.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::size_t at = base + std::size_t{i} * kBoneSize;
            MdlBone& bone = mdl.bones[i];
            bone.name = c_string(rel(at, i32(at)));
            bone.parent = i32(at + 4);
            bone.flags = i32(at + 0x88);
            if (bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(i)) {
                in_.fail("bone " + std::to_string(i) + " has invalid parent " + std::to_string(bone.parent));
            }
            bone.position = vec3(at + 0x20);
            for (std::size_t k = 0; k < 4; ++k) {
                bone.quaternion[k] = f32(at + 0x2c + 4 * k);
                bone.rotation_scale[k] = f32(at + 0x48 + 4 * k);
            }
            bone.position_scale = vec3(at + 0x3c);
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 4; ++c) {
                    bone.pose_to_bone[r][c] = f32(at + 0x58 + 16 * r + 4 * c);
                }
            }
        }
    }

    void parse_textures(MdlFile& mdl) {
        const std::uint32_t n = count(kTexturesOffset, "texture");
        const std::size_t base = u32(kTexturesOffset + 4);
        need(base, std::uint64_t{n} * kTextureSize);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::size_t at = base + std::size_t{i} * kTextureSize;
            mdl.textures.push_back(c_string(rel(at, i32(at))));
        }
        const std::uint32_t dirs = count(kTextureDirsOffset, "texture directory");
        const std::size_t dir_base = u32(kTextureDirsOffset + 4);
        need(dir_base, std::uint64_t{dirs} * 4);
        for (std::uint32_t i = 0; i < dirs; ++i) {
            mdl.texture_dirs.push_back(c_string(u32(dir_base + 4 * std::size_t{i})));
        }
    }

    void parse_skins(MdlFile& mdl) {
        const std::uint32_t refs = count(kSkinsOffset, "skin reference");
        const std::uint32_t families = count(kSkinsOffset + 4, "skin family");
        const std::size_t base = u32(kSkinsOffset + 8);
        need(base, std::uint64_t{refs} * families * 2);
        mdl.skin_families.resize(families);
        for (std::uint32_t f = 0; f < families; ++f) {
            for (std::uint32_t r = 0; r < refs; ++r) {
                mdl.skin_families[f].push_back(
                    static_cast<std::int16_t>(u16(base + (std::size_t{f} * refs + r) * 2)));
            }
        }
    }

    void parse_body_parts(MdlFile& mdl) {
        const std::uint32_t n = count(kBodyPartsOffset, "body part");
        const std::size_t base = u32(kBodyPartsOffset + 4);
        need(base, std::uint64_t{n} * kBodyPartSize);
        for (std::uint32_t b = 0; b < n; ++b) {
            const std::size_t at = base + std::size_t{b} * kBodyPartSize;
            MdlBodyPart part;
            part.name = c_string(rel(at, i32(at)));
            const std::uint32_t models = count(at + 4, "model");
            part.base = i32(at + 8);
            const std::size_t model_base = rel(at, i32(at + 12));
            need(model_base, std::uint64_t{models} * kModelSize);
            for (std::uint32_t m = 0; m < models; ++m) {
                part.models.push_back(parse_model(model_base + std::size_t{m} * kModelSize, mdl.bones.size()));
            }
            mdl.body_parts.push_back(std::move(part));
        }
    }

    void parse_animations(MdlFile& mdl) {
        const std::uint32_t anims = count(kAnimationsOffset, "animation");
        const std::size_t anim_base = u32(kAnimationsOffset + 4);
        need(anim_base, std::uint64_t{anims} * kAnimationSize);
        for (std::uint32_t i = 0; i < anims; ++i) {
            const std::size_t at = anim_base + std::size_t{i} * kAnimationSize;
            MdlAnimation a;
            a.name = c_string(rel(at, i32(at)));
            a.fps = f32(at + 4);
            a.flags = i32(at + 8);
            const std::int32_t frames = i32(at + 12);
            if (frames < 0) {
                in_.fail("negative frame count");
            }
            a.frame_count = static_cast<std::uint32_t>(frames);
            a.data_offset = rel(at, i32(at + 48));
            need(a.data_offset, std::uint64_t{32} * mdl.bones.size());
            mdl.animations.push_back(std::move(a));
        }

        const std::uint32_t seqs = count(kSequencesOffset, "sequence");
        const std::size_t seq_base = u32(kSequencesOffset + 4);
        need(seq_base, std::uint64_t{seqs} * kSequenceSize);
        for (std::uint32_t i = 0; i < seqs; ++i) {
            const std::size_t at = seq_base + std::size_t{i} * kSequenceSize;
            MdlSequence s;
            s.label = c_string(rel(at, i32(at)));
            s.activity_name = c_string(rel(at, i32(at + 4)));
            s.flags = i32(at + 8);
            s.activity = i32(at + 12);
            s.activity_weight = i32(at + 16);
            s.blend_count = i32(at + 52);
            // 16-bit animation indices, one per blend; the first is the base pose.
            s.animation = static_cast<std::int16_t>(u16(at + 56));
            if (s.animation < 0 || static_cast<std::uint32_t>(s.animation) >= anims) {
                s.animation = -1; // Broken reference in the shipped data (e.g. throwtaxi.mdl).
            }
            mdl.sequences.push_back(std::move(s));
        }

        const std::uint32_t includes = count(kIncludeModelsOffset, "include model");
        const std::size_t include_base = u32(kIncludeModelsOffset + 4);
        need(include_base, std::uint64_t{includes} * kIncludeModelSize);
        for (std::uint32_t i = 0; i < includes; ++i) {
            const std::size_t at = include_base + std::size_t{i} * kIncludeModelSize;
            mdl.include_models.push_back(normalize_game_path(c_string(rel(at, i32(at)))));
        }
        if (anims > 0) {
            mdl.data = std::make_shared<const std::vector<std::byte>>(data_.begin(), data_.end());
        }
    }

    MdlModel parse_model(std::size_t at, std::size_t bone_count) {
        MdlModel model;
        model.name = fixed_string(at, 128);
        const std::uint32_t mesh_count = count(at + 0x88, "mesh");
        const std::size_t mesh_base = rel(at, i32(at + 0x8c));
        const std::uint32_t vertex_count = count(at + 0x90, "vertex");
        const std::size_t vertex_base = rel(at, i32(at + 0x94));
        const std::int32_t format = i32(at + 0x9c);
        if (format < 0 || format > 2) {
            in_.fail("unknown vertex format " + std::to_string(format));
        }
        model.vertex_format = static_cast<MdlVertexFormat>(format);
        const auto origin = vec3(at + 0xa0);
        const auto scale = vec3(at + 0xac);

        need(mesh_base, std::uint64_t{mesh_count} * kMeshSize);
        std::uint64_t mesh_vertices = 0;
        for (std::uint32_t i = 0; i < mesh_count; ++i) {
            const std::size_t m = mesh_base + std::size_t{i} * kMeshSize;
            MdlMesh mesh;
            mesh.material = i32(m);
            mesh.vertex_count = u32(m + 8);
            mesh.vertex_offset = u32(m + 12);
            if (std::uint64_t{mesh.vertex_offset} + mesh.vertex_count > vertex_count) {
                in_.fail("mesh vertices exceed model vertex count");
            }
            mesh_vertices += mesh.vertex_count;
            model.meshes.push_back(mesh);
        }
        if (mesh_vertices != vertex_count) {
            in_.fail("mesh vertex counts do not add up to the model's");
        }

        static constexpr std::size_t kStride[] = {44, 12, 8};
        const std::size_t stride = kStride[format];
        need(vertex_base, std::uint64_t{vertex_count} * stride);
        model.vertices.resize(vertex_count);
        for (std::uint32_t i = 0; i < vertex_count; ++i) {
            const std::size_t v = vertex_base + std::size_t{i} * stride;
            MdlVertex& out = model.vertices[i];
            switch (model.vertex_format) {
            case MdlVertexFormat::Float:
                for (std::size_t k = 0; k < 3; ++k) {
                    out.weights[k] = u8(v + k);
                    out.bones[k] = u16(v + 4 + 2 * k);
                    if (out.bones[k] >= bone_count && out.weights[k] != 0) {
                        in_.fail("vertex references missing bone " + std::to_string(out.bones[k]));
                    }
                }
                out.position = vec3(v + 12);
                out.normal = vec3(v + 24);
                out.uv = {f32(v + 36), f32(v + 40)};
                out.has_normal = true;
                break;
            case MdlVertexFormat::Quantized16:
                for (std::size_t k = 0; k < 3; ++k) {
                    out.position[k] = origin[k] + static_cast<float>(u16(v + 2 * k)) * scale[k];
                }
                out.packed_normal = u16(v + 6);
                out.uv = {static_cast<float>(u16(v + 8)) / 65535.0f, static_cast<float>(u16(v + 10)) / 65535.0f};
                break;
            case MdlVertexFormat::Quantized8:
                for (std::size_t k = 0; k < 3; ++k) {
                    out.position[k] = origin[k] + scale[k] * (static_cast<float>(u8(v + k)) / 127.5f - 1.0f);
                }
                out.packed_normal = u8(v + 3);
                out.uv = {static_cast<float>(u16(v + 4)) / 65535.0f, static_cast<float>(u16(v + 6)) / 65535.0f};
                break;
            }
        }
        return model;
    }

    std::span<const std::byte> data_;
    detail::BinaryReader in_;
};

} // namespace

std::int32_t MdlFile::texture_for(std::int32_t material, std::size_t family) const {
    if (family >= skin_families.size() || material < 0 ||
        static_cast<std::size_t>(material) >= skin_families[family].size()) {
        return -1;
    }
    const std::int32_t texture = skin_families[family][static_cast<std::size_t>(material)];
    return texture >= 0 && static_cast<std::size_t>(texture) < textures.size() ? texture : -1;
}

namespace {

// Run-length encoded animation channel: entries are 16-bit; a header entry
// holds (valid, total) bytes, followed by `valid` values covering `total`
// frames (the last value repeats for the remaining frames of the run).
std::int16_t channel_value(std::span<const std::byte> data, std::size_t at, std::uint32_t frame) {
    std::uint32_t k = frame;
    for (int guard = 0; guard < 65536; ++guard) {
        if (at + 2 > data.size()) {
            throw FormatError("MDL: animation channel out of range");
        }
        const auto valid = static_cast<std::uint32_t>(data[at]);
        const auto total = static_cast<std::uint32_t>(data[at + 1]);
        if (total == 0) {
            throw FormatError("MDL: empty animation run");
        }
        if (total > k) {
            const std::uint32_t index = k < valid ? k : valid - 1;
            if (valid == 0) {
                throw FormatError("MDL: animation run without values");
            }
            const std::size_t value_at = at + 2 * (std::size_t{index} + 1);
            if (value_at + 2 > data.size()) {
                throw FormatError("MDL: animation value out of range");
            }
            return static_cast<std::int16_t>(detail::load_u16(data.data() + value_at));
        }
        k -= total;
        at += 2 * (std::size_t{valid} + 1);
    }
    throw FormatError("MDL: animation channel does not end");
}

} // namespace

std::vector<MdlBonePose> MdlFile::sample_animation(std::size_t animation, std::uint32_t frame) const {
    if (animation >= animations.size() || !data) {
        throw FormatError("MDL: animation index out of range");
    }
    const MdlAnimation& anim = animations[animation];
    if (anim.frame_count > 0) {
        frame = std::min(frame, anim.frame_count - 1);
    }
    const std::span<const std::byte> bytes(*data);
    std::vector<MdlBonePose> pose(bones.size());
    for (std::size_t b = 0; b < bones.size(); ++b) {
        const MdlBone& bone = bones[b];
        MdlBonePose& out = pose[b];
        out.position = bone.position;
        out.rotation = bone.quaternion;
        // Per-bone record: float (always 1.0), then 7 channel offsets relative
        // to the record (position x, y, z, rotation x, y, z, w); 0 = default.
        const std::size_t record = anim.data_offset + 32 * b;
        if (record + 32 > bytes.size()) {
            throw FormatError("MDL: animation record out of range");
        }
        for (std::size_t c = 0; c < 7; ++c) {
            const std::uint32_t offset = detail::load_u32(bytes.data() + record + 4 + 4 * c);
            if (offset == 0) {
                continue;
            }
            const float v = static_cast<float>(channel_value(bytes, record + offset, frame));
            if (c < 3) {
                // Positions are offsets from the bind position; rotations are absolute.
                out.position[c] = bone.position[c] + v * bone.position_scale[c];
            } else {
                out.rotation[c - 3] = v * bone.rotation_scale[c - 3];
            }
        }
    }
    return pose;
}

namespace {

using Quat = std::array<float, 4>;

Quat quat_mul(const Quat& a, const Quat& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

MdlMatrix matrix_from(const Quat& q, const std::array<float, 3>& t) {
    const auto [x, y, z, w] = q;
    return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0]},
             {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t[1]},
             {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t[2]}}};
}

MdlMatrix matrix_mul(const MdlMatrix& a, const MdlMatrix& b) {
    MdlMatrix r{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.0f);
        }
    }
    return r;
}

std::array<float, 3> transform_point(const MdlMatrix& m, const std::array<float, 3>& p) {
    return {m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
            m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
            m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3]};
}

} // namespace

std::vector<MdlBonePose> mdl_bind_pose(const std::vector<MdlBone>& bones) {
    std::vector<MdlBonePose> pose(bones.size());
    for (std::size_t b = 0; b < bones.size(); ++b) {
        pose[b] = {bones[b].position, bones[b].quaternion};
    }
    return pose;
}

std::vector<MdlMatrix> mdl_world_transforms(const std::vector<MdlBone>& bones, const std::vector<MdlBonePose>& pose) {
    if (pose.size() != bones.size()) {
        throw FormatError("MDL: pose does not match the skeleton");
    }
    std::vector<MdlMatrix> world(bones.size());
    std::vector<Quat> world_rotation(bones.size());
    std::vector<std::size_t> top(bones.size());
    for (std::size_t b = 0; b < bones.size(); ++b) {
        const std::int32_t parent = bones[b].parent; // Always lower than b (checked when parsing).
        if (parent < 0) {
            top[b] = b;
            world_rotation[b] = pose[b].rotation;
            world[b] = matrix_from(pose[b].rotation, pose[b].position);
            continue;
        }
        const auto p = static_cast<std::size_t>(parent);
        top[b] = top[p];
        const std::size_t rotation_parent = (bones[b].flags & bone_flags::RotationRelativeToRoot) ? top[b] : p;
        world_rotation[b] = quat_mul(world_rotation[rotation_parent], pose[b].rotation);
        world[b] = matrix_from(world_rotation[b], transform_point(world[p], pose[b].position));
    }
    return world;
}

std::vector<MdlMatrix> mdl_skinning_matrices(const std::vector<MdlBone>& bones, const std::vector<MdlMatrix>& world) {
    std::vector<MdlMatrix> out(bones.size());
    for (std::size_t b = 0; b < bones.size() && b < world.size(); ++b) {
        out[b] = matrix_mul(world[b], bones[b].pose_to_bone);
    }
    return out;
}

MdlFile parse_mdl(std::span<const std::byte> data) {
    return Parser(data).run();
}

} // namespace openbl::formats
