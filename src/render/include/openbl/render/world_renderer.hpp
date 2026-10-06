#pragma once

// Draws the brush geometry of a map: the world model and brush entities,
// textured and lit by the baked lightmaps (style 0).

#include "openbl/formats/bsp.hpp"
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
    std::size_t missing_materials = 0;
    std::uint32_t lightmap_width = 0;
    std::uint32_t lightmap_height = 0;
};

struct WorldDrawSettings {
    float light_scale = 2.3f; // Source-style lightmap overbright (2^1.2 in linear space); verify against the original.
    bool fullbright = false;
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
    std::vector<Batch> opaque_;
    std::vector<Batch> translucent_;
    std::array<float, 2> white_luxel_{}; // Lightmap coordinates used by faces without lightmaps.
};

} // namespace openbl::render
