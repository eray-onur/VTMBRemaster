#include "openbl/render/prop_renderer.hpp"

#include "openbl/formats/mesh_decal.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/vtx.hpp"
#include "openbl/render/prop_lighting.hpp"

#include "half.hpp"
#include "mesh_util.hpp"
#include "model_materials.hpp"

#include "openbl_prop_ps.h"
#include "openbl_prop_vs.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>

namespace openbl::render {

namespace {

struct Vertex {
    float position[3];
    float uv[2];
    float normal[3];
};

struct PushConstants {
    float mvp[16];
    std::uint32_t cube[9]; // 18 half-floats: rgb per cube face
    float alpha_ref;
    std::uint32_t flags;
    std::uint32_t params_index;
    float model_z[4]; // World z row of the model transform (water fog).
};
static_assert(sizeof(PushConstants) == 128);

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kUnlit = 4;
constexpr std::uint32_t kSelfIllum = 8;
constexpr std::uint32_t kTranslucent = 16;
constexpr std::uint32_t kMultiplyLayers = 64;
constexpr std::uint32_t kAdditive = 128;
constexpr std::uint32_t kEnvmap = 256;
constexpr std::uint32_t kEnvmapMaskShift = 9;
constexpr std::uint32_t kFadeShift = 24; // 8 bits: 255 - the fade's opacity.

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

} // namespace

PropRenderer::PropRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                           const formats::BspFile& bsp, const std::vector<std::string>& shown)
    : device_(device), materials_(materials), assets_(assets) {
    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_prop_vs;
    pd.pixel.spirv = openbl_prop_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}, {2, rhi::Format::RGB32_Float, 20}};
    pd.texture_count = 4; // Base, second layer, envmap (cube), envmap mask.
    pd.push_constant_size = sizeof(PushConstants);
    pd.shader_data = true; // Material params table.
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

    const PropLighting lighting(bsp);
    // Props in the 3D skybox's area (the sky_camera's) are drawn only in the skybox pass.
    std::optional<std::uint16_t> sky_area;
    for (const auto& e : bsp.entities) {
        if (auto cls = e.find("classname"); cls != e.end() && cls->second == "sky_camera") {
            sky_area = bsp.leafs[bsp.leaf_at(parse_vec3(e, "origin"))].area;
        }
    }
    auto add = [&](const std::string& path, const formats::Vec3& origin, const formats::Vec3& angles,
                   std::size_t skin, const std::optional<formats::Vec3>& lighting_origin,
                   const formats::BspStaticProp* static_prop) {
        const Model* model = load_model(path);
        if (!model) {
            return;
        }
        Instance inst;
        inst.model = model;
        inst.skybox = sky_area && bsp.leafs[bsp.leaf_at(origin)].area == *sky_area;
        inst.skin = skin < model->skins.size() ? skin : 0;
        const glm::mat4 m = prop_transform(origin, angles);
        std::memcpy(inst.transform.data(), glm::value_ptr(m), sizeof(inst.transform));
        std::array<std::array<float, 3>, 3> rotation{};
        for (int r = 0; r < 3; ++r) {
            for (int k = 0; k < 3; ++k) {
                rotation[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] = m[k][r];
            }
        }
        auto solid = [&](const glm::vec3& p) {
            return (bsp.leafs[bsp.leaf_at({p.x, p.y, p.z})].contents & formats::contents::Solid) != 0;
        };
        // Sample at the model's centre (or its explicit lighting origin).
        const glm::vec3 c = lighting_origin ? glm::vec3(glm::make_vec3(lighting_origin->data()))
                                            : glm::vec3(m * glm::vec4(glm::make_vec3(model->center.data()), 1.0f));
        if (!solid(c)) {
            inst.lighting = lighting.sample({c.x, c.y, c.z}, rotation);
        } else {
            // Embedded in the world (e.g. a building wrapped around a brush
            // block): light each side of the cube from just outside that side
            // of the model's bounds, where the lit surroundings are visible.
            const AmbientCube fallback = lighting.sample({origin[0], origin[1], origin[2] + 16.0f}, rotation);
            inst.lighting = fallback;
            for (std::size_t face = 0; face < 6; ++face) {
                glm::vec3 local = glm::make_vec3(model->center.data());
                const std::size_t axis = face / 2;
                const float sign = (face % 2) ? -1.0f : 1.0f;
                local[static_cast<int>(axis)] += sign * (model->half_extent[axis] + 8.0f);
                const glm::vec3 p(m * glm::vec4(local, 1.0f));
                if (!solid(p)) {
                    inst.lighting.color[face] = lighting.sample({p.x, p.y, p.z}, rotation).color[face];
                }
            }
        }
        for (const Mesh& mesh : model->skins[inst.skin]) {
            inst.envmapped = inst.envmapped || mesh.material->envmap;
            inst.has_translucent = inst.has_translucent || mesh.material->translucent;
        }
        if (static_prop && (static_prop->flags & formats::kStaticPropFades)) {
            inst.fade = *static_prop;
            ++stats_.fading;
        }
        // The world-space box around the model's bounds, for the leaves it touches.
        glm::vec3 lo(INFINITY), hi(-INFINITY);
        for (int corner = 0; corner < 8; ++corner) {
            glm::vec3 local = glm::make_vec3(model->center.data());
            for (int k = 0; k < 3; ++k) {
                local[k] += ((corner >> k) & 1 ? 1.0f : -1.0f) * model->half_extent[static_cast<std::size_t>(k)];
            }
            const glm::vec3 w(m * glm::vec4(local, 1.0f));
            lo = glm::min(lo, w);
            hi = glm::max(hi, w);
        }
        inst.leaves = formats::leaves_in_box(bsp, {lo.x, lo.y, lo.z}, {hi.x, hi.y, hi.z});
        if (inst.envmapped) {
            inst.cubemap = materials_.nearest_cubemap({c.x, c.y, c.z});
        }
        instances_.push_back(std::move(inst));
    };

    constexpr std::uint8_t kUseLightingOrigin = 0x2;
    for (const auto& sp : bsp.static_props) {
        add(bsp.static_prop_models[sp.model], sp.origin, sp.angles, static_cast<std::size_t>(std::max(sp.skin, 0)),
            (sp.flags & kUseLightingOrigin) ? std::optional(sp.lighting_origin) : std::nullopt, &sp);
    }
    for (const auto& e : bsp.entities) {
        auto cls = e.find("classname");
        auto model = e.find("model");
        if (cls == e.end() || model == e.end() || !cls->second.starts_with("prop_") ||
            !formats::normalize_game_path(model->second).ends_with(".mdl")) {
            continue;
        }
        if (auto hidden = e.find("StartHidden"); hidden != e.end() && hidden->second == "1") {
            const auto name = e.find("targetname");
            if (name == e.end() || std::find(shown.begin(), shown.end(), name->second) == shown.end()) {
                continue; // Invisible until a script shows it.
            }
        }
        std::size_t skin = 0;
        if (auto s = e.find("skin"); s != e.end()) {
            skin = static_cast<std::size_t>(std::max(0, std::atoi(s->second.c_str())));
        }
        const auto origin = parse_vec3(e, "origin");
        add(formats::normalize_game_path(model->second), origin, parse_vec3(e, "angles"), skin, std::nullopt, nullptr);
    }
    stats_.instances = instances_.size();
    place_decals(bsp);
}

void PropRenderer::place_decals(const formats::BspFile& bsp) {
    // Decals the world cannot place (no brush face within 4 units) go on the
    // nearest prop surface within this distance.
    constexpr float kReach = 8.0f;
    for (const auto& e : bsp.entities) {
        const auto cls = e.find("classname");
        const auto texture = e.find("texture");
        if (cls == e.end() || cls->second != "infodecal" || texture == e.end()) {
            continue;
        }
        const RenderMaterial& mat = materials_.material(texture->second);
        if (mat.missing || mat.skip) {
            continue;
        }
        const auto origin = parse_vec3(e, "origin");
        const auto& size = mat.base->desc();
        const float width = static_cast<float>(size.width) * mat.decal_scale;
        const float height = static_cast<float>(size.height) * mat.decal_scale;
        if (!formats::project_decal(bsp, origin, width, height).empty()) {
            continue; // On the world (WorldRenderer draws it).
        }
        Instance* best = nullptr;
        std::optional<formats::MeshDecal> best_decal;
        const glm::vec3 o(origin[0], origin[1], origin[2]);
        for (Instance& inst : instances_) {
            const glm::mat4 m = glm::make_mat4(inst.transform.data());
            const glm::vec3 c(m * glm::vec4(glm::make_vec3(inst.model->center.data()), 1.0f));
            if (glm::length(o - c) > inst.model->radius + kReach) {
                continue;
            }
            const glm::mat4 inv = glm::inverse(m);
            const glm::vec3 lo(inv * glm::vec4(o, 1.0f));
            const glm::vec3 z = glm::normalize(glm::vec3(inv * glm::vec4(0, 0, 1, 0)));
            const glm::vec3 y = glm::normalize(glm::vec3(inv * glm::vec4(0, 1, 0, 0)));
            auto d = formats::project_mesh_decal(inst.model->positions, inst.model->triangle_indices,
                                                 {lo.x, lo.y, lo.z}, width, height, {z.x, z.y, z.z}, {y.x, y.y, y.z},
                                                 kReach);
            if (d && (!best_decal || d->distance < best_decal->distance)) {
                best = &inst;
                best_decal = std::move(d);
            }
        }
        if (!best) {
            ++stats_.unplaced_decals;
            continue;
        }
        // Models use the decal's $modelmaterial variant when it has one.
        const RenderMaterial& draw_mat = mat.model_material.empty() ? mat : materials_.material(mat.model_material);
        if (draw_mat.missing || draw_mat.modulate) {
            ++stats_.unplaced_decals; // DecalModulate has no prop pipeline yet.
            continue;
        }
        std::vector<Vertex> vertices;
        for (std::size_t v = 0; v < best_decal->positions.size(); ++v) {
            const auto& p = best_decal->positions[v];
            const auto& n = best_decal->normals[v];
            vertices.push_back({{p[0], p[1], p[2]}, {best_decal->uvs[v][0], best_decal->uvs[v][1]}, {n[0], n[1], n[2]}});
        }
        Decal decal;
        decal.vertices = device_.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                               std::as_bytes(std::span(vertices)));
        decal.indices = device_.create_buffer({best_decal->indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                              std::as_bytes(std::span(best_decal->indices)));
        decal.index_count = static_cast<std::uint32_t>(best_decal->indices.size());
        decal.material = &draw_mat;
        const rhi::Texture* textures[] = {draw_mat.base, draw_mat.base2, materials_.black_cube(), materials_.white()};
        decal.bind_group = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
        best->decals.push_back(std::move(decal));
        ++stats_.decals;
    }
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
            bool needs_normals = false;
            for (std::size_t b = 0; b < mdl.body_parts.size(); ++b) {
                const auto& part = mdl.body_parts[b];
                if (part.models.empty() || vtx.body_parts[b].models.empty() || vtx.body_parts[b].models[0].lods.empty()) {
                    continue;
                }
                const auto& mo = part.models[0]; // Default body group.
                const auto& lod = vtx.body_parts[b].models[0].lods[0];
                const auto vertex_base = static_cast<std::uint32_t>(vertices.size());
                needs_normals = needs_normals || std::any_of(mo.vertices.begin(), mo.vertices.end(),
                                                              [](const formats::MdlVertex& v) { return !v.has_normal; });
                for (const auto& v : mo.vertices) {
                    vertices.push_back({{v.position[0], v.position[1], v.position[2]},
                                        {v.uv[0], v.uv[1]},
                                        {v.normal[0], v.normal[1], v.normal[2]}});
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
                if (needs_normals) {
                    std::vector<std::array<float, 3>> positions;
                    positions.reserve(vertices.size());
                    for (const Vertex& v : vertices) {
                        positions.push_back({v.position[0], v.position[1], v.position[2]});
                    }
                    const auto normals = detail::smooth_normals(positions, indices);
                    for (std::size_t v = 0; v < vertices.size(); ++v) {
                        vertices[v].normal[0] = normals[v][0];
                        vertices[v].normal[1] = normals[v][1];
                        vertices[v].normal[2] = normals[v][2];
                    }
                    ++stats_.generated_normals;
                }
                model = std::make_unique<Model>();
                glm::vec3 lo(INFINITY), hi(-INFINITY);
                for (const Vertex& v : vertices) {
                    lo = glm::min(lo, glm::make_vec3(v.position));
                    hi = glm::max(hi, glm::make_vec3(v.position));
                }
                const glm::vec3 center = (lo + hi) * 0.5f;
                model->center = {center.x, center.y, center.z};
                model->radius = glm::length(hi - lo) * 0.5f;
                model->half_extent = {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f};
                model->positions.reserve(vertices.size());
                for (const Vertex& v : vertices) {
                    model->positions.push_back({v.position[0], v.position[1], v.position[2]});
                }
                model->triangle_indices = indices;
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
                        mesh.material = &materials_.material(detail::model_material_name(assets_, mdl, mdl.texture_for(r.material, f)));
                        if (mesh.material->skip) {
                            continue;
                        }
                        const RenderMaterial& mm = *mesh.material;
                        const rhi::Texture* envmap = mm.envmap ? mm.envmap : materials_.black_cube();
                        const rhi::Texture* mask = mm.envmap_mask ? mm.envmap_mask : materials_.white();
                        const rhi::Texture* textures[] = {mm.base, mm.base2, envmap, mask};
                        mesh.bind_group = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
                        for (const rhi::Texture* frame : mm.base_frames) {
                            const rhi::Texture* frame_textures[] = {frame, mm.multiply_layers ? mm.base2 : frame, envmap, mask};
                            mesh.frame_bind_groups.push_back(
                                device_.create_bind_group(*pipelines_[0][0], *sampler_, frame_textures));
                        }
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

std::size_t PropRenderer::build_shader_data(const PropDrawSettings& settings) {
    static const std::vector<MaterialParams> kDefaults{MaterialParams{}};
    const std::vector<MaterialParams>& params = settings.material_params ? *settings.material_params : kDefaults;
    // Shader data: the fog (2 float4), the eye (w: where the envmap blocks
    // start), the material params table, then each envmapped instance's
    // model-to-world rows (3 float4).
    const auto fog = settings.fog.rows();
    envmap_blocks_.assign(instances_.size(), UINT32_MAX);
    std::vector<float> blocks;
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        const Instance& inst = instances_[i];
        if (!settings.envmaps || !inst.envmapped || inst.skybox != settings.skybox || blocks.size() / 12 >= 0xFFFF) {
            continue;
        }
        envmap_blocks_[i] = static_cast<std::uint32_t>(blocks.size() / 12);
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t c = 0; c < 4; ++c) {
                blocks.push_back(inst.transform[c * 4 + r]); // Column-major.
            }
        }
    }
    constexpr std::size_t kHeadFloats = 12;
    const std::size_t room = rhi::kMaxShaderDataSize / sizeof(float) - kHeadFloats - blocks.size();
    const std::size_t rows =
        std::min({params.size(), room / (sizeof(MaterialParams) / sizeof(float)), std::size_t{0xFFFF}});
    shader_data_.assign(fog.begin(), fog.end());
    const std::size_t blocks_start = kHeadFloats / 4 + rows * sizeof(MaterialParams) / 16;
    shader_data_.insert(shader_data_.end(),
                        {settings.eye[0], settings.eye[1], settings.eye[2], static_cast<float>(blocks_start)});
    const auto* first = reinterpret_cast<const float*>(params.data());
    shader_data_.insert(shader_data_.end(), first, first + rows * sizeof(MaterialParams) / sizeof(float));
    shader_data_.insert(shader_data_.end(), blocks.begin(), blocks.end());
    return rows;
}

void PropRenderer::draw_instance(rhi::CommandList& cmd, const std::array<float, 16>& vp,
                                 const PropDrawSettings& settings, std::size_t rows, std::size_t index,
                                 bool translucent, Bound& bound, const rhi::ShaderDataRef* data) {
    const Instance& inst = instances_[index];
    if (inst.skybox != settings.skybox || inst.alpha <= 0.0f) {
        return;
    }
    const bool fading = inst.alpha < 1.0f;
    if (fading && !translucent) {
        return; // Drawn whole with the translucent things.
    }
    const std::uint32_t block = envmap_blocks_[index];
    auto bind = [&](const rhi::Pipeline& pipeline) {
        if (&pipeline != bound.pipeline) {
            cmd.set_pipeline(pipeline);
            if (data) {
                cmd.bind_shader_data(*data);
            } else {
                cmd.set_shader_data(shader_data_.data(), shader_data_.size() * sizeof(float));
            }
            bound.pipeline = &pipeline;
        }
    };
    PushConstants pc{};
    const glm::mat4 mvp = glm::make_mat4(vp.data()) * glm::make_mat4(inst.transform.data());
    std::memcpy(pc.mvp, glm::value_ptr(mvp), sizeof(pc.mvp));
    for (std::size_t k = 0; k < 4; ++k) {
        pc.model_z[k] = inst.transform[k * 4 + 2]; // Column-major: row 2.
    }
    for (std::size_t h = 0; h < 18; ++h) {
        const std::uint32_t half = detail::to_half(inst.lighting.color[h / 3][h % 3] * settings.light_scale);
        pc.cube[h / 2] = (h % 2) ? ((pc.cube[h / 2] & 0xFFFFu) | (half << 16)) : half;
    }
    const std::uint32_t fade_bits =
        fading ? (255u - static_cast<std::uint32_t>(inst.alpha * 255.0f)) << kFadeShift : 0u;
    for (const Mesh& mesh : inst.model->skins[inst.skin]) {
        const RenderMaterial& m = *mesh.material;
        if (m.translucent != translucent && !fading) {
            continue;
        }
        // A fading prop's opaque meshes blend by the fade, without writing depth.
        bind(m.translucent || !fading ? pipeline_for(m) : *pipelines_[1][m.no_cull ? 1 : 0]);
        if (inst.model != bound.model) {
            cmd.set_vertex_buffer(*inst.model->vertices);
            cmd.set_index_buffer(*inst.model->indices);
            bound.model = inst.model;
        }
        pc.alpha_ref = m.alpha_ref;
        pc.flags = (m.alpha_test ? kAlphaTest : 0) | ((m.unlit || settings.fullbright) ? kUnlit : 0) |
                   (m.self_illum ? kSelfIllum : 0) | (m.translucent ? kTranslucent : 0) |
                   (m.multiply_layers ? kMultiplyLayers : 0) | (m.additive ? kAdditive : 0) | fade_bits;
        pc.params_index = m.params_index < rows ? m.params_index : 0;
        const rhi::BindGroup* group = mesh.frame_bind_groups.empty()
                                          ? mesh.bind_group.get()
                                          : mesh.frame_bind_groups[m.base_frame % mesh.frame_bind_groups.size()].get();
        if (m.envmap && block != UINT32_MAX) {
            pc.flags |= kEnvmap | (static_cast<std::uint32_t>(m.envmap_mask_mode) << kEnvmapMaskShift);
            pc.params_index |= block << 16;
            if (m.envmap_nearest && inst.cubemap && inst.cubemap != m.envmap) {
                auto& g = mesh.cubemap_groups[inst.cubemap];
                if (!g) {
                    const rhi::Texture* textures[] = {m.base, m.base2, inst.cubemap, m.envmap_mask};
                    g = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
                }
                group = g.get();
            }
        }
        cmd.set_bind_group(*group);
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(mesh.index_count, mesh.first_index);
    }
    if (!translucent) {
        return;
    }
    // Decals on this prop: alpha-blended over it, lit like it.
    for (const Decal& decal : inst.decals) {
        const RenderMaterial& m = *decal.material;
        bind(*pipelines_[1][0]);
        cmd.set_vertex_buffer(*decal.vertices);
        cmd.set_index_buffer(*decal.indices);
        bound.model = nullptr;
        pc.alpha_ref = m.alpha_ref;
        pc.flags = (m.alpha_test ? kAlphaTest : 0) | ((m.unlit || settings.fullbright) ? kUnlit : 0) |
                   (m.self_illum ? kSelfIllum : 0) | kTranslucent | fade_bits;
        pc.params_index = m.params_index < rows ? m.params_index : 0;
        cmd.set_bind_group(*decal.bind_group);
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(decal.index_count, 0);
    }
}

void PropRenderer::update_fades(const std::array<float, 3>& eye) {
    for (Instance& inst : instances_) {
        if (!inst.fade) {
            continue;
        }
        // Measured to the prop's origin (engine.dll takes its render origin).
        const auto& o = inst.fade->origin;
        const float dx = o[0] - eye[0], dy = o[1] - eye[1], dz = o[2] - eye[2];
        inst.alpha = formats::static_prop_fade(*inst.fade, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
}

void PropRenderer::draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                               const PropDrawSettings& settings) {
    const std::size_t rows = build_shader_data(settings);
    Bound bound;
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        draw_instance(cmd, view_projection, settings, rows, i, false, bound, nullptr);
    }
}

void PropRenderer::collect_translucent(const formats::BspDrawOrder& order, const PropDrawSettings& settings,
                                       std::uint32_t source, std::vector<TranslucentItem>& out) const {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        const Instance& inst = instances_[i];
        if (inst.skybox != settings.skybox || inst.alpha <= 0.0f ||
            !(inst.has_translucent || !inst.decals.empty() || inst.alpha < 1.0f)) {
            continue;
        }
        TranslucentItem item;
        item.slot = order.nearest_slot(inst.leaves);
        const float dx = inst.transform[12] - settings.eye[0], dy = inst.transform[13] - settings.eye[1],
                    dz = inst.transform[14] - settings.eye[2];
        item.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        item.source = source;
        item.index = static_cast<std::uint32_t>(i);
        out.push_back(item);
    }
}

void PropRenderer::begin_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                     const PropDrawSettings& settings) {
    pass_.view_projection = view_projection;
    pass_.settings = settings;
    pass_.rows = build_shader_data(settings);
    pass_.bound = {};
    cmd.set_pipeline(*pipelines_[1][0]);
    pass_.data = cmd.set_shader_data(shader_data_.data(), shader_data_.size() * sizeof(float));
}

void PropRenderer::draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index, bool rebind) {
    if (index >= instances_.size()) {
        return;
    }
    if (rebind) {
        pass_.bound = {};
    }
    draw_instance(cmd, pass_.view_projection, pass_.settings, pass_.rows, index, true, pass_.bound, &pass_.data);
}

} // namespace openbl::render
