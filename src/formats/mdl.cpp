#include "openbl/formats/mdl.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

#include <algorithm>
#include <cmath>
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
constexpr std::size_t kHitboxSetsOffset = 0x100;
constexpr std::size_t kAttachmentsOffset = 0x148;
constexpr std::size_t kFlexDescsOffset = 0x158;
constexpr std::size_t kFlexControllersOffset = 0x160;
constexpr std::size_t kFlexRulesOffset = 0x168;
constexpr std::size_t kIkChainsOffset = 0x170;
constexpr std::size_t kMouthsOffset = 0x178;
constexpr std::size_t kPoseParametersOffset = 0x180;
constexpr std::size_t kSurfacePropOffset = 0x188;
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
constexpr std::size_t kPoseParameterSize = 20;
constexpr std::size_t kHitboxSetSize = 12;
constexpr std::size_t kHitboxSize = 32;
constexpr std::size_t kAttachmentSize = 60;
constexpr std::size_t kFlexControllerSize = 20;
constexpr std::size_t kFlexRuleSize = 12;
constexpr std::size_t kFlexOpSize = 8;
constexpr std::size_t kIkChainSize = 16;
constexpr std::size_t kIkLinkSize = 28;
constexpr std::size_t kMouthSize = 20;
constexpr std::size_t kMeshFlexSize = 32;
constexpr std::size_t kClothHeaderSize = 92;
constexpr std::size_t kClothCapsuleSize = 36;
constexpr std::size_t kPackedFlexVertexSize = 8; // Model flex format 2.
constexpr std::size_t kFloatFlexVertexSize = 20; // Model flex format 0.
constexpr float kFlexPositionUnit = 1.0f / 32.0f;
constexpr float kFlexNormalUnit = 1.0f / 128.0f;
constexpr std::size_t kMovementSize = 44;
constexpr std::size_t kEventSize = 76;
constexpr std::size_t kMeleeSweepSize = 24;
constexpr std::size_t kMeleeHitSize = 188;
constexpr std::int32_t kBlendGridSize = 16; // Animation indices: i16[16][16] at sequence +56.

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
        parse_rig(mdl);
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
            const std::uint32_t movements = count(at + 16, "movement");
            const std::size_t movement_base = rel(at, i32(at + 20));
            need(movement_base, std::uint64_t{movements} * kMovementSize);
            for (std::uint32_t m = 0; m < movements; ++m) {
                const std::size_t mv = movement_base + std::size_t{m} * kMovementSize;
                MdlMovement piece;
                piece.end_frame = i32(mv);
                piece.flags = i32(mv + 4);
                piece.start_speed = f32(mv + 8);
                piece.end_speed = f32(mv + 12);
                piece.yaw = f32(mv + 16);
                piece.direction = vec3(mv + 20);
                piece.displacement = vec3(mv + 32);
                a.movements.push_back(piece);
            }
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
            auto animation_index = [&](std::int32_t i, std::int32_t j) {
                const auto a = static_cast<std::int16_t>(u16(at + 56 + 2 * std::size_t(i * kBlendGridSize + j)));
                // Broken references exist in the shipped data (e.g. throwtaxi.mdl).
                return a < 0 || static_cast<std::uint32_t>(a) >= anims ? -1 : std::int32_t{a};
            };
            s.animation = animation_index(0, 0);
            for (int k = 0; k < 2; ++k) {
                s.grid[k] = i32(at + 572 + 4 * std::size_t(k));
                if (s.grid[k] < 1 || s.grid[k] > kBlendGridSize) {
                    in_.fail("sequence blend grid out of range");
                }
                s.blend_param[k] = i32(at + 580 + 4 * std::size_t(k));
                s.blend_start[k] = f32(at + 588 + 4 * std::size_t(k));
                s.blend_end[k] = f32(at + 596 + 4 * std::size_t(k));
            }
            for (std::int32_t row = 0; row < s.grid[0]; ++row) {
                for (std::int32_t col = 0; col < s.grid[1]; ++col) {
                    s.blend_animations.push_back(animation_index(row, col));
                }
            }
            s.fade_in = f32(at + 612);
            s.fade_out = f32(at + 616);

            const std::uint32_t events = count(at + 20, "event");
            const std::size_t event_base = rel(at, i32(at + 24));
            need(event_base, std::uint64_t{events} * kEventSize);
            for (std::uint32_t e = 0; e < events; ++e) {
                const std::size_t ev = event_base + std::size_t{e} * kEventSize;
                s.events.push_back({f32(ev), i32(ev + 4), i32(ev + 8), fixed_string(ev + 12, 64)});
            }

            const std::uint32_t sweep = count(at + 700, "melee sweep point");
            const std::size_t sweep_base = rel(at, i32(at + 704));
            need(sweep_base, std::uint64_t{sweep} * kMeleeSweepSize);
            for (std::uint32_t k = 0; k < sweep; ++k) {
                std::array<float, 6> pts{};
                for (std::size_t c = 0; c < 6; ++c) {
                    pts[c] = f32(sweep_base + std::size_t{k} * kMeleeSweepSize + 4 * c);
                }
                s.melee_sweep.push_back(pts);
            }
            const std::uint32_t hits = count(at + 708, "melee hit");
            const std::size_t hit_base = rel(at, i32(at + 712));
            need(hit_base, std::uint64_t{hits} * kMeleeHitSize);
            for (std::uint32_t k = 0; k < hits; ++k) {
                const std::size_t h = hit_base + std::size_t{k} * kMeleeHitSize;
                MdlMeleeHit hit;
                hit.start_cycle = f32(h);
                hit.end_cycle = f32(h + 4);
                hit.bone = i32(h + 8);
                hit.value = f32(h + 12);
                for (std::size_t c = 0; c < hit.params.size(); ++c) {
                    hit.params[c] = f32(h + 16 + 4 * c);
                }
                // Four 16-byte reaction slots, each starting with a record-relative name.
                for (std::size_t r = 0; r < hit.reactions.size(); ++r) {
                    const std::int32_t name = i32(h + 120 + 16 * r);
                    if (name != 0) {
                        hit.reactions[r] = c_string(rel(h, name));
                    }
                }
                s.melee_hits.push_back(std::move(hit));
            }
            mdl.sequences.push_back(std::move(s));
        }

        const std::uint32_t poses = count(kPoseParametersOffset, "pose parameter");
        const std::size_t pose_base = u32(kPoseParametersOffset + 4);
        need(pose_base, std::uint64_t{poses} * kPoseParameterSize);
        for (std::uint32_t i = 0; i < poses; ++i) {
            const std::size_t at = pose_base + std::size_t{i} * kPoseParameterSize;
            mdl.pose_parameters.push_back(
                {c_string(rel(at, i32(at))), i32(at + 4), f32(at + 8), f32(at + 12), f32(at + 16)});
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

    // Hitboxes, attachments, flex controllers and rules, mouths, IK chains, surface property.
    void parse_rig(MdlFile& mdl) {
        const auto bones = static_cast<std::int32_t>(mdl.bones.size());
        auto check_bone = [&](std::int32_t bone) {
            if (bone < 0 || bone >= bones) {
                in_.fail("bone index out of range");
            }
            return bone;
        };

        if (const std::uint32_t prop = u32(kSurfacePropOffset); prop != 0) {
            mdl.surface_prop = c_string(prop);
        }

        const std::uint32_t sets = count(kHitboxSetsOffset, "hitbox set");
        const std::size_t set_base = u32(kHitboxSetsOffset + 4);
        need(set_base, std::uint64_t{sets} * kHitboxSetSize);
        for (std::uint32_t i = 0; i < sets; ++i) {
            const std::size_t at = set_base + std::size_t{i} * kHitboxSetSize;
            MdlHitboxSet set;
            set.name = c_string(rel(at, i32(at)));
            const std::uint32_t n = count(at + 4, "hitbox");
            const std::size_t base = rel(at, i32(at + 8));
            need(base, std::uint64_t{n} * kHitboxSize);
            for (std::uint32_t k = 0; k < n; ++k) {
                const std::size_t h = base + std::size_t{k} * kHitboxSize;
                set.hitboxes.push_back({check_bone(i32(h)), i32(h + 4), vec3(h + 8), vec3(h + 20)});
            }
            mdl.hitbox_sets.push_back(std::move(set));
        }

        const std::uint32_t attachments = count(kAttachmentsOffset, "attachment");
        const std::size_t attachment_base = u32(kAttachmentsOffset + 4);
        need(attachment_base, std::uint64_t{attachments} * kAttachmentSize);
        for (std::uint32_t i = 0; i < attachments; ++i) {
            const std::size_t at = attachment_base + std::size_t{i} * kAttachmentSize;
            MdlAttachment a;
            a.name = c_string(rel(at, i32(at)));
            a.flags = i32(at + 4);
            a.bone = check_bone(i32(at + 8));
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 4; ++c) {
                    a.local[r][c] = f32(at + 12 + 16 * r + 4 * c);
                }
            }
            mdl.attachments.push_back(std::move(a));
        }

        const std::uint32_t flex_names = count(kFlexDescsOffset, "flex");
        const std::size_t flex_base = u32(kFlexDescsOffset + 4);
        need(flex_base, std::uint64_t{flex_names} * 4);
        for (std::uint32_t i = 0; i < flex_names; ++i) {
            const std::size_t at = flex_base + 4 * std::size_t{i};
            mdl.flex_names.push_back(c_string(rel(at, i32(at))));
        }

        const std::uint32_t controllers = count(kFlexControllersOffset, "flex controller");
        const std::size_t controller_base = u32(kFlexControllersOffset + 4);
        need(controller_base, std::uint64_t{controllers} * kFlexControllerSize);
        for (std::uint32_t i = 0; i < controllers; ++i) {
            const std::size_t at = controller_base + std::size_t{i} * kFlexControllerSize;
            mdl.flex_controllers.push_back(
                {c_string(rel(at, i32(at))), c_string(rel(at, i32(at + 4))), f32(at + 12), f32(at + 16)});
        }

        const std::uint32_t rules = count(kFlexRulesOffset, "flex rule");
        const std::size_t rule_base = u32(kFlexRulesOffset + 4);
        need(rule_base, std::uint64_t{rules} * kFlexRuleSize);
        for (std::uint32_t i = 0; i < rules; ++i) {
            const std::size_t at = rule_base + std::size_t{i} * kFlexRuleSize;
            MdlFlexRule rule;
            rule.flex = i32(at);
            if (rule.flex < 0 || static_cast<std::uint32_t>(rule.flex) >= flex_names) {
                in_.fail("flex rule target out of range");
            }
            const std::uint32_t ops = count(at + 4, "flex op");
            const std::size_t op_base = rel(at, i32(at + 8));
            need(op_base, std::uint64_t{ops} * kFlexOpSize);
            int depth = 0;
            for (std::uint32_t k = 0; k < ops; ++k) {
                const std::size_t o = op_base + std::size_t{k} * kFlexOpSize;
                MdlFlexOp op;
                op.kind = i32(o);
                switch (op.kind) {
                case MdlFlexOp::Const:
                    op.value = f32(o + 4);
                    ++depth;
                    break;
                case MdlFlexOp::Controller:
                case MdlFlexOp::Flex:
                    op.index = i32(o + 4);
                    if (op.index < 0 || static_cast<std::uint32_t>(op.index) >=
                                            (op.kind == MdlFlexOp::Controller ? controllers : flex_names)) {
                        in_.fail("flex op index out of range");
                    }
                    ++depth;
                    break;
                case MdlFlexOp::Add:
                case MdlFlexOp::Sub:
                case MdlFlexOp::Mul:
                case MdlFlexOp::Div:
                    // The value field of operator ops holds leftover bytes; ignore it.
                    if (--depth < 1) {
                        in_.fail("flex rule stack underflow");
                    }
                    break;
                default:
                    in_.fail("unknown flex op " + std::to_string(op.kind));
                }
                rule.ops.push_back(op);
            }
            if (depth != 1) {
                in_.fail("flex rule leaves " + std::to_string(depth) + " values on the stack");
            }
            mdl.flex_rules.push_back(std::move(rule));
        }

        const std::uint32_t mouths = count(kMouthsOffset, "mouth");
        const std::size_t mouth_base = u32(kMouthsOffset + 4);
        need(mouth_base, std::uint64_t{mouths} * kMouthSize);
        for (std::uint32_t i = 0; i < mouths; ++i) {
            const std::size_t at = mouth_base + std::size_t{i} * kMouthSize;
            mdl.mouths.push_back({check_bone(i32(at)), vec3(at + 4), i32(at + 16)});
        }

        const std::uint32_t chains = count(kIkChainsOffset, "IK chain");
        const std::size_t chain_base = u32(kIkChainsOffset + 4);
        need(chain_base, std::uint64_t{chains} * kIkChainSize);
        for (std::uint32_t i = 0; i < chains; ++i) {
            const std::size_t at = chain_base + std::size_t{i} * kIkChainSize;
            MdlIkChain chain;
            chain.name = c_string(rel(at, i32(at)));
            chain.link_type = i32(at + 4);
            const std::uint32_t links = count(at + 8, "IK link");
            const std::size_t link_base = rel(at, i32(at + 12));
            need(link_base, std::uint64_t{links} * kIkLinkSize);
            for (std::uint32_t k = 0; k < links; ++k) {
                const std::size_t l = link_base + std::size_t{k} * kIkLinkSize;
                chain.links.push_back({check_bone(i32(l)), vec3(l + 4), vec3(l + 16)});
            }
            mdl.ik_chains.push_back(std::move(chain));
        }
    }

    // A 92-byte cloth header: f32 parameter, then counts and offsets (relative
    // to the header) of seven arrays. See docs/formats/models.md.
    MdlCloth parse_cloth(std::size_t body, std::size_t slot, std::uint32_t vertex_count) {
        need(body, kClothHeaderSize);
        auto field = [&](std::size_t k) { return u32(body + 4 * k); };
        auto array = [&](std::size_t k, std::uint64_t n, std::uint64_t size) {
            const std::size_t start = body + field(k);
            need(start, n * size);
            return start;
        };
        MdlCloth cloth;
        cloth.slot = static_cast<std::uint32_t>(slot);
        cloth.parameter = f32(body);
        const std::uint32_t particles = field(1);
        cloth.attached_particles = field(2);
        cloth.primary_constraints = field(6);
        if (particles > kMaxCount || field(2) + field(3) != particles || field(4) != kClothHeaderSize ||
            field(6) + field(7) != field(5)) {
            in_.fail("malformed cloth header");
        }
        auto particle = [&](std::size_t at) {
            const std::uint16_t p = u16(at);
            if (p >= particles) {
                in_.fail("cloth particle index out of range");
            }
            return p;
        };

        const std::size_t p_at = array(4, particles, 2);
        for (std::uint32_t i = 0; i < particles; ++i) {
            cloth.particles.push_back(u16(p_at + 2 * std::size_t{i}));
            if (cloth.particles.back() >= vertex_count) {
                in_.fail("cloth particle vertex out of range");
            }
        }
        const std::uint32_t constraints = count(body + 4 * 5, "cloth constraint");
        const std::size_t c_at = array(8, constraints, 16);
        for (std::uint32_t i = 0; i < constraints; ++i) {
            const std::size_t r = c_at + 16 * std::size_t{i};
            cloth.constraints.push_back({particle(r), particle(r + 2), f32(r + 4), f32(r + 8), f32(r + 12)});
        }
        cloth.primary_bytes = field(9);
        const std::uint64_t byte_count = std::uint64_t{field(9)} + field(10);
        const std::size_t b_at = array(11, byte_count, 1);
        for (std::uint64_t i = 0; i < byte_count; ++i) {
            cloth.bytes.push_back(u8(b_at + i));
        }
        const std::uint32_t triangles = count(body + 4 * 12, "cloth triangle");
        const std::size_t t_at = array(13, triangles, 6);
        for (std::uint32_t i = 0; i < triangles; ++i) {
            const std::size_t r = t_at + 6 * std::size_t{i};
            cloth.triangles.push_back({particle(r), particle(r + 2), particle(r + 4)});
        }
        const std::uint32_t edges = count(body + 4 * 14, "cloth edge");
        cloth.primary_edges = field(15);
        const std::size_t e_at = array(16, edges, 4);
        for (std::uint32_t i = 0; i < edges; ++i) {
            const std::size_t r = e_at + 4 * std::size_t{i};
            cloth.edges.push_back({particle(r), particle(r + 2)});
        }
        const std::uint32_t edge_triangles = count(body + 4 * 17, "cloth edge triangle");
        const std::size_t et_at = array(18, edge_triangles, 10);
        for (std::uint32_t i = 0; i < edge_triangles; ++i) {
            const std::size_t r = et_at + 10 * std::size_t{i};
            if (u16(r) >= edges || u16(r + 2) >= edges) {
                in_.fail("cloth edge index out of range");
            }
            cloth.edge_triangles.push_back({u16(r), u16(r + 2), particle(r + 4), particle(r + 6), particle(r + 8)});
        }
        const std::uint32_t vertex_data = count(body + 4 * 19, "cloth vertex");
        const std::size_t v_at = array(20, vertex_data, 12);
        for (std::uint32_t i = 0; i < vertex_data; ++i) {
            cloth.vertex_data.push_back(vec3(v_at + 12 * std::size_t{i}));
        }
        return cloth;
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
        const std::int32_t flex_format = i32(at + 0xc0);
        if (flex_format != 0 && flex_format != 2) {
            in_.fail("unknown flex vertex format " + std::to_string(flex_format));
        }
        const std::size_t flex_stride = flex_format == 2 ? kPackedFlexVertexSize : kFloatFlexVertexSize;
        const auto& directions = mdl_normal_table(64);
        // A packed direction is a normal-table code (12 x index) times a length.
        auto direction = [&](std::size_t code_at, float length) {
            const std::uint16_t code = u16(code_at);
            if (code % 12 != 0 || code / 12u >= directions.size()) {
                in_.fail("flex direction code out of range");
            }
            const auto& d = directions[code / 12u];
            return std::array<float, 3>{d[0] * length, d[1] * length, d[2] * length};
        };

        need(mesh_base, std::uint64_t{mesh_count} * kMeshSize);
        std::uint64_t mesh_vertices = 0;
        for (std::uint32_t i = 0; i < mesh_count; ++i) {
            const std::size_t m = mesh_base + std::size_t{i} * kMeshSize;
            MdlMesh mesh;
            mesh.material = i32(m);
            mesh.vertex_count = u32(m + 8);
            mesh.vertex_offset = u32(m + 12);
            mesh.material_type = i32(m + 24);
            mesh.material_param = i32(m + 28);
            const std::uint32_t flexes = count(m + 16, "mesh flex");
            const std::size_t flex_base = flexes ? rel(m, i32(m + 20)) : 0;
            need(flex_base, std::uint64_t{flexes} * kMeshFlexSize);
            for (std::uint32_t f = 0; f < flexes; ++f) {
                const std::size_t fx = flex_base + std::size_t{f} * kMeshFlexSize;
                MdlMeshFlex flex;
                flex.flex = i32(fx);
                for (std::size_t k = 0; k < 4; ++k) {
                    flex.targets[k] = f32(fx + 4 + 4 * k);
                }
                const std::uint32_t verts = count(fx + 20, "flex vertex");
                const std::size_t vert_base = rel(fx, i32(fx + 24));
                flex.pair = i32(fx + 28);
                need(vert_base, std::uint64_t{verts} * flex_stride);
                flex.vertices.resize(verts);
                for (std::uint32_t v = 0; v < verts; ++v) {
                    const std::size_t at_v = vert_base + std::size_t{v} * flex_stride;
                    MdlFlexVertex& out = flex.vertices[v];
                    if (flex_format == 2) {
                        // u16 index, u16 position direction, u16 normal direction,
                        // u8 position length (1/32 inch), u8 normal length (1/128).
                        out.index = u16(at_v);
                        out.delta = direction(at_v + 2, static_cast<float>(u8(at_v + 6)) * kFlexPositionUnit);
                        out.normal_delta = direction(at_v + 4, static_cast<float>(u8(at_v + 7)) * kFlexNormalUnit);
                    } else {
                        // u32 index, f32 delta[3], u16 normal direction, u8 normal length, u8 pad.
                        out.index = u32(at_v);
                        out.delta = vec3(at_v + 4);
                        out.normal_delta = direction(at_v + 16, static_cast<float>(u8(at_v + 18)) * kFlexNormalUnit);
                    }
                    if (out.index >= mesh.vertex_count) {
                        in_.fail("flex vertex index exceeds the mesh's vertex count");
                    }
                }
                mesh.flexes.push_back(std::move(flex));
            }
            if (const std::int32_t piece_offset = i32(m + 48); piece_offset != 0) {
                const std::size_t pieces = rel(m, piece_offset);
                const std::size_t particles = rel(m, i32(m + 52));
                const std::size_t slots = rel(m, i32(m + 56));
                need(pieces, mesh.vertex_count);
                need(particles, std::uint64_t{mesh.vertex_count} * 2);
                need(slots, std::uint64_t{mesh.vertex_count} * 2);
                for (std::uint32_t v = 0; v < mesh.vertex_count; ++v) {
                    mesh.cloth_piece.push_back(u8(pieces + v));
                    mesh.cloth_particle.push_back(u16(particles + 2 * std::size_t{v}));
                    mesh.cloth_slot.push_back(u16(slots + 2 * std::size_t{v}));
                }
            }
            if (std::uint64_t{mesh.vertex_offset} + mesh.vertex_count > vertex_count) {
                in_.fail("mesh vertices exceed model vertex count");
            }
            mesh_vertices += mesh.vertex_count;
            model.meshes.push_back(mesh);
        }
        if (mesh_vertices != vertex_count) {
            in_.fail("mesh vertex counts do not add up to the model's");
        }

        // Cloth: a table of model-relative pointers, one per slot (pieces and
        // LODs; 0 = empty), ending where the first body starts.
        model.cloth_pieces = i32(at + 0xc8);
        if (model.cloth_pieces > 0) {
            const std::size_t table = rel(at, i32(at + 0xcc));
            const std::size_t first = rel(at, i32(table));
            if (first <= table || (first - table) % 4 != 0 || first - table > 64) {
                in_.fail("malformed cloth table");
            }
            for (std::size_t s = 0; s < (first - table) / 4; ++s) {
                if (const std::int32_t body = i32(table + 4 * s); body != 0) {
                    model.cloth.push_back(parse_cloth(rel(at, body), s, vertex_count));
                }
            }
        }
        const std::uint32_t capsules = count(at + 0xd0, "cloth capsule");
        const std::size_t capsule_base = capsules ? rel(at, i32(at + 0xd4)) : 0;
        need(capsule_base, std::uint64_t{capsules} * kClothCapsuleSize);
        for (std::uint32_t c = 0; c < capsules; ++c) {
            // i32 bone twice (always equal), f32 radius, f32 start[3], f32 end[3].
            const std::size_t r = capsule_base + std::size_t{c} * kClothCapsuleSize;
            const std::int32_t bone = i32(r);
            if (bone < 0 || static_cast<std::size_t>(bone) >= bone_count) {
                in_.fail("cloth capsule bone out of range");
            }
            model.cloth_capsules.push_back({bone, f32(r + 8), vec3(r + 12), vec3(r + 24)});
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
                if (const auto& table = mdl_normal_table(64);
                    out.packed_normal % 12 == 0 && out.packed_normal / 12u < table.size()) {
                    out.normal = table[out.packed_normal / 12u];
                    out.has_normal = true;
                }
                out.uv = {static_cast<float>(u16(v + 8)) / 65535.0f, static_cast<float>(u16(v + 10)) / 65535.0f};
                break;
            case MdlVertexFormat::Quantized8:
                for (std::size_t k = 0; k < 3; ++k) {
                    out.position[k] = origin[k] + scale[k] * (static_cast<float>(u8(v + k)) / 127.5f - 1.0f);
                }
                out.packed_normal = u8(v + 3);
                if (const auto& table = mdl_normal_table(12); out.packed_normal < table.size()) {
                    out.normal = table[out.packed_normal];
                    out.has_normal = true;
                }
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

const std::vector<std::array<float, 3>>& mdl_normal_table(int steps) {
    // Recovered from the shipped models by averaging each code's geometric
    // normal over every quantized vertex (docs/formats/models.md).
    auto build = [](int n) {
        std::vector<std::array<float, 3>> table{{0.0f, 0.0f, -1.0f}};
        const double pi = 3.14159265358979323846;
        for (int j = 1; j < n; ++j) {
            const double theta = j * pi / n;
            const double s = std::sin(theta);
            const int count = 4 * static_cast<int>(std::ceil(std::round(2.0 * n * s) / 4.0));
            for (int i = 0; i < count; ++i) {
                const double phi = 2.0 * pi * i / count;
                table.push_back({static_cast<float>(s * std::cos(phi)), static_cast<float>(s * std::sin(phi)),
                                 static_cast<float>(-std::cos(theta))});
            }
        }
        table.push_back({0.0f, 0.0f, 1.0f});
        return table;
    };
    static const auto table64 = build(64);
    static const auto table12 = build(12);
    static const std::vector<std::array<float, 3>> none;
    return steps == 64 ? table64 : steps == 12 ? table12 : none;
}

MdlFile parse_mdl(std::span<const std::byte> data) {
    return Parser(data).run();
}

} // namespace openbl::formats
