#pragma once

// Animated characters placed in a map (npc_* entities): GPU-skinned studio
// models playing a looping sequence, with animations found through the
// model's include chain (e.g. models/character/shared/male/*.mdl).

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/prop_lighting.hpp"
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
};

struct CharacterDrawSettings {
    float light_scale = 1.0f; // As for the world (docs/RENDERING.md).
    bool fullbright = false;
    // Material params table (MaterialSystem::params()); null: defaults only.
    const std::vector<MaterialParams>* material_params = nullptr;
};

class CharacterRenderer {
public:
    CharacterRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                      const formats::BspFile& bsp);
    ~CharacterRenderer();

    // Advances every character's animation to `time` (seconds) and computes its skinning matrices.
    void update(float time);
    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const CharacterDrawSettings& settings);
    void draw_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                          const CharacterDrawSettings& settings);

    const CharacterStats& stats() const { return stats_; }

private:
    struct Mesh {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        const RenderMaterial* material = nullptr;
        std::unique_ptr<rhi::BindGroup> bind_group;
        std::vector<std::unique_ptr<rhi::BindGroup>> frame_bind_groups; // Animated base texture.
    };
    struct Model {
        std::shared_ptr<const formats::MdlFile> mdl;
        std::unique_ptr<rhi::Buffer> vertices;
        std::unique_ptr<rhi::Buffer> indices;
        std::vector<std::vector<Mesh>> skins;
    };
    struct Instance {
        const Model* model = nullptr;
        std::size_t skin = 0;
        std::array<float, 16> transform{};
        AmbientCube lighting;
        // Playing sequence; null file = bind pose.
        std::shared_ptr<const formats::MdlFile> anim_file;
        std::size_t animation = 0;
        std::vector<int> bone_map; // Model bone -> animation file bone, or -1.
        float phase = 0;           // Seconds, so crowds do not move in lockstep.
        std::vector<formats::MdlMatrix> skinning; // Updated by update().
    };

    const Model* load_model(const std::string& path);
    void draw_pass(rhi::CommandList& cmd, const std::array<float, 16>& vp, const CharacterDrawSettings& settings,
                   bool translucent);

    rhi::Device& device_;
    MaterialSystem& materials_;
    const AssetSource& assets_;
    ModelLibrary library_;
    CharacterStats stats_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::array<std::array<std::unique_ptr<rhi::Pipeline>, 2>, 3> pipelines_; // [blend][cull]
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;
    std::vector<Instance> instances_;
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
