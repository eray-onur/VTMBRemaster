#pragma once

// Draws studio-model props placed in a map: static props from the game lump
// and prop_* entities. Each model is loaded once; every placement is drawn
// with its own transform, skin and light tint.

#include "openbl/formats/bsp.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/prop_lighting.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openbl::render {

struct PropStats {
    std::size_t instances = 0;
    std::size_t models = 0;
    std::size_t missing_models = 0;
    std::size_t triangles = 0;          // Per model, LOD 0, summed over unique models.
    std::size_t generated_normals = 0;  // Models whose normals were computed from geometry.
    std::size_t decals = 0;             // infodecals placed on props (those the world could not place).
    std::size_t unplaced_decals = 0;    // infodecals on neither the world nor a prop.
};

struct PropDrawSettings {
    float light_scale = 1.0f; // As for the world (docs/RENDERING.md).
    bool fullbright = false;
    // Material params table (MaterialSystem::params()); null: defaults only.
    const std::vector<MaterialParams>* material_params = nullptr;
    Fog fog;
    bool skybox = false; // true: only props in the 3D skybox's area; false: the rest.
    std::array<float, 3> eye{}; // Camera position, for environment maps.
    bool envmaps = true;
};

class PropRenderer {
public:
    // `shown`: targetnames of StartHidden prop entities to draw anyway.
    PropRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                 const formats::BspFile& bsp, const std::vector<std::string>& shown = {});
    ~PropRenderer();

    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const PropDrawSettings& settings) {
        draw_pass(cmd, view_projection, settings, false);
    }
    void draw_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                          const PropDrawSettings& settings) {
        draw_pass(cmd, view_projection, settings, true);
    }

    const PropStats& stats() const { return stats_; }

    // True if `point` is closed in by a prop's surface: of six rays along the
    // axes, at least five hit the same prop within its size. Used to hide glow
    // sprites placed inside lamp globes, which the original's glow visibility
    // test hides.
    bool encloses(const std::array<float, 3>& point) const;

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
    struct Model {
        std::unique_ptr<rhi::Buffer> vertices;
        std::unique_ptr<rhi::Buffer> indices;
        std::vector<std::vector<Mesh>> skins; // [skin family][mesh]
        std::array<float, 3> center{};        // Bounds centre in model space.
        float radius = 0;                     // Bounding sphere around `center`.
        std::array<float, 3> half_extent{};   // Bounding box half sizes, model space.
        std::vector<formats::Vec3> positions; // LOD 0 triangles, for projecting decals.
        std::vector<std::uint32_t> triangle_indices;
    };
    // An infodecal projected onto one placed prop, in the prop's model space.
    struct Decal {
        std::unique_ptr<rhi::Buffer> vertices;
        std::unique_ptr<rhi::Buffer> indices;
        std::uint32_t index_count = 0;
        const RenderMaterial* material = nullptr;
        std::unique_ptr<rhi::BindGroup> bind_group;
    };
    struct Instance {
        const Model* model = nullptr;
        std::size_t skin = 0;
        std::array<float, 16> transform{}; // Column-major model matrix.
        AmbientCube lighting;              // Model space.
        std::vector<Decal> decals;
        bool skybox = false;               // In the 3D skybox's area.
        bool envmapped = false;            // Some mesh of its skin has an environment map.
        const rhi::Texture* cubemap = nullptr; // Nearest cubemap, for "env_cubemap" materials.
    };

    const Model* load_model(const std::string& path);
    void place_decals(const formats::BspFile& bsp);
    void draw_pass(rhi::CommandList& cmd, const std::array<float, 16>& vp, const PropDrawSettings& settings,
                   bool translucent);
    const rhi::Pipeline& pipeline_for(const RenderMaterial& m) const;

    rhi::Device& device_;
    MaterialSystem& materials_;
    const AssetSource& assets_;
    PropStats stats_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::array<std::array<std::unique_ptr<rhi::Pipeline>, 2>, 3> pipelines_; // [blend][cull]
    std::unordered_map<std::string, std::unique_ptr<Model>> models_; // Null caches failures.
    std::vector<Instance> instances_;
    std::vector<float> shader_data_; // Fog rows, eye, material params table, envmap blocks.
    std::vector<std::uint32_t> envmap_blocks_; // Per instance, this pass: its block, or UINT32_MAX.
};

} // namespace openbl::render
