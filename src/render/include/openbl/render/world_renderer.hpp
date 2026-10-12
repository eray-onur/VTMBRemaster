#pragma once

// Draws the brush geometry of a map: the world model and brush entities,
// textured and lit by the baked lightmaps, with every light style weighted
// by its current brightness (switchable lights in their default state,
// animated styles flickering).

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/draw_order.hpp"
#include "openbl/formats/visibility.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/light_styles.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/translucent.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openbl::render {

struct WorldStats {
    std::size_t faces = 0;
    std::size_t triangles = 0;
    std::size_t batches = 0;
    std::size_t skipped_faces = 0; // Tool, sky and not-yet-supported surfaces.
    std::size_t hidden_entities = 0; // Brush entities with StartHidden, not drawn.
    std::size_t monitors = 0;        // func_monitor screens drawing a camera view.
    std::size_t skybox_faces = 0;    // Faces in the 3D skybox's area.
    std::size_t water_planes = 0;    // Heights of water surfaces.
    std::size_t decals = 0;        // infodecals placed on brush faces.
    std::size_t unplaced_decals = 0;
    std::size_t missing_materials = 0;
    std::uint32_t lightmap_width = 0;
    std::uint32_t lightmap_height = 0;
};

struct WorldDrawSettings {
    // Lightmap scale in linear space. The original pixel shaders compute
    // base * texel * 2 in gamma space, and texel = luxel^(1/2.2) / 2, so the
    // result is base * luxel in linear terms (measured against the original;
    // see docs/RENDERING.md). The combined lightmap saturates at 2^2.2.
    float light_scale = 1.0f;
    bool fullbright = false;
    std::array<float, 3> camera_position{}; // For water.
    float time = 0.0f;                      // Seconds; animates water and light styles.
    bool light_styles = true;               // false: lightmap style 0 only (debugging).
    // Material params table (MaterialSystem::params()); null: defaults only.
    const std::vector<MaterialParams>* material_params = nullptr;
    // false while drawing into a monitor's render target, so no target is
    // sampled while it is being drawn.
    bool draw_monitors = true;
    Fog fog;
    // true: draw only the 3D skybox (the sky_camera's area); false: everything else.
    bool skybox = false;
    // false while drawing a water reflection or refraction (or a monitor), so
    // no water target is sampled while it is being drawn.
    bool draw_water = true;
    // The water targets hold this frame's reflection and refraction; if not,
    // water shows its fog colour.
    bool water_targets = false;
    bool envmaps = true; // Environment-map reflections ($envmap).
    bool bumpmaps = true; // Normal maps lit by the directional lightmaps ($bumpmap).
    // What this view can see (docs/RENDERING.md#visibility); null: everything.
    const formats::ViewVisibility* visibility = nullptr;
};

// A horizontal water surface: the water faces at one height. As in the
// original, all water samples one reflection and one refraction target per
// frame, drawn for one plane (the nearest in view): the scene mirrored about
// its surface (only what is above it) and the scene as seen (only what is
// below it, in its water fog).
struct WaterPlane {
    float z = 0;
    std::array<float, 3> mins{}, maxs{}; // Bounds of its faces.
    bool skybox = false;                 // In the 3D skybox's area (drawn without targets).
    const RenderMaterial* fog_material = nullptr; // Its water fog ($fogcolor, $fogstart, $fogend).
};

// The 3D skybox: a miniature scene in a sealed area of the map, seen from
// `origin` plus the player's position divided by `scale`.
struct SkyCamera {
    std::array<float, 3> origin{};
    float scale = 16.0f;
    Fog fog;
    std::uint16_t area = 0; // BSP area holding the skybox.
};

// A func_monitor screen showing its point_camera's view.
struct MonitorView {
    std::array<float, 3> origin{};  // Camera position.
    std::array<float, 3> look_at{}; // Point the camera looks at.
    float fov = 90.0f;               // Degrees, across the (square) picture.
    std::unique_ptr<rhi::RenderTarget> target;
};

class WorldRenderer {
public:
    // `shown`: targetnames of StartHidden entities to draw anyway (as if a
    // script had shown them, e.g. the haven TV's "tv_screen").
    WorldRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp,
                  const std::vector<std::string>& shown = {});
    ~WorldRenderer();

    // `view_projection` is a column-major 4x4 matrix. Callers mixing in other
    // geometry draw all opaque passes before the translucent pass.
    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const WorldDrawSettings& settings);

    // The translucent pass, in the original's order and mixed with other
    // renderers' items (render/translucent.hpp): water first, then each
    // collected item. Translucent faces of the world are grouped by the BSP
    // node they lie on; those of brush entities count as entities.
    void draw_water_surfaces(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                             const WorldDrawSettings& settings);
    void collect_translucent(const formats::BspDrawOrder& order, const WorldDrawSettings& settings,
                             std::uint32_t source, std::vector<TranslucentItem>& out) const;
    void begin_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                           const WorldDrawSettings& settings);
    // `rebind`: another renderer drew since this one last did.
    void draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index, bool rebind);

    const WorldStats& stats() const { return stats_; }
    // Visible monitors; draw each camera's view into its target before the frame's main view.
    const std::vector<MonitorView>& monitors() const { return monitors_; }
    const std::optional<SkyCamera>& sky_camera() const { return sky_camera_; }

    const std::vector<WaterPlane>& water_planes() const { return water_planes_; }
    // (Re)creates the water's reflection and refraction targets at this size
    // (the window's, so they match the view they are sampled in); a no-op if
    // unchanged. Null targets until then, or if the map has no water.
    void create_water_targets(std::uint32_t width, std::uint32_t height);
    const rhi::RenderTarget* water_reflection() const { return water_reflection_.get(); }
    const rhi::RenderTarget* water_refraction() const { return water_refraction_.get(); }

private:
    struct Batch {
        const RenderMaterial* material = nullptr;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::unique_ptr<rhi::BindGroup> bind_group;
        // One per frame of an animated texture (base, or the water normal map).
        std::vector<std::unique_ptr<rhi::BindGroup>> frame_bind_groups;
        bool decal = false;
        bool skybox = false; // In the 3D skybox's area.
        // Water: the bind groups of its material's $bottommaterial, for eyes below the surface.
        std::unique_ptr<rhi::BindGroup> bottom_bind_group;
        std::vector<std::unique_ptr<rhi::BindGroup>> bottom_frame_bind_groups;
        int monitor = -1; // Screen faces of monitors_[monitor], showing its render target.
        // Translucent batches: the BSP node the faces lie on (-1: none; then
        // the leaves their bounds touch), whether they belong to a brush
        // entity, and the centre of their bounds.
        std::int32_t node = -1;
        std::vector<std::uint32_t> leaves;
        bool entity = false;
        std::array<float, 3> center{};
        // Opaque batches: their faces grouped by the clusters they lie in,
        // each group with its bounds, so a view draws only the groups in its
        // PVS and frustum (docs/RENDERING.md#visibility).
        struct Run {
            std::uint32_t first_index = 0;
            std::uint32_t index_count = 0;
            std::vector<std::int32_t> clusters;
            formats::Vec3 mins{}, maxs{};
        };
        std::vector<Run> runs;

        const rhi::BindGroup& current_bind_group() const;
    };

    void build_lightmap_atlas(const formats::BspFile& bsp, std::vector<std::array<float, 2>>& face_offsets);
    void draw_batches(rhi::CommandList& cmd, const std::vector<Batch>& batches, const std::array<float, 16>& vp,
                      const WorldDrawSettings& settings);
    // Fills shader_data_ for `settings`; returns the material params rows it holds.
    std::size_t build_shader_data(const WorldDrawSettings& settings);
    void draw_batch(rhi::CommandList& cmd, const Batch& batch, const std::array<float, 16>& vp,
                    const WorldDrawSettings& settings, std::size_t rows, const rhi::Pipeline*& bound,
                    const rhi::ShaderDataRef* data);
    const rhi::Pipeline& pipeline_for(const RenderMaterial& m) const;

    rhi::Device& device_;
    WorldStats stats_;
    std::unique_ptr<rhi::Texture> lightmap_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::Buffer> vertices_;
    std::unique_ptr<rhi::Buffer> indices_;
    // [blend: opaque, alpha, additive][cull: back, none]
    std::array<std::array<std::unique_ptr<rhi::Pipeline>, 2>, 3> pipelines_;
    void draw_water(rhi::CommandList& cmd, const std::array<float, 16>& vp, const WorldDrawSettings& settings);

    std::vector<Batch> opaque_;
    std::vector<Batch> translucent_;
    std::vector<Batch> water_;
    std::vector<Batch> decals_; // infodecals, drawn over the opaque world.
    std::vector<MonitorView> monitors_;
    std::optional<SkyCamera> sky_camera_;
    std::vector<WaterPlane> water_planes_;
    std::unique_ptr<rhi::RenderTarget> water_reflection_, water_refraction_;
    std::array<std::uint32_t, 2> water_target_size_{};
    std::unique_ptr<rhi::Texture> black_;  // Stands in for water targets not drawn.
    const rhi::Texture* black_cube_ = nullptr; // Stands in for a missing water envmap (MaterialSystem's).
    std::unique_ptr<rhi::Pipeline> water_pipeline_;
    std::vector<float> water_data_; // Water shader data: camera and time, world fog.
    std::array<std::unique_ptr<rhi::Pipeline>, 2> decal_pipelines_; // [alpha, modulate]
    std::unique_ptr<rhi::Sampler> decal_sampler_;                    // Clamped, so edges do not wrap.
    std::array<float, 2> white_luxel_{}; // Lightmap coordinates used by faces without lightmaps.
    LightStyles styles_;
    // Shader data: style brightness (16 float4), then the material params table.
    std::vector<float> shader_data_;
    // The translucent pass under way (begin_translucent).
    struct TranslucentPass {
        std::array<float, 16> view_projection{};
        WorldDrawSettings settings;
        std::size_t rows = 1;
        rhi::ShaderDataRef data;
        const rhi::Pipeline* bound = nullptr;
    } pass_;
};

} // namespace openbl::render
