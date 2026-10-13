#pragma once

// Draws studio-model props placed in a map: static props from the game lump
// and prop_* entities. Each model is loaded once; every placement is drawn
// with its own transform, skin and light tint.

#include "openbl/render/entity_state.hpp"
#include "openbl/formats/bsp.hpp"
#include "openbl/formats/visibility.hpp"
#include "openbl/formats/draw_order.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/prop_lighting.hpp"
#include "openbl/render/translucent.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <optional>
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
    std::size_t fading = 0;             // Static props with a distance fade.
    std::size_t styled_lighting = 0;    // Lit by lights with a style, rebuilt when it changes.
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
    bool bumpmaps = true; // Light and reflect $bumpmap materials about the bumped normal.
    // What this view can see (docs/RENDERING.md#visibility); null: everything.
    const formats::ViewVisibility* visibility = nullptr;
};

class PropRenderer {
public:
    // `shown`: targetnames of StartHidden prop entities to draw anyway.
    PropRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                 const formats::BspFile& bsp, const std::vector<std::string>& shown = {});
    ~PropRenderer();

    // The static props' distance fade for this frame. The original computes
    // it once per frame from the main camera and uses it in every view (the
    // 3D skybox, reflections and monitors included).
    void update_fades(const std::array<float, 3>& eye);
    // This frame's light styles (LightStyles::values): props lit by styled
    // lights choose their lights again when one changes, as the original does.
    void update_lighting(const std::array<float, LightStyles::kCount>& styles);
    // What the simulation says of each map entity (render/entity_state.hpp;
    // hidden, skin), indexed like BspFile::entities. Until this is
    // called, entities with StartHidden are hidden unless named in `shown`.
    void set_entity_states(const std::vector<EntityState>& states);

    // Props that are fading are left out here and drawn whole as translucent items.
    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const PropDrawSettings& settings);

    // The translucent pass in the original's order (render/translucent.hpp):
    // one item per prop with something translucent to draw (translucent
    // meshes, decals, or the whole prop while it fades).
    void collect_translucent(const formats::BspDrawOrder& order, const PropDrawSettings& settings,
                             std::uint32_t source, std::vector<TranslucentItem>& out) const;
    void begin_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                           const PropDrawSettings& settings);
    // `rebind`: another renderer drew since this one last did.
    void draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index, bool rebind);

    const PropStats& stats() const { return stats_; }

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
        std::array<float, 16> map_transform{}; // Its pose in the map (transform follows the simulation).
        ModelLighting lighting;            // Model space.
        std::unique_ptr<StyledLighting> styled; // Set when styled lights reach it.
        std::int32_t entity = -1;          // Its map entity (prop_* entities); -1: a static prop.
        bool hidden = false;
        std::vector<Decal> decals;
        bool skybox = false;               // In the 3D skybox's area.
        bool envmapped = false;            // Some mesh of its skin has an environment map.
        bool has_translucent = false;      // Some mesh of its skin is translucent.
        const rhi::Texture* cubemap = nullptr; // Nearest cubemap, for "env_cubemap" materials.
        std::optional<formats::BspStaticProp> fade; // Static props with a distance fade.
        std::vector<std::uint32_t> leaves;  // World leaves its bounds touch.
        formats::Vec3 mins{}, maxs{};       // Its world-space box.
        float alpha = 1.0f;                 // This frame's fade: 0 hidden, 1 solid.
    };

    const Model* load_model(const std::string& path);
    void place_decals(const formats::BspFile& bsp);
    // Fills shader_data_ and instance_blocks_ for `settings`; returns the material params rows held.
    std::size_t build_shader_data(const PropDrawSettings& settings);
    struct Bound {
        const rhi::Pipeline* pipeline = nullptr;
        const Model* model = nullptr;
    };
    // Draws one instance's opaque or translucent part (a fading instance's
    // translucent part is all of it); `data`: shader data to bind, or null to place shader_data_.
    void draw_instance(rhi::CommandList& cmd, const std::array<float, 16>& view_projection, const PropDrawSettings& settings,
                       std::size_t rows, std::size_t index, bool translucent, Bound& bound,
                       const rhi::ShaderDataRef* data);
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
    std::vector<std::uint32_t> instance_blocks_; // Per instance, this pass: its block, or UINT32_MAX.
    // The translucent pass under way (begin_translucent).
    struct TranslucentPass {
        std::array<float, 16> view_projection{};
        PropDrawSettings settings;
        std::size_t rows = 1;
        rhi::ShaderDataRef data;
        Bound bound;
    } pass_;
};

} // namespace openbl::render
