#include "openbl/render/rope_renderer.hpp"

#include "openbl/anim/rope.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/render/prop_lighting.hpp"

#include "openbl_rope_ps.h"
#include "openbl_rope_vs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace openbl::render {

namespace {

struct PushConstants {
    float view_projection[16];
    float eye[4];
    std::uint32_t flags;
    float alpha_ref;
};

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kUnlit = 2;

struct Vertex {
    float position[3];
    float tangent[3];
    float uv[2];
    float light[3];
    float width;
};
static_assert(sizeof(Vertex) == 48);

// Texels of the base texture per unit of rope at TextureScale 1; larger
// scales stretch the texture (the move_rope documentation on the Valve
// Developer Community wiki).
constexpr float kTexelsPerUnit = 4.0f;
// The original's ropes sag as if they had this fraction of their Slack as
// extra length: fitted to the telephone cables of sm_hub_1 against a capture
// (docs/RENDERING.md#ropes-and-cables). With all of it they hang 170 units
// below their poles, into views where the original shows none.
constexpr float kSlackFraction = 0.15f;
// Curve points per link: about one every 12 units, 2 to 16.
constexpr float kCurveStep = 12.0f;

using V3 = anim::Vec3;

std::string value(const formats::BspEntity& e, const char* key) {
    const auto it = e.find(key);
    return it == e.end() ? std::string() : it->second;
}

float number(const formats::BspEntity& e, const char* key, float fallback) {
    const std::string v = value(e, key);
    return v.empty() ? fallback : static_cast<float>(std::atof(v.c_str()));
}

V3 vec3(const std::string& s) {
    V3 v{};
    std::istringstream(s) >> v[0] >> v[1] >> v[2];
    return v;
}

float distance(const V3& a, const V3& b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

RopeRenderer::RopeRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp) {
    std::optional<std::uint16_t> sky_area;
    std::unordered_map<std::string, const formats::BspEntity*> by_name;
    for (const auto& e : bsp.entities) {
        const std::string cls = value(e, "classname");
        if (cls == "sky_camera") {
            const auto o = vec3(value(e, "origin"));
            sky_area = bsp.leafs[bsp.leaf_at({o[0], o[1], o[2]})].area;
        }
        if ((cls == "keyframe_rope" || cls == "move_rope") && !value(e, "targetname").empty()) {
            by_name.emplace(value(e, "targetname"), &e);
        }
    }

    const PropLighting lighting(bsp);
    constexpr std::array<std::array<float, 3>, 3> kIdentity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    // Vertices and indices per (material, skybox), batched afterwards.
    std::map<std::pair<const RenderMaterial*, bool>, std::pair<std::vector<Vertex>, std::vector<std::uint32_t>>> groups;
    for (const auto& e : bsp.entities) {
        const std::string cls = value(e, "classname");
        if (cls != "keyframe_rope" && cls != "move_rope") {
            continue;
        }
        const auto next = by_name.find(value(e, "NextKey"));
        if (next == by_name.end()) {
            continue;
        }
        const RenderMaterial& m = materials.material(formats::normalize_game_path(value(e, "RopeMaterial")));
        if (m.missing || m.skip) {
            ++stats_.missing_materials;
            continue;
        }
        const V3 a = vec3(value(e, "origin"));
        const V3 b = vec3(value(*next->second, "origin"));
        const float slack = number(e, "Slack", 25.0f) * kSlackFraction;
        const int subdiv = static_cast<int>(number(e, "Subdiv", 2.0f));
        const float width = number(e, "Width", 2.0f);
        const float texture_scale = std::max(number(e, "TextureScale", 1.0f), 1e-3f);
        const float texture_height = static_cast<float>(m.base->desc().height);

        const auto nodes = anim::rope_rest_nodes(a, b, slack, subdiv);
        const float link = distance(nodes[0], nodes[1]);
        const int steps = std::clamp(static_cast<int>(std::ceil(link / kCurveStep)), 2, 16);
        const auto curve = anim::rope_curve(nodes, steps);

        // The light at each node: the mean of its ambient cube's faces with
        // every light folded in, encoded, clamped to the material's range.
        std::vector<std::array<float, 3>> node_light;
        for (const V3& p : nodes) {
            const ModelLighting l = lighting.sample(p, kIdentity);
            std::array<float, 3> c{};
            for (const auto& face : l.folded.color) {
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    c[ch] += face[ch] / 6.0f;
                }
            }
            for (float& v : c) {
                v = std::clamp(std::pow(std::max(v, 0.0f), 1.0f / 2.2f), m.min_light, m.max_light);
            }
            node_light.push_back(c);
        }

        const bool skybox = sky_area && bsp.leafs[bsp.leaf_at({a[0], a[1], a[2]})].area == *sky_area;
        auto& [vertices, indices] = groups[{&m, skybox}];
        float v = 0.0f;
        for (std::size_t i = 0; i < curve.size(); ++i) {
            if (i > 0) {
                v += distance(curve[i - 1], curve[i]) * kTexelsPerUnit / (texture_scale * texture_height);
            }
            const V3& before = curve[i > 0 ? i - 1 : i];
            const V3& after = curve[i + 1 < curve.size() ? i + 1 : i];
            // Interpolated between the nodes this point lies between.
            const float along = static_cast<float>(i) / static_cast<float>(steps);
            const std::size_t node = std::min(static_cast<std::size_t>(along), nodes.size() - 2);
            const float t = along - static_cast<float>(node);
            const auto base = static_cast<std::uint32_t>(vertices.size());
            for (int side = 0; side < 2; ++side) {
                Vertex vx{};
                for (int k = 0; k < 3; ++k) {
                    vx.position[k] = curve[i][static_cast<std::size_t>(k)];
                    vx.tangent[k] = after[static_cast<std::size_t>(k)] - before[static_cast<std::size_t>(k)];
                    vx.light[k] = node_light[node][static_cast<std::size_t>(k)] * (1.0f - t) +
                                  node_light[node + 1][static_cast<std::size_t>(k)] * t;
                }
                vx.uv[0] = static_cast<float>(side);
                vx.uv[1] = v;
                vx.width = width;
                vertices.push_back(vx);
            }
            if (i > 0) {
                indices.insert(indices.end(), {base - 2, base - 1, base + 1, base - 2, base + 1, base});
            }
        }
        ++stats_.ropes;
    }

    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_rope_vs;
    pd.pixel.spirv = openbl_rope_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0},
                     {1, rhi::Format::RGB32_Float, 12},
                     {2, rhi::Format::RG32_Float, 24},
                     {3, rhi::Format::RGB32_Float, 32},
                     {4, rhi::Format::R32_Float, 44}};
    pd.texture_count = 2; // Base, bump map.
    pd.push_constant_size = sizeof(PushConstants);
    pd.shader_data = true;
    pd.cull = rhi::CullMode::None;
    pipeline_ = device.create_pipeline(pd);
    sampler_ = device.create_sampler({});

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (auto& [key, group] : groups) {
        Batch batch;
        batch.material = key.first;
        batch.skybox = key.second;
        batch.first_index = static_cast<std::uint32_t>(indices.size());
        batch.index_count = static_cast<std::uint32_t>(group.second.size());
        const auto offset = static_cast<std::uint32_t>(vertices.size());
        for (std::uint32_t index : group.second) {
            indices.push_back(index + offset);
        }
        vertices.insert(vertices.end(), group.first.begin(), group.first.end());
        const rhi::Texture* textures[] = {key.first->base, key.first->bumpmap ? key.first->bumpmap : materials.white()};
        batch.bind_group = device.create_bind_group(*pipeline_, *sampler_, textures);
        batches_.push_back(std::move(batch));
    }
    if (!vertices.empty()) {
        vertices_ = device.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                         std::as_bytes(std::span(vertices)));
        indices_ = device.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                        std::as_bytes(std::span(indices)));
    }
}

RopeRenderer::~RopeRenderer() = default;

void RopeRenderer::draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                               const std::array<float, 3>& eye, const Fog& fog, bool skybox) {
    if (!vertices_) {
        return;
    }
    bool bound = false;
    for (const Batch& batch : batches_) {
        if (batch.skybox != skybox) {
            continue;
        }
        if (!bound) {
            cmd.set_pipeline(*pipeline_);
            const auto fog_rows = fog.rows();
            cmd.set_shader_data(fog_rows.data(), sizeof(fog_rows));
            cmd.set_vertex_buffer(*vertices_);
            cmd.set_index_buffer(*indices_);
            bound = true;
        }
        PushConstants pc{};
        std::memcpy(pc.view_projection, view_projection.data(), sizeof(pc.view_projection));
        pc.eye[0] = eye[0];
        pc.eye[1] = eye[1];
        pc.eye[2] = eye[2];
        const RenderMaterial& m = *batch.material;
        pc.flags = (m.alpha_test ? kAlphaTest : 0) | (m.unlit ? kUnlit : 0);
        pc.alpha_ref = m.alpha_ref;
        cmd.set_bind_group(*batch.bind_group);
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(batch.index_count, batch.first_index);
    }
}

} // namespace openbl::render
