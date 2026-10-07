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

namespace motion_flags {
constexpr std::int32_t X = 0x40;
constexpr std::int32_t Y = 0x80;
constexpr std::int32_t Z = 0x100;
constexpr std::int32_t Yaw = 0x800;
// 0x1000 is set on most pieces; meaning unknown.
} // namespace motion_flags

// A stretch of root motion: from the previous piece's end frame (or 0) to end_frame.
struct MdlMovement {
    std::int32_t end_frame = 0;
    std::int32_t flags = 0; // motion_flags.
    float start_speed = 0, end_speed = 0;
    float yaw = 0;                       // Degrees turned (with motion_flags::Yaw).
    std::array<float, 3> direction{};    // Unit vector.
    std::array<float, 3> displacement{}; // Total motion over the piece.
};

struct MdlAnimation {
    std::string name;
    float fps = 30.0f;
    std::int32_t flags = 0;
    std::uint32_t frame_count = 0;
    std::vector<MdlMovement> movements;
    std::size_t data_offset = 0; // Absolute offset of the per-bone channel records.
};

namespace sequence_flags {
constexpr std::int32_t Looping = 0x1;
}

struct MdlEvent {
    float cycle = 0;  // 0-1 through the sequence.
    std::int32_t id = 0; // Game-specific event code (see docs/formats/models.md).
    std::int32_t type = 0;
    std::string options;
};

// Bloodlines melee data: when a swing can hit, with which bone, and how the
// victim reacts. Layout decoded; gameplay meaning partly inferred.
struct MdlMeleeHit {
    float start_cycle = 0, end_cycle = 0;
    std::int32_t bone = -1;
    float value = 0;                       // Often 15; probably damage or reach.
    std::array<float, 5> params{};          // Following floats, meaning unknown.
    std::array<std::string, 4> reactions;   // Knockback activities, e.g. ACT_KNOCKBACK_NORMAL_HIGH_RIGHT.
};

struct MdlSequence {
    std::string label;
    std::string activity_name;
    std::int32_t flags = 0;
    std::int32_t activity = -1;
    std::int32_t activity_weight = 0;
    std::int32_t blend_count = 1;
    // First animation played (16-bit index in the file), or -1 if the shipped
    // data references an animation that does not exist.
    std::int32_t animation = 0;

    // Blend grid: grid[0] x grid[1] animations laid out [i][j], i along the
    // first pose parameter. Indices are -1 where the reference is broken.
    std::array<std::int32_t, 2> grid{1, 1};
    std::vector<std::int32_t> blend_animations;
    std::array<std::int32_t, 2> blend_param{-1, -1}; // Index into MdlFile::pose_parameters.
    std::array<float, 2> blend_start{}, blend_end{};
    float fade_in = 0.2f, fade_out = 0.2f;
    float third_fade = 0.2f; // Equals the fades except on two sledgehammer attacks; meaning unknown.
    std::vector<std::int32_t> layers; // Sequences overlaid on this one (e.g. "katana_bobble_layer").
    std::vector<MdlEvent> events;
    // Bloodlines melee combos (docs/formats/models.md).
    std::int32_t attack_directions = -1; // Mask: 8 forward, 512/1024 side swings, 16 jump; -1 none.
    std::string dodge_activity;          // e.g. ACT_DODGE_DUCK; empty if none.
    std::string blocked_activity;        // e.g. ACT_BLOCKED_REACTION_LEFT.
    std::string combo_next;              // Sequence label of the next attack in the combo.
    std::string follow_up;               // e.g. knockback_flying_wall_hit.
    std::array<float, 3> combo_window{0.0f, 1.0f, 1.0f}; // Cycles; e.g. 0.5, 0.9, 0.91 on combo attacks.
    std::vector<std::array<float, 6>> melee_sweep; // Pairs of points, probably the weapon's path.
    std::vector<MdlMeleeHit> melee_hits;

    std::int32_t blend(std::int32_t i, std::int32_t j) const {
        return blend_animations[static_cast<std::size_t>(i * grid[1] + j)];
    }
};

struct MdlPoseParameter {
    std::string name; // move_yaw, hit_yaw, aim_yaw, aim_pitch.
    std::int32_t flags = 0;
    float start = 0, end = 0;
    float wrap = 0; // 360 for looping parameters, else 0.
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
    bool has_normal = false; // False only if a quantized code is out of range.
    // Raw packed normal for quantized formats: an index into a normal table
    // (times 12 for Quantized16). See mdl_normal_table and docs/formats/models.md.
    std::uint16_t packed_normal = 0;
};

// The unit-normal table used by quantized vertices: latitude rings from -z to
// +z, `steps` latitude steps apart, ring j holding 4*ceil(round(2*steps*sin(t))/4)
// points from azimuth 0 counter-clockwise. 64 steps (5,314 normals) for
// Quantized16, 12 steps (202 normals) for Quantized8.
const std::vector<std::array<float, 3>>& mdl_normal_table(int steps);

// One vertex moved by a flex at full weight (see docs/formats/models.md).
struct MdlFlexVertex {
    std::uint32_t index = 0;                // Mesh-relative vertex index.
    std::array<float, 3> delta{};           // Position offset, model units.
    std::array<float, 3> normal_delta{};    // Added to the normal (renormalise after).
};

// A facial morph target on one mesh.
struct MdlMeshFlex {
    std::int32_t flex = 0;               // Index into MdlFile::flex_names.
    std::array<float, 4> targets{};      // Controller ramp points (meaning partly unknown).
    std::int32_t pair = 0;               // Left/right pairing flag (unverified).
    std::vector<MdlFlexVertex> vertices;
};

struct MdlMesh {
    std::int32_t material = 0; // Index into the skin table.
    std::uint32_t vertex_offset = 0; // Into MdlModel::vertices.
    std::uint32_t vertex_count = 0;
    std::int32_t material_type = 0; // 1 on 496 meshes (eyes and similar); meaning unverified.
    std::int32_t material_param = 0;
    std::vector<MdlMeshFlex> flexes;
    // Cloth binding, one entry per mesh vertex; empty if the mesh has no cloth.
    // cloth_piece is 255 for an ordinary skinned vertex, otherwise the index of
    // its cloth piece. For cloth vertices, cloth_particle & 0x7fff is the
    // particle the vertex follows (bit 15: meaning unknown) and cloth_slot is
    // the particle count plus its index into MdlCloth::vertex_data.
    std::vector<std::uint8_t> cloth_piece;
    std::vector<std::uint16_t> cloth_particle;
    std::vector<std::uint16_t> cloth_slot;
};

struct MdlHitbox {
    std::int32_t bone = 0;
    std::int32_t group = 0; // 0 generic, 1 head, 2 chest, 3 stomach, 4/5 left/right arm, 6/7 left/right leg.
    std::array<float, 3> min{}, max{}; // In the bone's space.
};

struct MdlHitboxSet {
    std::string name;
    std::vector<MdlHitbox> hitboxes;
};

struct MdlAttachment {
    std::string name; // e.g. "mouth", "eyes", "muzzleflash", "placementOrigin".
    std::int32_t flags = 0;
    std::int32_t bone = 0;
    MdlMatrix local{}; // Relative to the bone.
};

struct MdlFlexController {
    std::string type; // "eyelid", "mouth", "morph", ...
    std::string name; // e.g. "right_lid_raiser"; matches the keys of expression sets.
    float min = 0, max = 1;
};

// Computes a flex's weight from controller values with a small stack program.
struct MdlFlexOp {
    enum Kind : std::int32_t {
        Const = 1,      // push value
        Controller = 2, // push controller[index]
        Flex = 3,       // push the computed weight of flex[index]
        Add = 4,
        Sub = 5,
        Mul = 6,
        Div = 7, // Binary; division is assumed, unverified.
    };
    std::int32_t kind = Const;
    std::int32_t index = 0; // Controller or Flex.
    float value = 0;        // Const.
};

struct MdlFlexRule {
    std::int32_t flex = 0;
    std::vector<MdlFlexOp> ops;
};

struct MdlMouth {
    std::int32_t bone = 0;
    std::array<float, 3> forward{};
    std::int32_t flex = 0;
};

struct MdlIkLink {
    std::int32_t bone = 0;
    std::array<float, 3> a{}, b{}; // Zero in every shipped model.
};

struct MdlIkChain {
    std::string name; // rhand, lhand, rfoot, lfoot.
    std::int32_t link_type = 0;
    std::vector<MdlIkLink> links;
};

// A distance constraint between two cloth particles.
struct MdlClothConstraint {
    std::uint16_t a = 0, b = 0;      // Particle indices.
    float weight_a = 0.0f;           // 0 when a is attached; usually in [0, 1].
    float weight_b = 0.0f;           // Usually weight_a - 2.
    float rest_length_squared = 0.0f;
};

// One cloth piece at one LOD: a particle system over some of the model's
// vertices (docs/formats/models.md). Meanings marked "unknown" are laid out
// but not yet tied to behaviour.
struct MdlCloth {
    std::uint32_t slot = 0;          // Position in the model's cloth table.
    float parameter = 0.0f;          // 0.3 to 8; meaning unknown.
    std::vector<std::uint16_t> particles; // Model vertex index of each particle.
    std::uint32_t attached_particles = 0; // The first N follow the skeleton; the rest simulate.
    std::vector<MdlClothConstraint> constraints;
    std::uint32_t primary_constraints = 0; // Size of the first group.
    std::vector<std::uint8_t> bytes;       // Unknown, values 1 to 4.
    std::uint32_t primary_bytes = 0;
    std::vector<std::array<std::uint16_t, 3>> triangles; // Particle indices.
    std::vector<std::array<std::uint16_t, 2>> edges;     // Particle indices.
    std::uint32_t primary_edges = 0;
    // Two edge indices, then three particle indices.
    std::vector<std::array<std::uint16_t, 5>> edge_triangles;
    std::vector<std::array<float, 3>> vertex_data; // One per cloth mesh vertex; meaning unknown.
};

// A capsule the cloth collides with, in model space.
struct MdlClothCapsule {
    std::int32_t bone = 0;
    float radius = 0.0f;
    std::array<float, 3> start{}, end{};
};

struct MdlModel {
    std::string name;
    MdlVertexFormat vertex_format = MdlVertexFormat::Float;
    std::vector<MdlVertex> vertices;
    std::vector<MdlMesh> meshes;
    std::int32_t cloth_pieces = 0;
    std::vector<MdlCloth> cloth;              // Non-empty table slots.
    std::vector<MdlClothCapsule> cloth_capsules;
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
    std::vector<MdlPoseParameter> pose_parameters;
    std::string surface_prop; // "metal", "wood", "flesh", ... or empty.
    std::vector<MdlHitboxSet> hitbox_sets;
    std::vector<MdlAttachment> attachments;
    std::vector<std::string> flex_names;
    std::vector<MdlFlexController> flex_controllers;
    std::vector<MdlFlexRule> flex_rules;
    std::vector<MdlMouth> mouths;
    std::vector<MdlIkChain> ik_chains;
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
