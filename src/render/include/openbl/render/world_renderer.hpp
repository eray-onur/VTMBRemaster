#pragma once

// Draws the brush geometry of a map: the world model and brush entities,
// textured and lit by the baked lightmaps, with every light style weighted
// by its current brightness (switchable lights in their default state,
// animated styles flickering).

#include "openbl/formats/bsp.hpp"
#include "openbl/render/light_styles.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace openbl::render {

struct WorldStats {
    std::size_t faces = 0;
    std::size_t triangles = 0;
    std::size_t batches = 0;
    std::size_t skipped_faces = 0; // Tool, sky and not-yet-supported surfaces.
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
};

class WorldRenderer {
public:
    WorldRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp);
    ~WorldRenderer();

    // `view_projection` is a column-major 4x4 matrix. draw() runs both passes;
    // callers mixing in other geometry draw all opaque passes before translucent ones.
    void draw(rhi::CommandList& cmd, const std::array<float, 16>& view_projection, const WorldDrawSettings& settings);
    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const WorldDrawSettings& settings);
    void draw_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                          const WorldDrawSettings& settings);

    const WorldStats& stats() const { return stats_; }

private:
    struct Batch {
        const RenderMaterial* material = nullptr;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::unique_ptr<rhi::BindGroup> bind_group;
        // One per frame of an animated texture (base, or the water normal map).
        std::vector<std::unique_ptr<rhi::BindGroup>> frame_bind_groups;
        bool decal = false;

        const rhi::BindGroup& current_bind_group() const;
    };

    void build_lightmap_atlas(const formats::BspFile& bsp, std::vector<std::array<float, 2>>& face_offsets);
    void draw_batches(rhi::CommandList& cmd, const std::vector<Batch>& batches, const std::array<float, 16>& vp,
                      const WorldDrawSettings& settings);
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
    std::unique_ptr<rhi::Pipeline> water_pipeline_;
    std::array<std::unique_ptr<rhi::Pipeline>, 2> decal_pipelines_; // [alpha, modulate]
    std::unique_ptr<rhi::Sampler> decal_sampler_;                    // Clamped, so edges do not wrap.
    std::array<float, 2> white_luxel_{}; // Lightmap coordinates used by faces without lightmaps.
    LightStyles styles_;
    // Shader data: style brightness (16 float4), then the material params table.
    std::vector<float> shader_data_;
};

} // namespace openbl::render
