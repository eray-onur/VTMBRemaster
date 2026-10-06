#pragma once

// Bloodlines studio models (.mdl, version 2531). Geometry lives in the .mdl;
// triangle lists live in the matching .vtx (see vtx.hpp). See docs/formats/models.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openbl::formats {

constexpr std::int32_t kMdlVersion = 2531;

namespace bone_flags {
// The bone's animated rotation is relative to the top of its hierarchy (e.g.
// "Bip01"), not to its parent. Set on every "BipNN Spine1" bone; verified on
// the male stances (median 7 degrees from bind vs 120 degrees in parent space).
constexpr std::int32_t RotationRelativeToRoot = 0x2;
} // namespace bone_flags

using MdlMatrix = std::array<std::array<float, 4>, 3>; // 3x4 row-major affine transform.

struct MdlBone {
    std::string name;
    std::int32_t parent = -1;
    std::int32_t flags = 0;
    std::array<float, 3> position{};
    std::array<float, 4> quaternion{}; // x, y, z, w
    std::array<float, 3> position_scale{}; // Animation value -> units.
    std::array<float, 4> rotation_scale{}; // Animation value -> quaternion component, per component.
    std::array<std::array<float, 4>, 3> pose_to_bone{}; // Inverse bind pose, 3x4 row-major.
};

// Local transform of one bone (relative to its parent).
struct MdlBonePose {
    std::array<float, 3> position{};
    std::array<float, 4> rotation{}; // Quaternion x, y, z, w.
};

struct MdlAnimation {
    std::string name;
    float fps = 30.0f;
    std::int32_t flags = 0;
    std::uint32_t frame_count = 0;
    std::size_t data_offset = 0; // Absolute offset of the per-bone channel records.
};

namespace sequence_flags {
constexpr std::int32_t Looping = 0x1;
}

struct MdlSequence {
    std::string label;
    std::string activity_name;
    std::int32_t flags = 0;
    std::int32_t activity = -1;
    std::int32_t activity_weight = 0;
    std::int32_t blend_count = 1;
    // First animation played (16-bit index in the file), or -1 if the shipped
    // data references an animation that does not exist. Further blends are not
    // decoded yet.
    std::int32_t animation = 0;
};

enum class MdlVertexFormat : std::int32_t {
    Float = 0,       // 44 bytes: bone weights, float position/normal/uv.
    Quantized16 = 1, // 12 bytes: 16-bit position over the model bounds, packed normal, 16-bit uv.
    Quantized8 = 2,  // 8 bytes: 8-bit position around a center, normal index, 16-bit uv.
};

struct MdlVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{}; // Valid only if has_normal.
    std::array<float, 2> uv{};
    std::array<std::uint8_t, 3> weights{255, 0, 0}; // Sum to 255.
    std::array<std::uint16_t, 3> bones{};
    bool has_normal = false;
    // Raw packed normal for quantized formats; the decoding is not known yet
    // (see docs/formats/models.md). Renderers compute normals from geometry instead.
    std::uint16_t packed_normal = 0;
};

struct MdlMesh {
    std::int32_t material = 0; // Index into the skin table.
    std::uint32_t vertex_offset = 0; // Into MdlModel::vertices.
    std::uint32_t vertex_count = 0;
};

struct MdlModel {
    std::string name;
    MdlVertexFormat vertex_format = MdlVertexFormat::Float;
    std::vector<MdlVertex> vertices;
    std::vector<MdlMesh> meshes;
};

struct MdlBodyPart {
    std::string name;
    std::int32_t base = 0;
    std::vector<MdlModel> models;
};

struct MdlFile {
    std::int32_t version = 0;
    std::int32_t checksum = 0; // Matches the .vtx checksum.
    std::string name;
    std::array<float, 3> hull_min{};
    std::array<float, 3> hull_max{};
    std::uint32_t flags = 0;
    std::vector<MdlBone> bones;
    std::vector<std::string> textures;     // Material names without directory.
    std::vector<std::string> texture_dirs; // Directories searched under materials/.
    std::vector<std::vector<std::int16_t>> skin_families; // [family][skin ref] -> texture index
    std::vector<MdlBodyPart> body_parts;
    std::vector<MdlAnimation> animations;
    std::vector<MdlSequence> sequences;
    // Other model files whose sequences and animations this model uses
    // (e.g. models/character/shared/male/npc_allsequences.mdl). Normalized game paths.
    std::vector<std::string> include_models;

    // Texture index used by a mesh material in a skin family, or -1.
    std::int32_t texture_for(std::int32_t material, std::size_t family = 0) const;

    // Local pose of every bone at `frame` (clamped to the animation's range).
    // Position channels are offsets from the bind position; rotation channels
    // are absolute quaternion components. Missing channels keep the bind value.
    // Throws FormatError on corrupt animation data.
    std::vector<MdlBonePose> sample_animation(std::size_t animation, std::uint32_t frame) const;

    // Raw file bytes, kept when the model has animations (needed for sampling).
    std::shared_ptr<const std::vector<std::byte>> data;
};

// Throws FormatError.
MdlFile parse_mdl(std::span<const std::byte> data);

// Model-space transform of every bone for a local pose (one entry per bone),
// honouring bone_flags::RotationRelativeToRoot.
std::vector<MdlMatrix> mdl_world_transforms(const std::vector<MdlBone>& bones, const std::vector<MdlBonePose>& pose);

// Matrices that move bind-pose vertices into the posed model space:
// world transform * inverse bind pose, per bone.
std::vector<MdlMatrix> mdl_skinning_matrices(const std::vector<MdlBone>& bones, const std::vector<MdlMatrix>& world);

// The bones' bind pose as a local pose.
std::vector<MdlBonePose> mdl_bind_pose(const std::vector<MdlBone>& bones);

} // namespace openbl::formats
