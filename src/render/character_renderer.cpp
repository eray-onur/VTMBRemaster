#include "openbl/render/character_renderer.hpp"

#include "openbl/anim/flex.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/vtx.hpp"

#include "mesh_util.hpp"
#include "model_materials.hpp"
#include "openbl_skinned_ps.h"
#include "openbl_skinned_vs.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace openbl::render {

namespace {

struct Vertex {
    float position[3];
    float uv[2];
    float normal[3];
    std::uint16_t bones[4];
    std::uint8_t weights[4];
    float flex_slot; // Index of this vertex's flex offset, or -1 if no flex moves it.
};
static_assert(sizeof(Vertex) == 48);

struct PushConstants {
    float mvp[16];
    float alpha_ref;
    std::uint32_t flags;
    std::uint32_t params_row; // Shader-data element where this mesh's material params start.
    std::uint32_t flex_base;  // Shader-data element of flex slot 0; 0 when no flex is active.
    float model_z[4];         // World z row of the model transform (water fog).
};
static_assert(sizeof(PushConstants) == 96);

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kUnlit = 4;
constexpr std::uint32_t kSelfIllum = 8;
constexpr std::uint32_t kTranslucent = 16;
constexpr std::uint32_t kEyes = 32;
constexpr std::uint32_t kAdditive = 128;

// Height above an NPC's origin at which its lighting is sampled (roughly chest height).
constexpr float kLightingHeight = 40.0f;

formats::Vec3 parse_vec3(const formats::BspEntity& e, const char* key) {
    formats::Vec3 v{};
    if (auto it = e.find(key); it != e.end()) {
        std::istringstream(it->second) >> v[0] >> v[1] >> v[2];
    }
    return v;
}

} // namespace

std::shared_ptr<const formats::MdlFile> ModelLibrary::load(const std::string& path) {
    if (auto it = files_.find(path); it != files_.end()) {
        return it->second;
    }
    std::shared_ptr<const formats::MdlFile> file;
    if (auto data = assets_.read(path)) {
        try {
            file = std::make_shared<const formats::MdlFile>(formats::parse_mdl(*data));
        } catch (const formats::FormatError& e) {
            std::fprintf(stderr, "model %s: %s\n", path.c_str(), e.what());
        }
    }
    files_.emplace(path, file);
    return file;
}

CharacterRenderer::CharacterRenderer(rhi::Device& device, MaterialSystem& materials, const AssetSource& assets,
                                     const formats::BspFile& bsp, const CharacterOptions& options)
    : device_(device), materials_(materials), assets_(assets), library_(assets) {
    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_skinned_vs;
    pd.pixel.spirv = openbl_skinned_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0},
                     {1, rhi::Format::RG32_Float, 12},
                     {2, rhi::Format::RGB32_Float, 20},
                     {3, rhi::Format::RGBA16_UInt, 32},
                     {4, rhi::Format::RGBA8_UNorm, 40},
                     {5, rhi::Format::R32_Float, 44}};
    pd.texture_count = 2;
    pd.push_constant_size = sizeof(PushConstants);
    pd.shader_data = true;
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
    std::uint32_t seed = 12345;
    for (const auto& e : bsp.entities) {
        auto cls = e.find("classname");
        auto model_key = e.find("model");
        if (cls == e.end() || model_key == e.end() || !cls->second.starts_with("npc_")) {
            continue;
        }
        if (auto hidden = e.find("StartHidden"); hidden != e.end() && hidden->second == "1") {
            const auto name = e.find("targetname");
            if (name == e.end() || std::find(options.shown.begin(), options.shown.end(), name->second) == options.shown.end()) {
                continue; // Invisible until a script shows it.
            }
        }
        const std::string path = formats::normalize_game_path(model_key->second);
        if (!path.ends_with(".mdl")) {
            continue;
        }
        const Model* model = load_model(path);
        if (!model) {
            continue;
        }
        Instance inst;
        inst.model = model;
        if (auto s = e.find("skin"); s != e.end()) {
            inst.skin = static_cast<std::size_t>(std::max(0, std::atoi(s->second.c_str())));
        }
        inst.skin = inst.skin < model->skins.size() ? inst.skin : 0;
        const auto origin = parse_vec3(e, "origin");
        const auto angles = parse_vec3(e, "angles");
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(origin[0], origin[1], origin[2]));
        m = glm::rotate(m, glm::radians(angles[1]), glm::vec3(0, 0, 1));
        std::memcpy(inst.transform.data(), glm::value_ptr(m), sizeof(inst.transform));
        std::array<std::array<float, 3>, 3> rotation{};
        for (int r = 0; r < 3; ++r) {
            for (int k = 0; k < 3; ++k) {
                rotation[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] = m[k][r];
            }
        }
        inst.lighting = lighting.sample({origin[0], origin[1], origin[2] + kLightingHeight}, rotation);

        // The requested sequence, else the idle: the first ACT_IDLE sequence,
        // else any sequence named like an idle.
        std::optional<ModelLibrary::SequenceRef> found;
        if (!options.sequence.empty()) {
            found = library_.find_sequence(path, [&](const formats::MdlSequence& s) {
                return formats::iequals(s.label, options.sequence) || formats::iequals(s.activity_name, options.sequence);
            });
        }
        if (!found) {
            found = library_.find_sequence(path, [](const formats::MdlSequence& s) {
                return formats::iequals(s.activity_name, "ACT_IDLE");
            });
        }
        if (!found) {
            found = library_.find_sequence(path, [](const formats::MdlSequence& s) {
                std::string label = formats::normalize_game_path(s.label);
                return label.find("idle") != std::string::npos;
            });
        }
        if (!options.flex_controllers.empty() && !model->flexes.empty()) {
            const auto& mdl = *model->mdl;
            auto controllers = anim::default_flex_controllers(mdl);
            for (const auto& [name, value] : options.flex_controllers) {
                anim::set_flex_controller(mdl, controllers, name, value);
            }
            const auto weights = anim::evaluate_flex_rules(mdl, controllers);
            inst.flex_deltas.assign(std::size_t{model->flex_slots} * 4, 0.0f);
            bool any = false;
            for (const ModelFlex& f : model->flexes) {
                const float w = f.flex >= 0 && static_cast<std::size_t>(f.flex) < weights.size()
                                    ? weights[static_cast<std::size_t>(f.flex)]
                                    : 0.0f;
                const float scale = anim::mesh_flex_scale(f.targets, w);
                if (scale == 0.0f) {
                    continue;
                }
                any = true;
                for (const auto& [slot, delta] : f.deltas) {
                    for (std::size_t k = 0; k < 3; ++k) {
                        inst.flex_deltas[std::size_t{slot} * 4 + k] += scale * delta[k];
                    }
                }
            }
            if (!any) {
                inst.flex_deltas.clear();
            }
        }
        seed = seed * 1664525u + 1013904223u;
        if (found) {
            inst.player.emplace(model->mdl->bones);
            for (const auto& [name, value] : options.pose_parameters) {
                inst.player->set_pose_parameter(name, value);
            }
            // Start part-way through so crowds do not move in lockstep.
            inst.player->play(found->file, found->sequence, 0.0f, static_cast<float>(seed % 1000) / 1000.0f);
        } else {
            ++stats_.without_sequence;
        }
        inst.skinning = formats::mdl_skinning_matrices(
            model->mdl->bones, formats::mdl_world_transforms(model->mdl->bones, formats::mdl_bind_pose(model->mdl->bones)));
        instances_.push_back(std::move(inst));
    }
    stats_.instances = instances_.size();
}

CharacterRenderer::~CharacterRenderer() = default;

const CharacterRenderer::Model* CharacterRenderer::load_model(const std::string& path) {
    if (auto it = models_.find(path); it != models_.end()) {
        return it->second.get();
    }
    std::unique_ptr<Model> model;
    try {
        auto mdl = library_.load(path);
        auto vtx_data = assets_.read(path.substr(0, path.size() - 4) + ".dx80.vtx");
        if (mdl && vtx_data) {
            const auto vtx = formats::parse_vtx(*vtx_data);
            if (mdl->checksum != vtx.checksum || vtx.body_parts.size() != mdl->body_parts.size()) {
                throw formats::FormatError("mdl/vtx mismatch");
            }
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;
            struct MeshRange {
                std::uint32_t first, count;
                std::int32_t material;
            };
            std::vector<MeshRange> ranges;
            std::vector<ModelFlex> flexes;
            std::uint32_t flex_slots = 0;
            bool needs_normals = false;
            for (std::size_t b = 0; b < mdl->body_parts.size(); ++b) {
                const auto& part = mdl->body_parts[b];
                if (part.models.empty() || vtx.body_parts[b].models.empty() || vtx.body_parts[b].models[0].lods.empty()) {
                    continue;
                }
                const auto& mo = part.models[0];
                const auto& lod = vtx.body_parts[b].models[0].lods[0];
                const auto base = static_cast<std::uint32_t>(vertices.size());
                needs_normals = needs_normals || std::any_of(mo.vertices.begin(), mo.vertices.end(),
                                                              [](const formats::MdlVertex& v) { return !v.has_normal; });
                for (const auto& v : mo.vertices) {
                    Vertex out{};
                    out.flex_slot = -1.0f;
                    std::memcpy(out.position, v.position.data(), sizeof(out.position));
                    std::memcpy(out.uv, v.uv.data(), sizeof(out.uv));
                    std::memcpy(out.normal, v.normal.data(), sizeof(out.normal));
                    for (std::size_t k = 0; k < 3; ++k) {
                        out.bones[k] = v.bones[k] < mdl->bones.size() ? v.bones[k] : 0;
                        out.weights[k] = v.weights[k];
                    }
                    vertices.push_back(out);
                }
                // Flexes: give every vertex a flex moves a slot, and keep its offsets by slot.
                for (const auto& mesh : mo.meshes) {
                    for (const auto& f : mesh.flexes) {
                        ModelFlex flex{f.flex, f.targets, {}};
                        for (const auto& fv : f.vertices) {
                            const std::size_t global = base + mesh.vertex_offset + fv.index;
                            if (global >= vertices.size()) {
                                continue;
                            }
                            if (vertices[global].flex_slot < 0) {
                                vertices[global].flex_slot = static_cast<float>(flex_slots++);
                            }
                            flex.deltas.emplace_back(static_cast<std::uint32_t>(vertices[global].flex_slot), fv.delta);
                        }
                        flexes.push_back(std::move(flex));
                    }
                }
                for (std::size_t me = 0; me < mo.meshes.size() && me < lod.meshes.size(); ++me) {
                    const auto tris = lod.meshes[me].triangles();
                    if (tris.empty()) {
                        continue;
                    }
                    ranges.push_back({static_cast<std::uint32_t>(indices.size()), static_cast<std::uint32_t>(tris.size()),
                                      mo.meshes[me].material});
                    for (std::uint32_t t : tris) {
                        indices.push_back(base + mo.meshes[me].vertex_offset + t);
                    }
                }
            }
            if (!indices.empty()) {
                if (needs_normals) {
                    std::vector<std::array<float, 3>> positions;
                    for (const Vertex& v : vertices) {
                        positions.push_back({v.position[0], v.position[1], v.position[2]});
                    }
                    const auto normals = detail::smooth_normals(positions, indices);
                    for (std::size_t v = 0; v < vertices.size(); ++v) {
                        std::memcpy(vertices[v].normal, normals[v].data(), sizeof(vertices[v].normal));
                    }
                }
                model = std::make_unique<Model>();
                model->mdl = mdl;
                model->flexes = std::move(flexes);
                model->flex_slots = flex_slots;
                model->vertices = device_.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                                        std::as_bytes(std::span(vertices)));
                model->indices = device_.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                                       std::as_bytes(std::span(indices)));
                const std::size_t families = std::max<std::size_t>(1, mdl->skin_families.size());
                model->skins.resize(families);
                for (std::size_t f = 0; f < families; ++f) {
                    for (const MeshRange& r : ranges) {
                        Mesh mesh;
                        mesh.first_index = r.first;
                        mesh.index_count = r.count;
                        mesh.material =
                            &materials_.material(detail::model_material_name(assets_, *mdl, mdl->texture_for(r.material, f)));
                        if (mesh.material->skip) {
                            continue;
                        }
                        const rhi::Texture* textures[] = {mesh.material->base, mesh.material->base2};
                        mesh.bind_group = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
                        for (const rhi::Texture* frame : mesh.material->base_frames) {
                            const rhi::Texture* frame_textures[] = {frame, mesh.material->eyes ? mesh.material->base2 : frame};
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
        std::fprintf(stderr, "character %s: %s\n", path.c_str(), e.what());
        model.reset();
    }
    if (!model) {
        ++stats_.missing_models;
    }
    return models_.emplace(path, std::move(model)).first->second.get();
}

void CharacterRenderer::update(float time) {
    for (Instance& inst : instances_) {
        if (!inst.player) {
            continue;
        }
        const auto& bones = inst.model->mdl->bones;
        try {
            inst.skinning = formats::mdl_skinning_matrices(bones, formats::mdl_world_transforms(bones, inst.player->pose(time)));
        } catch (const formats::FormatError& e) {
            std::fprintf(stderr, "animation: %s\n", e.what());
            inst.player.reset();
        }
    }
}

void CharacterRenderer::draw_pass(rhi::CommandList& cmd, const std::array<float, 16>& vp,
                                  const CharacterDrawSettings& settings, bool translucent) {
    const glm::mat4 view_projection = glm::make_mat4(vp.data());
    std::vector<float> data;
    for (const Instance& inst : instances_) {
        bool any = false;
        for (const Mesh& mesh : inst.model->skins[inst.skin]) {
            any = any || mesh.material->translucent == translucent;
        }
        if (!any) {
            continue;
        }
        // Shader data: ambient cube (6 float4), then 3 float4 rows per bone,
        // then the material params of each mesh drawn in this pass.
        data.assign(8 * 4 + inst.skinning.size() * 12, 0.0f);
        const auto fog = settings.fog.rows(); // Elements 6 and 7.
        std::copy(fog.begin(), fog.end(), data.begin() + 24);
        for (std::size_t f = 0; f < 6; ++f) {
            for (std::size_t c = 0; c < 3; ++c) {
                data[f * 4 + c] = inst.lighting.color[f][c] * settings.light_scale;
            }
        }
        for (std::size_t b = 0; b < inst.skinning.size(); ++b) {
            std::memcpy(&data[32 + b * 12], inst.skinning[b].data(), 12 * sizeof(float));
        }
        std::vector<std::uint32_t> params_rows;
        for (const Mesh& mesh : inst.model->skins[inst.skin]) {
            const std::uint32_t index = mesh.material->params_index;
            const MaterialParams p = settings.material_params && index < settings.material_params->size()
                                         ? (*settings.material_params)[index]
                                         : MaterialParams{};
            params_rows.push_back(static_cast<std::uint32_t>(data.size() / 4));
            const auto* f = reinterpret_cast<const float*>(&p);
            data.insert(data.end(), f, f + sizeof(p) / sizeof(float));
        }
        std::uint32_t flex_base = 0;
        if (!inst.flex_deltas.empty() &&
            (data.size() + inst.flex_deltas.size()) * sizeof(float) <= rhi::kMaxShaderDataSize) {
            flex_base = static_cast<std::uint32_t>(data.size() / 4);
            data.insert(data.end(), inst.flex_deltas.begin(), inst.flex_deltas.end());
        }
        PushConstants pc{};
        pc.flex_base = flex_base;
        const glm::mat4 mvp = view_projection * glm::make_mat4(inst.transform.data());
        std::memcpy(pc.mvp, glm::value_ptr(mvp), sizeof(pc.mvp));
        for (std::size_t k = 0; k < 4; ++k) {
            pc.model_z[k] = inst.transform[k * 4 + 2]; // Column-major: row 2.
        }

        const rhi::Pipeline* bound = nullptr;
        std::size_t mesh_index = 0;
        for (const Mesh& mesh : inst.model->skins[inst.skin]) {
            const RenderMaterial& m = *mesh.material;
            pc.params_row = params_rows[mesh_index++];
            if (m.translucent != translucent) {
                continue;
            }
            const std::size_t blend_mode = !m.translucent ? 0 : (m.additive ? 2 : 1);
            const rhi::Pipeline& pipeline = *pipelines_[blend_mode][m.no_cull ? 1 : 0];
            if (&pipeline != bound) {
                cmd.set_pipeline(pipeline);
                cmd.set_vertex_buffer(*inst.model->vertices);
                cmd.set_index_buffer(*inst.model->indices);
                cmd.set_shader_data(data.data(), data.size() * sizeof(float));
                bound = &pipeline;
            }
            pc.alpha_ref = m.alpha_ref;
            pc.flags = (m.alpha_test ? kAlphaTest : 0) | ((m.unlit || settings.fullbright) ? kUnlit : 0) |
                       (m.self_illum ? kSelfIllum : 0) | (m.translucent ? kTranslucent : 0) | (m.eyes ? kEyes : 0) |
                       (m.additive ? kAdditive : 0);
            cmd.set_bind_group(mesh.frame_bind_groups.empty()
                                   ? *mesh.bind_group
                                   : *mesh.frame_bind_groups[m.base_frame % mesh.frame_bind_groups.size()]);
            cmd.push_constants(&pc, sizeof(pc));
            cmd.draw_indexed(mesh.index_count, mesh.first_index);
        }
    }
}

void CharacterRenderer::draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                    const CharacterDrawSettings& settings) {
    draw_pass(cmd, view_projection, settings, false);
}

void CharacterRenderer::draw_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                         const CharacterDrawSettings& settings) {
    draw_pass(cmd, view_projection, settings, true);
}

} // namespace openbl::render
