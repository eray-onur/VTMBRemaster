#pragma once

// Detail props (the map's `dprp` game lump; docs/RENDERING.md#detail-props):
// thousands of small models (grass, weeds, pebbles) per map, each with one
// baked light colour. They are baked into world-space buffers when the map
// loads, one per material, grouped by the cluster each prop stands in.

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/visibility.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <vector>

namespace openbl::render {

struct DetailPropStats {
    std::size_t props = 0;
    std::size_t triangles = 0;
    std::size_t missing_models = 0;
};

class DetailPropRenderer {
public:
    DetailPropRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                       const formats::BspFile& bsp);
    ~DetailPropRenderer();

    // Opaque materials first, then translucent ones (blended, without depth
    // writes, their cluster groups back to front from `eye`). `visibility`:
    // skips groups this view cannot see (null: none).
    void draw(rhi::CommandList& cmd, const std::array<float, 16>& view_projection, const std::array<float, 3>& eye,
              const Fog& fog, const formats::ViewVisibility* visibility);

    const DetailPropStats& stats() const { return stats_; }

private:
    struct Run {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::int32_t cluster = -1;
        formats::Vec3 mins{}, maxs{};
    };
    struct Batch {
        const RenderMaterial* material = nullptr;
        std::vector<Run> runs;
        std::unique_ptr<rhi::BindGroup> bind_group;
    };

    DetailPropStats stats_;
    std::vector<Batch> batches_; // Opaque ones first.
    std::array<std::array<std::unique_ptr<rhi::Pipeline>, 2>, 2> pipelines_; // [translucent][no cull]
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::Buffer> vertices_;
    std::unique_ptr<rhi::Buffer> indices_;
    std::vector<std::pair<float, const Run*>> sorted_; // Scratch for the translucent order.
};

} // namespace openbl::render
