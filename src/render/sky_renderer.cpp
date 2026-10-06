#include "openbl/render/sky_renderer.hpp"

#include "openbl_textured_ps.h"
#include "openbl_textured_vs.h"

#include <stdexcept>
#include <vector>

namespace openbl::render {

namespace {

struct Vertex {
    float position[3];
    float uv[2];
};

using V3 = std::array<float, 3>;

// Face direction, and the directions of increasing u and decreasing v, as seen
// from inside the cube. Verified by colour continuity across all cube edges of
// the shipped skyboxes (see docs/formats/maps.md).
struct Face {
    const char* suffix;
    V3 dir, right, up;
};
constexpr Face kFaces[6] = {
    {"rt", {1, 0, 0}, {0, -1, 0}, {0, 0, 1}},
    {"lf", {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
    {"bk", {0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {"ft", {0, -1, 0}, {-1, 0, 0}, {0, 0, 1}},
    {"up", {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}},
    {"dn", {0, 0, -1}, {0, -1, 0}, {1, 0, 0}},
};

} // namespace

SkyRenderer::SkyRenderer(rhi::Device& device, MaterialSystem& materials, const std::string& sky_name) {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (const Face& f : kFaces) {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        for (const auto [s, t] : {std::pair{-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}}) {
            Vertex v{};
            for (std::size_t k = 0; k < 3; ++k) {
                v.position[k] = (f.dir[k] + f.right[k] * s + f.up[k] * t) * 16.0f;
            }
            v.uv[0] = (s + 1.0f) * 0.5f;
            v.uv[1] = (1.0f - t) * 0.5f;
            vertices.push_back(v);
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    vertices_ = device.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                     std::as_bytes(std::span(vertices)));
    indices_ = device.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                    std::as_bytes(std::span(indices)));

    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_textured_vs;
    pd.pixel.spirv = openbl_textured_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}};
    pd.cull = rhi::CullMode::None;
    pd.depth_test = false;
    pd.depth_write = false;
    pipeline_ = device.create_pipeline(pd);
    // Clamp so that the edges of neighbouring faces do not bleed into each other.
    sampler_ = device.create_sampler({rhi::Filter::Linear, rhi::AddressMode::Clamp, 1.0f});

    std::size_t found = 0;
    for (std::size_t i = 0; i < 6; ++i) {
        const RenderMaterial& m = materials.material("skybox/" + sky_name + kFaces[i].suffix);
        found += !m.missing;
        const rhi::Texture* textures[] = {m.base};
        faces_[i] = device.create_bind_group(*pipeline_, *sampler_, textures);
    }
    if (found == 0) {
        throw std::runtime_error("skybox not found: " + sky_name);
    }
}

SkyRenderer::~SkyRenderer() = default;

void SkyRenderer::draw(rhi::CommandList& cmd, const std::array<float, 16>& rotation_projection) {
    cmd.set_pipeline(*pipeline_);
    cmd.set_vertex_buffer(*vertices_);
    cmd.set_index_buffer(*indices_);
    cmd.push_constants(rotation_projection.data(), sizeof(rotation_projection));
    for (std::uint32_t i = 0; i < 6; ++i) {
        cmd.set_bind_group(*faces_[i]);
        cmd.draw_indexed(6, i * 6);
    }
}

} // namespace openbl::render
