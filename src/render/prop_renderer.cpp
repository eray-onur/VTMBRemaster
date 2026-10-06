#include "openbl/render/prop_renderer.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/vtx.hpp"
#include "openbl/render/light_probe.hpp"

#include "openbl_prop_ps.h"
#include "openbl_prop_vs.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace openbl::render {

namespace {

struct Vertex {
    float position[3];
    float uv[2];
};

struct PushConstants {
    float mvp[16];
    float tint[4];
    std::uint32_t flags;
    float padding[3];
};
static_assert(sizeof(PushConstants) == 96);

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kUnlit = 4;
constexpr std::uint32_t kSelfIllum = 8;
constexpr std::uint32_t kTranslucent = 16;

// Light used when no lit floor is found below a prop.
constexpr std::array<float, 3> kDefaultLight = {0.25f, 0.25f, 0.25f};

// Source angles: pitch (down positive) about Y, yaw about Z, roll about X;
// applied roll, then pitch, then yaw.
glm::mat4 prop_transform(const formats::Vec3& origin, const formats::Vec3& angles) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(origin[0], origin[1], origin[2]));
    m = glm::rotate(m, glm::radians(angles[1]), glm::vec3(0, 0, 1));
    m = glm::rotate(m, glm::radians(angles[0]), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(angles[2]), glm::vec3(1, 0, 0));
    return m;
}

formats::Vec3 parse_vec3(const formats::BspEntity& e, const char* key) {
    formats::Vec3 v{};
    if (auto it = e.find(key); it != e.end()) {
        std::istringstream(it->second) >> v[0] >> v[1] >> v[2];
    }
    return v;
}

std::string material_name(const AssetSource& assets, const formats::MdlFile& mdl, std::int32_t texture) {
    if (texture < 0) {
        return {};
    }
    const std::string& tex = mdl.textures[static_cast<std::size_t>(texture)];
    std::string first;
    for (const std::string& dir : mdl.texture_dirs) {
        const std::string name = formats::normalize_game_path(dir + "/" + tex);
        if (first.empty()) {
            first = name;
        }
        if (assets.exists("materials/" + name + ".vmt")) {
            return name;
        }
    }
    return first.empty() ? formats::normalize_game_path(tex) : first;
}

} // namespace

PropRenderer::PropRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                           const formats::BspFile& bsp)
    : device_(device), materials_(materials), assets_(assets) {
    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_prop_vs;
    pd.pixel.spirv = openbl_prop_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}};
    pd.texture_count = 1;
    pd.push_constant_size = sizeof(PushConstants);
    pd.front_counter_clockwise = false;
    const rhi::BlendMode blends[] = {rhi::BlendMode::Opaque, rhi::BlendMode::Alpha, rhi::BlendMode::Additive};
    for (std::size_t b = 0; b < 3; ++b) {
        for (std::size_t c = 0; c < 2; ++c) {
            pd.blend = blends[b];
            pd.depth_write = b == 0;
            pd.cull = c == 0 ? rhi::CullMode::Back : rhi::CullMode::None;
            pipelines_[b][c] = device.create_pipeline(pd);
        }
    }
    sampler_ = device.create_sampler({});

    const FloorLightProbe probe(bsp);
    auto add = [&](const std::string& path, const formats::Vec3& origin, const formats::Vec3& angles,
                   std::size_t skin, const formats::Vec3& light_point) {
        const Model* model = load_model(path);
        if (!model) {
            return;
        }
        Instance inst;
        inst.model = model;
        inst.skin = skin < model->skins.size() ? skin : 0;
        const glm::mat4 m = prop_transform(origin, angles);
        std::memcpy(inst.transform.data(), glm::value_ptr(m), sizeof(inst.transform));
        const formats::Vec3 probe_point{light_point[0], light_point[1], light_point[2] + 16.0f};
        if (auto light = probe.sample(probe_point)) {
            inst.light = *light;
        } else {
            inst.light = kDefaultLight;
            ++stats_.unprobed_instances;
        }
        instances_.push_back(inst);
    };

    constexpr std::uint8_t kUseLightingOrigin = 0x2;
    for (const auto& sp : bsp.static_props) {
        add(bsp.static_prop_models[sp.model], sp.origin, sp.angles, static_cast<std::size_t>(std::max(sp.skin, 0)),
            (sp.flags & kUseLightingOrigin) ? sp.lighting_origin : sp.origin);
    }
    for (const auto& e : bsp.entities) {
        auto cls = e.find("classname");
        auto model = e.find("model");
        if (cls == e.end() || model == e.end() || !cls->second.starts_with("prop_") ||
            !formats::normalize_game_path(model->second).ends_with(".mdl")) {
            continue;
        }
        std::size_t skin = 0;
        if (auto s = e.find("skin"); s != e.end()) {
            skin = static_cast<std::size_t>(std::max(0, std::atoi(s->second.c_str())));
        }
        const auto origin = parse_vec3(e, "origin");
        add(formats::normalize_game_path(model->second), origin, parse_vec3(e, "angles"), skin, origin);
    }
    stats_.instances = instances_.size();
}

PropRenderer::~PropRenderer() = default;

const PropRenderer::Model* PropRenderer::load_model(const std::string& path) {
    if (auto it = models_.find(path); it != models_.end()) {
        return it->second.get();
    }
    std::unique_ptr<Model> model;
    try {
        const std::string base = path.substr(0, path.size() - 4);
        auto mdl_data = assets_.read(path);
        auto vtx_data = assets_.read(base + ".dx80.vtx");
        if (mdl_data && vtx_data) {
            const auto mdl = formats::parse_mdl(*mdl_data);
            const auto vtx = formats::parse_vtx(*vtx_data);
            if (mdl.checksum != vtx.checksum || vtx.body_parts.size() != mdl.body_parts.size()) {
                throw formats::FormatError("mdl/vtx mismatch");
            }
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;
            struct MeshRange {
                std::uint32_t first, count;
                std::int32_t material;
            };
            std::vector<MeshRange> ranges;
            for (std::size_t b = 0; b < mdl.body_parts.size(); ++b) {
                const auto& part = mdl.body_parts[b];
                if (part.models.empty() || vtx.body_parts[b].models.empty() || vtx.body_parts[b].models[0].lods.empty()) {
                    continue;
                }
                const auto& mo = part.models[0]; // Default body group.
                const auto& lod = vtx.body_parts[b].models[0].lods[0];
                const auto vertex_base = static_cast<std::uint32_t>(vertices.size());
                for (const auto& v : mo.vertices) {
                    vertices.push_back({{v.position[0], v.position[1], v.position[2]}, {v.uv[0], v.uv[1]}});
                }
                for (std::size_t me = 0; me < mo.meshes.size() && me < lod.meshes.size(); ++me) {
                    const auto tris = lod.meshes[me].triangles();
                    if (tris.empty()) {
                        continue;
                    }
                    MeshRange r{static_cast<std::uint32_t>(indices.size()), static_cast<std::uint32_t>(tris.size()),
                                mo.meshes[me].material};
                    for (std::uint32_t t : tris) {
                        indices.push_back(vertex_base + mo.meshes[me].vertex_offset + t);
                    }
                    ranges.push_back(r);
                    stats_.triangles += tris.size() / 3;
                }
            }
            if (!indices.empty()) {
                model = std::make_unique<Model>();
                model->vertices = device_.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                                        std::as_bytes(std::span(vertices)));
                model->indices = device_.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                                       std::as_bytes(std::span(indices)));
                const std::size_t families = std::max<std::size_t>(1, mdl.skin_families.size());
                model->skins.resize(families);
                for (std::size_t f = 0; f < families; ++f) {
                    for (const MeshRange& r : ranges) {
                        Mesh mesh;
                        mesh.first_index = r.first;
                        mesh.index_count = r.count;
                        mesh.material = &materials_.material(material_name(assets_, mdl, mdl.texture_for(r.material, f)));
                        if (mesh.material->skip) {
                            continue;
                        }
                        const rhi::Texture* textures[] = {mesh.material->base};
                        mesh.bind_group = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
                        model->skins[f].push_back(std::move(mesh));
                    }
                }
                ++stats_.models;
            }
        }
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "model %s: %s\n", path.c_str(), e.what());
        model.reset();
    }
    if (!model) {
        ++stats_.missing_models;
    }
    return models_.emplace(path, std::move(model)).first->second.get();
}

const rhi::Pipeline& PropRenderer::pipeline_for(const RenderMaterial& m) const {
    const std::size_t blend = !m.translucent ? 0 : (m.additive ? 2 : 1);
    return *pipelines_[blend][m.no_cull ? 1 : 0];
}

void PropRenderer::draw_pass(rhi::CommandList& cmd, const std::array<float, 16>& vp, const PropDrawSettings& settings,
                             bool translucent) {
    const glm::mat4 view_projection = glm::make_mat4(vp.data());
    const rhi::Pipeline* bound_pipeline = nullptr;
    const Model* bound_model = nullptr;
    PushConstants pc{};
    for (const Instance& inst : instances_) {
        const glm::mat4 mvp = view_projection * glm::make_mat4(inst.transform.data());
        std::memcpy(pc.mvp, glm::value_ptr(mvp), sizeof(pc.mvp));
        for (int k = 0; k < 3; ++k) {
            pc.tint[k] = inst.light[static_cast<std::size_t>(k)] * settings.light_scale;
        }
        for (const Mesh& mesh : inst.model->skins[inst.skin]) {
            const RenderMaterial& m = *mesh.material;
            if (m.translucent != translucent) {
                continue;
            }
            const rhi::Pipeline& pipeline = pipeline_for(m);
            if (&pipeline != bound_pipeline) {
                cmd.set_pipeline(pipeline);
                bound_pipeline = &pipeline;
            }
            if (inst.model != bound_model) {
                cmd.set_vertex_buffer(*inst.model->vertices);
                cmd.set_index_buffer(*inst.model->indices);
                bound_model = inst.model;
            }
            pc.tint[3] = m.alpha_ref;
            pc.flags = (m.alpha_test ? kAlphaTest : 0) | ((m.unlit || settings.fullbright) ? kUnlit : 0) |
                       (m.self_illum ? kSelfIllum : 0) | (m.translucent ? kTranslucent : 0);
            cmd.set_bind_group(*mesh.bind_group);
            cmd.push_constants(&pc, sizeof(pc));
            cmd.draw_indexed(mesh.index_count, mesh.first_index);
        }
    }
}

} // namespace openbl::render
