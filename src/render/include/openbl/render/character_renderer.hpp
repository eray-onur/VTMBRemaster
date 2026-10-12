#pragma once

// Animated characters placed in a map (npc_* entities): GPU-skinned studio
// models playing a looping sequence, with animations found through the
// model's include chain (e.g. models/character/shared/male/*.mdl).

#include "openbl/anim/animation.hpp"
#include "openbl/anim/cloth.hpp"
#include "openbl/formats/bsp.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/prop_lighting.hpp"
#include "openbl/formats/draw_order.hpp"
#include "openbl/render/translucent.hpp"
#include "openbl/rhi/rhi.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openbl::render {

// Loads and caches parsed model files.
class ModelLibrary {
public:
    explicit ModelLibrary(const AssetSource& assets) : assets_(assets) {}

    // Null if the file is missing or malformed.
    std::shared_ptr<const formats::MdlFile> load(const std::string& path);

    struct SequenceRef {
        std::shared_ptr<const formats::MdlFile> file; // File holding the sequence and its animation.
        std::size_t sequence = 0;
    };
    // Searches `model_path` and its include models (depth first) for the first
    // playable sequence accepted by `accept`.
    template <typename Fn> std::optional<SequenceRef> find_sequence(const std::string& model_path, Fn&& accept);

private:
    const AssetSource& assets_;
    std::unordered_map<std::string, std::shared_ptr<const formats::MdlFile>> files_;
};

struct CharacterStats {
    std::size_t instances = 0;
    std::size_t models = 0;
    std::size_t missing_models = 0;
    std::size_t without_sequence = 0; // Shown in their bind pose.
    std::size_t with_cloth = 0;       // Instances simulating cloth (hair, skirts, coats).
    std::size_t with_envmap = 0;      // Instances with an environment-mapped material.
};

struct CharacterOptions {
    // Sequence every character plays, by label or activity (case-insensitive);
    // empty: the idle (first ACT_IDLE sequence, else one named like an idle).
    std::string sequence;
    // Pose parameter values, e.g. {"move_yaw", 90} (others stay 0).
    std::vector<std::pair<std::string, float>> pose_parameters;
    // Targetnames of StartHidden NPCs to draw anyway (as if a script had shown them).
    std::vector<std::string> shown;
    // Facial flex controller values by name, e.g. {"right_lid_raiser", 1}; others stay 0.
    std::vector<std::pair<std::string, float>> flex_controllers;
    // Simulate cloth (hair, skirts, coats); false: cloth vertices are skinned like the rest.
    bool cloth = true;
};

struct CharacterDrawSettings {
    float light_scale = 1.0f; // As for the world (docs/RENDERING.md).
    bool fullbright = false;
    // Material params table (MaterialSystem::params()); null: defaults only.
    const std::vector<MaterialParams>* material_params = nullptr;
    Fog fog;
    std::array<float, 3> eye{}; // Camera position, for environment maps.
    bool envmaps = true;
    bool bumpmaps = true; // Light and reflect $bumpmap materials about the bumped normal.
};

class CharacterRenderer {
public:
    CharacterRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                      const formats::BspFile& bsp, const CharacterOptions& options = {});
    ~CharacterRenderer();

    // Advances every character's animation to `time` (seconds) and computes its skinning matrices.
    void update(float time);
    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const CharacterDrawSettings& settings);
    // The translucent pass in the original's order (render/translucent.hpp):
    // one item per character with translucent meshes.
    void collect_translucent(const formats::BspDrawOrder& order, const CharacterDrawSettings& settings,
                             std::uint32_t source, std::vector<TranslucentItem>& out) const;
    void begin_translucent(const std::array<float, 16>& view_projection, const CharacterDrawSettings& settings);
    void draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index);

    const CharacterStats& stats() const { return stats_; }

private:
    struct Mesh {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        const RenderMaterial* material = nullptr;
        std::unique_ptr<rhi::BindGroup> bind_group;
        std::vector<std::unique_ptr<rhi::BindGroup>> frame_bind_groups; // Animated base texture.
        // For "env_cubemap" materials: one bind group per cubemap an instance uses.
        mutable std::unordered_map<const rhi::Texture*, std::unique_ptr<rhi::BindGroup>> cubemap_groups;
    };
    // One mesh flex: a morph of some vertices, by flex slot.
    struct ModelFlex {
        std::int32_t flex = 0; // Index into MdlFile::flex_names.
        std::array<float, 4> targets{};
        std::vector<std::pair<std::uint32_t, std::array<float, 3>>> deltas; // Slot, position offset.
    };
    // A cloth piece at LOD 0, with what is needed to skin its particles on the CPU.
    struct ClothPiece {
        const formats::MdlCloth* cloth = nullptr; // In Model::mdl.
        std::uint32_t first_slot = 0;              // Its particles' first index in the instance's cloth data.
        std::vector<formats::MdlVertex> particles; // Bind-pose vertex of each particle.
    };
    struct ClothCapsule {
        std::int32_t bone = 0;
        anim::ClothCapsule bind; // Bind pose, model space.
    };
    struct Model {
        std::shared_ptr<const formats::MdlFile> mdl;
        std::vector<ModelFlex> flexes;
        std::uint32_t flex_slots = 0; // Vertices any flex moves; each has a slot.
        std::vector<ClothPiece> cloth;
        std::uint32_t cloth_particles = 0; // Over all pieces.
        std::vector<ClothCapsule> cloth_capsules;
        std::unique_ptr<rhi::Buffer> vertices;
        std::unique_ptr<rhi::Buffer> indices;
        std::vector<std::vector<Mesh>> skins;
    };
    struct Instance {
        const Model* model = nullptr;
        std::size_t skin = 0;
        std::array<float, 16> transform{};
        AmbientCube lighting;
        std::optional<anim::AnimationPlayer> player; // Empty: bind pose.
        std::vector<float> flex_deltas; // float4 per flex slot; empty when no flex is active.
        std::vector<formats::MdlMatrix> skinning;    // Updated by update().
        std::vector<anim::ClothSimulation> cloth;    // One per Model::cloth piece.
        std::vector<float> cloth_data; // Positions, then normals: float4 per particle; updated by update().
        std::optional<float> cloth_time; // Time of the last cloth update.
        bool envmapped = false;                // Some mesh of its skin has an environment map.
        bool has_translucent = false;          // Some mesh of its skin is translucent.
        const rhi::Texture* cubemap = nullptr; // Nearest cubemap, for "env_cubemap" materials.
        std::vector<std::uint32_t> leaves;     // World leaves around it.
    };

    const Model* load_model(const std::string& path);
    void update_cloth(Instance& inst, float dt);
    void draw_pass(rhi::CommandList& cmd, const std::array<float, 16>& vp, const CharacterDrawSettings& settings,
                   bool translucent);
    // Draws one character's opaque or translucent meshes (it places its own shader data).
    void draw_instance(rhi::CommandList& cmd, const std::array<float, 16>& vp, const CharacterDrawSettings& settings,
                       const Instance& inst, bool translucent);

    rhi::Device& device_;
    MaterialSystem& materials_;
    const AssetSource& assets_;
    ModelLibrary library_;
    CharacterStats stats_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::array<std::array<std::unique_ptr<rhi::Pipeline>, 2>, 3> pipelines_; // [blend][cull]
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;
    std::vector<Instance> instances_;
    // The translucent pass under way (begin_translucent).
    std::array<float, 16> pass_view_projection_{};
    CharacterDrawSettings pass_settings_;
};

template <typename Fn>
std::optional<ModelLibrary::SequenceRef> ModelLibrary::find_sequence(const std::string& model_path, Fn&& accept) {
    std::vector<std::string> stack{model_path};
    std::vector<std::string> seen;
    while (!stack.empty() && seen.size() < 64) {
        const std::string path = stack.back();
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), path) != seen.end()) {
            continue;
        }
        seen.push_back(path);
        const auto file = load(path);
        if (!file) {
            continue;
        }
        for (std::size_t s = 0; s < file->sequences.size(); ++s) {
            if (file->sequences[s].animation >= 0 && accept(file->sequences[s])) {
                return SequenceRef{file, s};
            }
        }
        for (auto it = file->include_models.rbegin(); it != file->include_models.rend(); ++it) {
            stack.push_back(*it);
        }
    }
    return std::nullopt;
}

} // namespace openbl::render
