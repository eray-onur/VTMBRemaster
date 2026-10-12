#pragma once

// Ropes and cables (keyframe_rope, move_rope): each rope entity with a
// NextKey hangs a rope from its origin to that entity's, shaped at rest
// (anim/rope.hpp) and lit at its nodes when the map loads
// (docs/RENDERING.md#ropes-and-cables).

#include "openbl/formats/bsp.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <vector>

namespace openbl::render {

struct RopeStats {
    std::size_t ropes = 0;
    std::size_t missing_materials = 0;
};

class RopeRenderer {
public:
    RopeRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp);
    ~RopeRenderer();

    // `skybox`: only the ropes in the 3D skybox's area, or only the rest;
    // `eye`: camera position (the strips turn to face it).
    void draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                     const std::array<float, 3>& eye, const Fog& fog, bool skybox);

    const RopeStats& stats() const { return stats_; }

private:
    // The ropes sharing a material and the skybox flag: one index range.
    struct Batch {
        const RenderMaterial* material = nullptr;
        bool skybox = false;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::unique_ptr<rhi::BindGroup> bind_group;
    };

    RopeStats stats_;
    std::vector<Batch> batches_;
    std::unique_ptr<rhi::Pipeline> pipeline_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::Buffer> vertices_;
    std::unique_ptr<rhi::Buffer> indices_;
};

} // namespace openbl::render
