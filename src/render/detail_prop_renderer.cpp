#include "openbl/render/detail_prop_renderer.hpp"

#include "model_materials.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/vtx.hpp"
#include "openbl/render/light_styles.hpp"

#include "openbl_detail_ps.h"
#include "openbl_detail_vs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace openbl::render {

namespace {

struct PushConstants {
    float view_projection[16];
    std::uint32_t flags;
    float alpha_ref;
};

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kTranslucent = 2;

struct Vertex {
    float position[3];
    float uv[2];
    float light[3];
};
static_assert(sizeof(Vertex) == 32);

// A detail model's default body and LOD 0: positions, texture coordinates
// and, per mesh, its material's texture index and triangle list.
struct ModelMesh {
    std::int32_t texture = -1;
    std::vector<std::uint32_t> indices;
};
struct ModelData {
    std::vector<formats::Vec3> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<ModelMesh> meshes;
    std::vector<std::string> materials; // Per mesh.
};

std::optional<ModelData> load(const AssetSource& assets, const std::string& path) {
    const auto mdl_data = assets.read(path);
    const auto vtx_data = assets.read(path.substr(0, path.size() - 4) + ".dx80.vtx");
    if (!mdl_data || !vtx_data) {
        return std::nullopt;
    }
    const auto mdl = formats::parse_mdl(*mdl_data);
    const auto vtx = formats::parse_vtx(*vtx_data);
    if (mdl.checksum != vtx.checksum || vtx.body_parts.size() != mdl.body_parts.size()) {
        throw formats::FormatError("mdl/vtx mismatch");
    }
    ModelData out;
    for (std::size_t b = 0; b < mdl.body_parts.size(); ++b) {
        const auto& part = mdl.body_parts[b];
        if (part.models.empty() || vtx.body_parts[b].models.empty() || vtx.body_parts[b].models[0].lods.empty()) {
            continue;
        }
        const auto& mo = part.models[0];
        const auto& lod = vtx.body_parts[b].models[0].lods[0];
        const auto base = static_cast<std::uint32_t>(out.positions.size());
        for (const auto& v : mo.vertices) {
            out.positions.push_back({v.position[0], v.position[1], v.position[2]});
            out.uvs.push_back({v.uv[0], v.uv[1]});
        }
        for (std::size_t me = 0; me < mo.meshes.size() && me < lod.meshes.size(); ++me) {
            ModelMesh mesh;
            mesh.texture = mdl.texture_for(mo.meshes[me].material, 0);
            for (std::uint32_t t : lod.meshes[me].triangles()) {
                mesh.indices.push_back(base + mo.meshes[me].vertex_offset + t);
            }
            if (!mesh.indices.empty()) {
                out.materials.push_back(detail::model_material_name(assets, mdl, mesh.texture));
                out.meshes.push_back(std::move(mesh));
            }
        }
    }
    return out;
}

// Source angles (pitch, yaw, roll) as a row-major rotation: yaw about z,
// then pitch about y, then roll about x, as for static props.
std::array<std::array<float, 3>, 3> rotation(const formats::Vec3& angles) {
    constexpr float kRad = 3.14159265f / 180.0f;
    const float sp = std::sin(angles[0] * kRad), cp = std::cos(angles[0] * kRad);
    const float sy = std::sin(angles[1] * kRad), cy = std::cos(angles[1] * kRad);
    const float sr = std::sin(angles[2] * kRad), cr = std::cos(angles[2] * kRad);
    return {{{cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy},
             {cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy},
             {-sp, sr * cp, cr * cp}}};
}

std::array<float, 3> rgbe(const std::array<std::uint8_t, 4>& c) {
    std::byte bytes[4];
    std::memcpy(bytes, c.data(), 4);
    return formats::decode_rgbe(bytes);
}

} // namespace

DetailPropRenderer::DetailPropRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                                       const formats::BspFile& bsp) {
    const LightStyles styles(bsp);
    std::unordered_map<std::uint16_t, std::optional<ModelData>> models;
    // Per material, per cluster: the baked vertices and indices.
    struct Group {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        formats::Vec3 mins{1e30f, 1e30f, 1e30f}, maxs{-1e30f, -1e30f, -1e30f};
    };
    std::map<std::string, std::map<std::int32_t, Group>> groups;
    for (const formats::BspDetailProp& prop : bsp.detail_props) {
        auto [it, inserted] = models.try_emplace(prop.model);
        if (inserted && prop.model < bsp.detail_prop_models.size()) {
            const std::string path = formats::normalize_game_path(bsp.detail_prop_models[prop.model]);
            try {
                it->second = load(assets, path);
            } catch (const formats::FormatError& e) {
                std::fprintf(stderr, "detail model %s: %s\n", path.c_str(), e.what());
            }
            stats_.missing_models += !it->second;
        }
        if (!it->second) {
            continue;
        }
        const ModelData& model = *it->second;
        // The baked light: style 0's colour, plus each extra style at its
        // default brightness.
        std::array<float, 3> light = rgbe(prop.lighting);
        for (std::uint32_t s = 0; s < prop.light_style_count; ++s) {
            const std::size_t i = std::size_t{prop.first_light_style} + s;
            if (prop.first_light_style == 0xFFFFFFFF || i >= bsp.detail_light_styles.size()) {
                break;
            }
            const auto& style = bsp.detail_light_styles[i];
            const auto c = rgbe(style.lighting);
            const float value = styles.default_value(style.style);
            for (std::size_t k = 0; k < 3; ++k) {
                light[k] += c[k] * value;
            }
        }
        const auto r = rotation(prop.angles);
        const std::int32_t cluster = prop.leaf < bsp.leafs.size() ? bsp.leafs[prop.leaf].cluster : -1;
        for (std::size_t m = 0; m < model.meshes.size(); ++m) {
            Group& g = groups[model.materials[m]][cluster];
            const auto base = static_cast<std::uint32_t>(g.vertices.size());
            std::vector<std::uint32_t> remap(model.positions.size(), UINT32_MAX);
            for (std::uint32_t index : model.meshes[m].indices) {
                if (remap[index] == UINT32_MAX) {
                    remap[index] = base + static_cast<std::uint32_t>(g.vertices.size() - base);
                    const auto& p = model.positions[index];
                    Vertex v{};
                    for (std::size_t k = 0; k < 3; ++k) {
                        v.position[k] = prop.origin[k] + r[k][0] * p[0] + r[k][1] * p[1] + r[k][2] * p[2];
                        g.mins[k] = std::min(g.mins[k], v.position[k]);
                        g.maxs[k] = std::max(g.maxs[k], v.position[k]);
                        v.light[k] = light[k];
                    }
                    v.uv[0] = model.uvs[index][0];
                    v.uv[1] = model.uvs[index][1];
                    g.vertices.push_back(v);
                }
                g.indices.push_back(remap[index]);
            }
            stats_.triangles += model.meshes[m].indices.size() / 3;
        }
        ++stats_.props;
    }

    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_detail_vs;
    pd.pixel.spirv = openbl_detail_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}, {2, rhi::Format::RGB32_Float, 20}};
    pd.texture_count = 1;
    pd.push_constant_size = sizeof(PushConstants);
    pd.shader_data = true;
    for (int t = 0; t < 2; ++t) {
        for (int c = 0; c < 2; ++c) {
            pd.blend = t ? rhi::BlendMode::Alpha : rhi::BlendMode::Opaque;
            pd.depth_write = t == 0;
            pd.cull = c ? rhi::CullMode::None : rhi::CullMode::Back;
            pipelines_[t][c] = device.create_pipeline(pd);
        }
    }
    sampler_ = device.create_sampler({});

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (auto& [name, by_cluster] : groups) {
        const RenderMaterial& m = materials.material(name);
        if (m.skip) {
            continue;
        }
        Batch batch;
        batch.material = &m;
        for (auto& [cluster, g] : by_cluster) {
            Run run;
            run.first_index = static_cast<std::uint32_t>(indices.size());
            run.index_count = static_cast<std::uint32_t>(g.indices.size());
            run.cluster = cluster;
            run.mins = g.mins;
            run.maxs = g.maxs;
            const auto offset = static_cast<std::uint32_t>(vertices.size());
            for (std::uint32_t i : g.indices) {
                indices.push_back(i + offset);
            }
            vertices.insert(vertices.end(), g.vertices.begin(), g.vertices.end());
            batch.runs.push_back(run);
        }
        const rhi::Texture* textures[] = {m.base};
        batch.bind_group = device.create_bind_group(*pipelines_[0][0], *sampler_, textures);
        batches_.push_back(std::move(batch));
    }
    std::stable_sort(batches_.begin(), batches_.end(),
                     [](const Batch& a, const Batch& b) { return !a.material->translucent && b.material->translucent; });
    if (!vertices.empty()) {
        vertices_ = device.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                         std::as_bytes(std::span(vertices)));
        indices_ = device.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                        std::as_bytes(std::span(indices)));
    }
}

DetailPropRenderer::~DetailPropRenderer() = default;

void DetailPropRenderer::draw(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                              const std::array<float, 3>& eye, const Fog& fog,
                              const formats::ViewVisibility* visibility) {
    if (!vertices_) {
        return;
    }
    const auto fog_rows = fog.rows();
    const rhi::Pipeline* bound = nullptr;
    for (const Batch& batch : batches_) {
        const RenderMaterial& m = *batch.material;
        const rhi::Pipeline& pipeline = *pipelines_[m.translucent ? 1 : 0][m.no_cull ? 1 : 0];
        sorted_.clear();
        for (const Run& run : batch.runs) {
            if (visibility && !(visibility->cluster(run.cluster) && visibility->box(run.mins, run.maxs))) {
                continue;
            }
            float d2 = 0.0f;
            for (std::size_t k = 0; k < 3; ++k) {
                const float c = (run.mins[k] + run.maxs[k]) * 0.5f - eye[k];
                d2 += c * c;
            }
            sorted_.push_back({d2, &run});
        }
        if (sorted_.empty()) {
            continue;
        }
        if (m.translucent) { // Far to near.
            std::sort(sorted_.begin(), sorted_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        }
        if (&pipeline != bound) {
            cmd.set_pipeline(pipeline);
            cmd.set_shader_data(fog_rows.data(), sizeof(fog_rows));
            cmd.set_vertex_buffer(*vertices_);
            cmd.set_index_buffer(*indices_);
            bound = &pipeline;
        }
        PushConstants pc{};
        std::memcpy(pc.view_projection, view_projection.data(), sizeof(pc.view_projection));
        pc.flags = (m.alpha_test ? kAlphaTest : 0) | (m.translucent ? kTranslucent : 0);
        pc.alpha_ref = m.alpha_ref;
        cmd.set_bind_group(*batch.bind_group);
        cmd.push_constants(&pc, sizeof(pc));
        for (const auto& [d2, run] : sorted_) {
            cmd.draw_indexed(run->index_count, run->first_index);
        }
    }
}

} // namespace openbl::render
