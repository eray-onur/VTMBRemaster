#include "openbl/render/character_renderer.hpp"

#include "openbl/anim/flex.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/vtx.hpp"

#include "mesh_util.hpp"
#include "model_materials.hpp"
#include "openbl/formats/draw_order.hpp"
#include "openbl_shadow_ps.h"
#include "openbl_shadow_vs.h"
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
    // Cloth: ±(particle slot + 1), negative where the vertex faces against its
    // particle's triangle normal (the back of double-sided cards); 0: skinned.
    float cloth;
    float tangent[4]; // Texture u axis, handedness (for $bumpmap); zero if the model has none.
};
static_assert(sizeof(Vertex) == 68);

struct PushConstants {
    float mvp[16];
    float alpha_ref;
    std::uint32_t flags;
    std::uint32_t params_row; // Shader-data element where this mesh's material params start.
    std::uint32_t flex_base;  // Shader-data element of flex slot 0; 0 when no flex is active.
    float model_z[4];         // World z row of the model transform (water fog).
    std::uint32_t cloth_base;  // Shader-data element of cloth particle 0's position; 0: no cloth.
    std::uint32_t cloth_count; // Particles; their normals follow the positions.
    std::uint32_t envmap_block; // Shader-data element of the eye, then the model-to-world rows; 0: none.
    std::uint32_t lights_block; // Shader-data element of the two local lights (8 float4); 0: none.
};
static_assert(sizeof(PushConstants) == 112);

// A bind-pose vertex moved by the current skinning matrices.
anim::Vec3 skin(const std::vector<formats::MdlMatrix>& skinning, const formats::MdlVertex& v) {
    anim::Vec3 out{};
    for (std::size_t k = 0; k < 3; ++k) {
        if (v.weights[k] == 0 || v.bones[k] >= skinning.size()) {
            continue;
        }
        const auto& m = skinning[v.bones[k]];
        const float w = static_cast<float>(v.weights[k]) / 255.0f;
        for (std::size_t r = 0; r < 3; ++r) {
            out[r] += w * (m[r][0] * v.position[0] + m[r][1] * v.position[1] + m[r][2] * v.position[2] + m[r][3]);
        }
    }
    return out;
}

anim::Vec3 transform(const formats::MdlMatrix& m, const anim::Vec3& p) {
    return {m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
            m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
            m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3]};
}

// Simulated time a character's cloth gets on its first frame, so it starts
// settled rather than in the bind pose.
constexpr float kClothSettleTime = 1.0f;

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kUnlit = 4;
constexpr std::uint32_t kSelfIllum = 8;
constexpr std::uint32_t kTranslucent = 16;
constexpr std::uint32_t kEyes = 32;
constexpr std::uint32_t kAdditive = 128;
constexpr std::uint32_t kEnvmap = 256;
constexpr std::uint32_t kEnvmapMaskShift = 9;
constexpr std::uint32_t kBump = 2048;
constexpr std::uint32_t kDetail = 8192; // $detail.
constexpr std::uint32_t kShadowBuild = 16384;

// Height above an NPC's origin at which its lighting is sampled (roughly chest height).
constexpr float kLightingHeight = 40.0f;

// Shadows (docs/RENDERING.md#character-shadows). A character casts one, away
// from its brightest light, if that light arrives with at least this
// luminance (in lightmap units). In the original, sm_hub_1's barrel fire
// casts the shadow of a bum it reaches with 0.55 and the street lights those
// of characters below them (2 to 8); in the sp_tutorial_1 office, where no
// light reaches the player with more than about 0.1, there is none.
constexpr float kShadowMinLight = 0.25f;
constexpr float kShadowRadius = 44.0f;     // Half the silhouette's square, in world units.
constexpr float kShadowFade = 150.0f;      // How far past the character the shadow fades out.
constexpr std::uint32_t kShadowSize = 128; // Silhouette texels per side.
constexpr float kShadowLift = 0.25f;       // Receivers are drawn this far off the surface.
constexpr float kCharacterCentre = 36.0f;  // Above the origin; the standing box is 32 x 32 x 72.
constexpr float kShadowMaxTilt = 0.349066f; // From straight down, in radians (20 degrees).

struct ShadowPushConstants {
    float mvp[16];
    float rows[16]; // shadow_u, shadow_v, along, extent (shaders/shadow.hlsl).
};

float luminance(const std::array<float, 3>& c) {
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

// World triangles a shadow can fall on: those of the world faces in the
// leaves overlapping the box, and of displacements, that are drawn lit and
// opaque and face against `direction`. Lifted off their surfaces so they draw
// over them.
std::vector<float> shadow_receivers(const formats::BspFile& bsp, MaterialSystem& materials, const formats::Vec3& mins,
                                    const formats::Vec3& maxs, const formats::Vec3& direction) {
    constexpr std::int32_t kSurfSky = 0x4, kSurfWarp = 0x8, kSurfTrans = 0x10, kSurfNoDraw = 0x80;
    std::vector<std::size_t> faces;
    for (const std::uint32_t leaf : formats::leaves_in_box(bsp, mins, maxs)) {
        const auto& l = bsp.leafs[leaf];
        for (std::uint32_t i = 0; i < l.num_leaf_faces; ++i) {
            const std::size_t k = std::size_t{l.first_leaf_face} + i;
            if (k < bsp.leaf_faces.size()) {
                faces.push_back(bsp.leaf_faces[k]);
            }
        }
    }
    for (std::size_t f = 0; f < bsp.faces.size(); ++f) {
        if (bsp.faces[f].dispinfo >= 0) {
            faces.push_back(f);
        }
    }
    std::sort(faces.begin(), faces.end());
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
    std::vector<float> out;
    for (const std::size_t f : faces) {
        if (f >= bsp.faces.size()) {
            continue;
        }
        const formats::BspFace& face = bsp.faces[f];
        if (face.texinfo >= bsp.texinfos.size() || face.plane >= bsp.planes.size()) {
            continue;
        }
        const formats::BspTexInfo& ti = bsp.texinfos[face.texinfo];
        if ((ti.flags & (kSurfSky | kSurfWarp | kSurfTrans | kSurfNoDraw)) != 0 || ti.texdata < 0 ||
            static_cast<std::size_t>(ti.texdata) >= bsp.texdatas.size()) {
            continue;
        }
        const RenderMaterial& m = materials.material(bsp.texdatas[static_cast<std::size_t>(ti.texdata)].material);
        if (m.skip || m.translucent || m.water || m.unlit || m.alpha_test) {
            continue;
        }
        formats::Vec3 plane_normal = bsp.planes[face.plane].normal;
        if (face.side != 0) {
            plane_normal = {-plane_normal[0], -plane_normal[1], -plane_normal[2]};
        }
        const formats::BspFaceMesh mesh = formats::build_face_mesh(bsp, f);
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const auto& a = mesh.positions[mesh.indices[t]];
            const auto& b = mesh.positions[mesh.indices[t + 1]];
            const auto& c = mesh.positions[mesh.indices[t + 2]];
            bool overlaps = true;
            for (std::size_t k = 0; k < 3; ++k) {
                const float lo = std::min({a[k], b[k], c[k]}), hi = std::max({a[k], b[k], c[k]});
                overlaps = overlaps && hi >= mins[k] && lo <= maxs[k];
            }
            if (!overlaps) {
                continue;
            }
            const glm::vec3 ga(a[0], a[1], a[2]), gb(b[0], b[1], b[2]), gc(c[0], c[1], c[2]);
            glm::vec3 n = glm::cross(gb - ga, gc - ga);
            const float len = glm::length(n);
            if (len < 1e-6f) {
                continue;
            }
            n /= len;
            if (glm::dot(n, glm::vec3(plane_normal[0], plane_normal[1], plane_normal[2])) < 0.0f) {
                n = -n;
            }
            if (glm::dot(n, glm::vec3(direction[0], direction[1], direction[2])) > -0.05f) {
                continue; // Facing away from the light, or edge-on.
            }
            for (const glm::vec3& p : {ga, gb, gc}) {
                const glm::vec3 lifted = p + n * kShadowLift;
                out.insert(out.end(), {lifted.x, lifted.y, lifted.z});
            }
        }
    }
    return out;
}

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
                     {5, rhi::Format::R32_Float, 44},
                     {6, rhi::Format::R32_Float, 48},
                     {7, rhi::Format::RGBA32_Float, 52}};
    pd.texture_count = 6; // Base, iris, envmap (cube), envmap mask, bump map, detail.
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

    rhi::PipelineDesc sd;
    sd.vertex.spirv = openbl_shadow_vs;
    sd.pixel.spirv = openbl_shadow_ps;
    sd.vertex_stride = 3 * sizeof(float);
    sd.attributes = {{0, rhi::Format::RGB32_Float, 0}};
    sd.texture_count = 1;
    sd.push_constant_size = sizeof(ShadowPushConstants);
    sd.shader_data = true;
    sd.cull = rhi::CullMode::None;
    sd.depth_write = false;
    sd.blend = rhi::BlendMode::Modulate;
    shadow_pipeline_ = device.create_pipeline(sd);
    rhi::SamplerDesc clamped;
    clamped.address = rhi::AddressMode::Clamp;
    clamped.max_anisotropy = 1.0f;
    shadow_sampler_ = device.create_sampler(clamped);

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
            inst.flex_deltas.assign(std::size_t{model->flex_slots} * 8, 0.0f);
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
                for (const ModelFlex::Delta& d : f.deltas) {
                    for (std::size_t k = 0; k < 3; ++k) {
                        inst.flex_deltas[std::size_t{d.slot} * 8 + k] += scale * d.position[k];
                        inst.flex_deltas[std::size_t{d.slot} * 8 + 4 + k] += scale * d.normal[k];
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
        for (const ClothPiece& piece : model->cloth) {
            if (!options.cloth) {
                break;
            }
            std::vector<anim::Vec3> skinned;
            for (const auto& v : piece.particles) {
                skinned.push_back(skin(inst.skinning, v));
            }
            inst.cloth.emplace_back(*piece.cloth, skinned);
        }
        stats_.with_cloth += !inst.cloth.empty();
        for (const Mesh& mesh : model->skins[inst.skin]) {
            inst.envmapped = inst.envmapped || mesh.material->envmap;
            inst.has_translucent = inst.has_translucent || mesh.material->translucent;
        }
        stats_.with_envmap += inst.envmapped;
        // A person-sized box at the origin, for the leaves it stands in.
        inst.leaves = formats::leaves_in_box(bsp, {origin[0] - 16, origin[1] - 16, origin[2]},
                                             {origin[0] + 16, origin[1] + 16, origin[2] + 72});
        // Generous, for animation and cloth reaching out of the standing box.
        inst.mins = {origin[0] - 48, origin[1] - 48, origin[2] - 16};
        inst.maxs = {origin[0] + 48, origin[1] + 48, origin[2] + 96};
        if (inst.envmapped) {
            inst.cubemap = materials.nearest_cubemap({origin[0], origin[1], origin[2] + kLightingHeight});
        }
        if (options.shadows) {
            create_shadow(inst, bsp, origin);
            stats_.with_shadow += inst.shadow != nullptr;
        }
        instances_.push_back(std::move(inst));
    }
    stats_.instances = instances_.size();
}

CharacterRenderer::~CharacterRenderer() = default;

void CharacterRenderer::create_shadow(Instance& inst, const formats::BspFile& bsp, const formats::Vec3& origin) {
    // The local light that brings the most to the character.
    const std::size_t brightest =
        luminance(inst.lighting.arriving[1]) > luminance(inst.lighting.arriving[0]) ? 1 : 0;
    const LocalLight& light = inst.lighting.lights[brightest];
    if (light.type == LocalLight::Type::None || luminance(inst.lighting.arriving[brightest]) < kShadowMinLight) {
        return;
    }
    // Away from the light, but always downwards: from the light's position
    // through the character's centre (or the way a directional light
    // shines), tilted from straight down by at most kShadowMaxTilt. In the
    // original a light below the centre (sm_hub_1's barrel fire) still casts
    // a short shadow on the ground beside the feet, and one overhead casts it
    // under them.
    const glm::vec3 centre(origin[0], origin[1], origin[2] + kCharacterCentre);
    const glm::vec3 to_character = light.type == LocalLight::Type::Directional
                                       ? glm::vec3(light.direction[0], light.direction[1], light.direction[2])
                                       : centre - glm::vec3(light.position[0], light.position[1], light.position[2]);
    const glm::vec2 across(to_character.x, to_character.y);
    const float tilt = std::min(std::atan2(glm::length(across), std::max(-to_character.z, 0.0f)), kShadowMaxTilt);
    const glm::vec2 away = glm::length(across) > 1e-3f ? glm::normalize(across) : glm::vec2(0.0f);
    const glm::vec3 d(away.x * std::sin(tilt), away.y * std::sin(tilt), -std::cos(tilt));
    const glm::vec3 up = std::abs(d.z) < 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    const glm::mat4 view = glm::lookAt(centre - d * 256.0f, centre, up);
    glm::mat4 projection = glm::ortho(-kShadowRadius, kShadowRadius, -kShadowRadius, kShadowRadius, 1.0f, 512.0f);
    projection[1][1] *= -1.0f; // As the other views (Vulkan clip space has +Y down).
    const glm::mat4 vp = projection * view;

    auto shadow = std::make_unique<Shadow>();
    std::memcpy(shadow->view_projection.data(), glm::value_ptr(vp), sizeof(shadow->view_projection));
    // The standing box's half-depth along the shadow: where the shadow starts to fade.
    const float half_depth = 16.0f * (std::abs(d.x) + std::abs(d.y)) + kCharacterCentre * std::abs(d.z);
    for (int k = 0; k < 4; ++k) {
        shadow->push[static_cast<std::size_t>(k)] = vp[k][0];     // Clip x row.
        shadow->push[4 + static_cast<std::size_t>(k)] = vp[k][1]; // Clip y row.
    }
    shadow->push[8] = d.x;
    shadow->push[9] = d.y;
    shadow->push[10] = d.z;
    shadow->push[11] = -glm::dot(d, centre);
    shadow->push[12] = half_depth;
    shadow->push[13] = kShadowFade;
    shadow->push[14] = 1.0f / static_cast<float>(kShadowSize);

    // The volume the shadow can reach: the silhouette's square, swept from the
    // character to where it has faded.
    const glm::vec3 end = centre + d * (half_depth + kShadowFade);
    for (std::size_t k = 0; k < 3; ++k) {
        const auto i = static_cast<glm::length_t>(k);
        shadow->mins[k] = std::min(centre[i], end[i]) - kShadowRadius;
        shadow->maxs[k] = std::max(centre[i], end[i]) + kShadowRadius;
    }
    const std::vector<float> receivers = shadow_receivers(bsp, materials_, shadow->mins, shadow->maxs, {d.x, d.y, d.z});
    if (receivers.empty()) {
        return;
    }
    std::vector<std::uint32_t> indices(receivers.size() / 3);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<std::uint32_t>(i);
    }
    shadow->index_count = static_cast<std::uint32_t>(indices.size());
    shadow->vertices = device_.create_buffer({receivers.size() * sizeof(float), rhi::BufferUsage::Vertex},
                                             std::as_bytes(std::span(receivers)));
    shadow->indices = device_.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                            std::as_bytes(std::span(indices)));
    shadow->leaves = formats::leaves_in_box(bsp, shadow->mins, shadow->maxs);
    shadow->target = device_.create_render_target(kShadowSize, kShadowSize);
    const rhi::Texture* textures[] = {&shadow->target->texture()};
    shadow->bind_group = device_.create_bind_group(*shadow_pipeline_, *shadow_sampler_, textures);
    inst.shadow = std::move(shadow);
}

void CharacterRenderer::draw_shadow_textures(rhi::CommandList& cmd, const CharacterDrawSettings& settings) {
    CharacterDrawSettings silhouette = settings;
    silhouette.envmaps = false;
    silhouette.bumpmaps = false;
    silhouette.visibility = nullptr;
    const float clear[4] = {0, 0, 0, 0};
    for (Instance& inst : instances_) {
        if (!inst.shadow) {
            continue;
        }
        Shadow& s = *inst.shadow;
        s.ready = !settings.visibility || settings.visibility->object(s.leaves, s.mins, s.maxs);
        if (!s.ready) {
            continue;
        }
        cmd.begin_render_target(*s.target, clear);
        draw_instance(cmd, s.view_projection, silhouette, inst, false, true);
        cmd.end_render_target();
    }
}

void CharacterRenderer::draw_shadows(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                     const CharacterDrawSettings& settings) {
    bool bound = false;
    for (const Instance& inst : instances_) {
        if (!inst.shadow || !inst.shadow->ready) {
            continue;
        }
        const Shadow& s = *inst.shadow;
        if (settings.visibility && !settings.visibility->object(s.leaves, s.mins, s.maxs)) {
            continue;
        }
        if (!bound) {
            cmd.set_pipeline(*shadow_pipeline_);
            const auto fog = settings.fog.rows();
            cmd.set_shader_data(fog.data(), fog.size() * sizeof(float));
            bound = true;
        }
        ShadowPushConstants pc{};
        std::memcpy(pc.mvp, view_projection.data(), sizeof(pc.mvp));
        std::memcpy(pc.rows, s.push.data(), sizeof(pc.rows));
        cmd.set_bind_group(*s.bind_group);
        cmd.set_vertex_buffer(*s.vertices);
        cmd.set_index_buffer(*s.indices);
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(s.index_count);    }
}

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
            std::vector<ClothPiece> cloth;
            std::uint32_t cloth_particles = 0;
            std::vector<ClothCapsule> capsules;
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
                    if (mo.has_tangents) {
                        std::memcpy(out.tangent, v.tangent.data(), sizeof(out.tangent));
                    }
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
                            flex.deltas.push_back({static_cast<std::uint32_t>(vertices[global].flex_slot), fv.delta,
                                                   fv.normal_delta});
                        }
                        flexes.push_back(std::move(flex));
                    }
                }
                // Cloth: LOD 0 of piece p is table slot p. Each cloth vertex
                // follows one particle; its normal sign is set against the
                // particle's bind-pose triangle normal.
                std::vector<std::size_t> piece_index(static_cast<std::size_t>(std::max(mo.cloth_pieces, 0)), SIZE_MAX);
                std::vector<std::vector<anim::Vec3>> bind_normals;
                for (const auto& c : mo.cloth) {
                    if (c.slot >= piece_index.size()) {
                        continue;
                    }
                    ClothPiece piece;
                    piece.cloth = &c;
                    piece.first_slot = cloth_particles;
                    std::vector<anim::Vec3> bind;
                    for (const auto p : c.particles) {
                        piece.particles.push_back(mo.vertices[p]);
                        bind.push_back(mo.vertices[p].position);
                    }
                    bind_normals.push_back(anim::ClothSimulation(c, bind).normals());
                    cloth_particles += static_cast<std::uint32_t>(c.particles.size());
                    piece_index[c.slot] = cloth.size();
                    cloth.push_back(std::move(piece));
                }
                for (const auto& mesh : mo.meshes) {
                    for (std::size_t v = 0; v < mesh.cloth_piece.size(); ++v) {
                        const std::uint8_t p = mesh.cloth_piece[v];
                        const std::size_t global = base + mesh.vertex_offset + v;
                        if (p >= piece_index.size() || piece_index[p] == SIZE_MAX || global >= vertices.size()) {
                            continue;
                        }
                        const std::size_t first = cloth.size() - bind_normals.size();
                        const std::size_t local = piece_index[p] - first;
                        const std::uint32_t particle = mesh.cloth_particle[v] & 0x7fffu;
                        if (particle >= bind_normals[local].size()) {
                            continue;
                        }
                        const auto& n = bind_normals[local][particle];
                        const auto& vn = vertices[global].normal;
                        const float facing = n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2];
                        const float slot = static_cast<float>(cloth[piece_index[p]].first_slot + particle) + 1.0f;
                        vertices[global].cloth = facing < 0.0f ? -slot : slot;
                    }
                }
                for (const auto& c : mo.cloth_capsules) {
                    capsules.push_back({c.bone, {c.start, c.end, c.radius}});
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
                model->cloth = std::move(cloth);
                model->cloth_particles = cloth_particles;
                model->cloth_capsules = std::move(capsules);
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
                        const RenderMaterial& mm = *mesh.material;
                        const rhi::Texture* envmap = mm.envmap ? mm.envmap : materials_.black_cube();
                        const rhi::Texture* mask = mm.envmap_mask ? mm.envmap_mask : materials_.white();
                        const rhi::Texture* bump = mm.bumpmap ? mm.bumpmap : materials_.white();
                        const rhi::Texture* detail_map = mm.detail ? mm.detail : materials_.white();
                        const rhi::Texture* textures[] = {mm.base, mm.base2, envmap, mask, bump, detail_map};
                        mesh.bind_group = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
                        for (const rhi::Texture* frame : mm.base_frames) {
                            const rhi::Texture* frame_textures[] = {frame, mm.eyes ? mm.base2 : frame, envmap, mask, bump,
                                                                    detail_map};
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

void CharacterRenderer::update(float time, const formats::ViewVisibility* visible) {
    for (Instance& inst : instances_) {
        if (visible && !visible->any_leaf(inst.leaves)) {
            continue;
        }
        if (inst.player) {
            const auto& bones = inst.model->mdl->bones;
            try {
                inst.skinning =
                    formats::mdl_skinning_matrices(bones, formats::mdl_world_transforms(bones, inst.player->pose(time)));
            } catch (const formats::FormatError& e) {
                std::fprintf(stderr, "animation: %s\n", e.what());
                inst.player.reset();
            }
        }
        if (!inst.cloth.empty()) {
            // After a gap (unseen, or the first frame), settle as on the first frame.
            update_cloth(inst, inst.cloth_time ? std::min(time - *inst.cloth_time, kClothSettleTime) : kClothSettleTime);
            inst.cloth_time = time;
        }
    }
}

void CharacterRenderer::update_cloth(Instance& inst, float dt) {
    const Model& model = *inst.model;
    std::vector<anim::ClothCapsule> capsules;
    for (const ClothCapsule& c : model.cloth_capsules) {
        if (c.bone < 0 || static_cast<std::size_t>(c.bone) >= inst.skinning.size()) {
            continue;
        }
        const auto& m = inst.skinning[static_cast<std::size_t>(c.bone)];
        capsules.push_back({transform(m, c.bind.start), transform(m, c.bind.end), c.bind.radius});
    }
    const std::size_t n = model.cloth_particles;
    inst.cloth_data.assign(n * 8, 0.0f);
    std::vector<anim::Vec3> skinned;
    for (std::size_t p = 0; p < model.cloth.size(); ++p) {
        const ClothPiece& piece = model.cloth[p];
        skinned.clear();
        for (const auto& v : piece.particles) {
            skinned.push_back(skin(inst.skinning, v));
        }
        // A long first step settles in fixed steps rather than being cut short.
        for (float left = dt; left > 0.0f; left -= 0.1f) {
            inst.cloth[p].update(std::min(left, 0.1f), skinned, capsules);
        }
        const auto& positions = inst.cloth[p].positions();
        const auto normals = inst.cloth[p].normals();
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const std::size_t slot = piece.first_slot + i;
            for (std::size_t k = 0; k < 3; ++k) {
                inst.cloth_data[slot * 4 + k] = positions[i][k];
                inst.cloth_data[(n + slot) * 4 + k] = normals[i][k];
            }
        }
    }
}

void CharacterRenderer::draw_pass(rhi::CommandList& cmd, const std::array<float, 16>& vp,
                                  const CharacterDrawSettings& settings, bool translucent) {
    for (const Instance& inst : instances_) {
        if (!settings.visibility || settings.visibility->object(inst.leaves, inst.mins, inst.maxs)) {
            draw_instance(cmd, vp, settings, inst, translucent);
        }
    }
}

void CharacterRenderer::draw_instance(rhi::CommandList& cmd, const std::array<float, 16>& vp,
                                      const CharacterDrawSettings& settings, const Instance& inst, bool translucent,
                                      bool shadow_build) {
    const glm::mat4 view_projection = glm::make_mat4(vp.data());
    std::vector<float> data;
    {
        bool any = false;
        for (const Mesh& mesh : inst.model->skins[inst.skin]) {
            any = any || mesh.material->translucent == translucent;
        }
        if (!any) {
            return;
        }
        // Shader data: ambient cube (6 float4), then 3 float4 rows per bone,
        // then the material params of each mesh drawn in this pass.
        data.assign(8 * 4 + inst.skinning.size() * 12, 0.0f);
        const auto fog = settings.fog.rows(); // Elements 6 and 7.
        std::copy(fog.begin(), fog.end(), data.begin() + 24);
        for (std::size_t f = 0; f < 6; ++f) {
            for (std::size_t c = 0; c < 3; ++c) {
                data[f * 4 + c] = inst.lighting.cube.color[f][c] * settings.light_scale;
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
        if ((data.size() + kLocalLightFloats) * sizeof(float) <= rhi::kMaxShaderDataSize) {
            pc.lights_block = static_cast<std::uint32_t>(data.size() / 4);
            const auto lights = pack_local_lights(inst.lighting, inst.transform, settings.light_scale);
            data.insert(data.end(), lights.begin(), lights.end());
        } else {
            for (std::size_t f = 0; f < 6; ++f) {
                for (std::size_t c = 0; c < 3; ++c) {
                    data[f * 4 + c] = inst.lighting.folded.color[f][c] * settings.light_scale;
                }
            }
        }
        if (!inst.cloth_data.empty() &&
            (data.size() + inst.cloth_data.size()) * sizeof(float) <= rhi::kMaxShaderDataSize) {
            pc.cloth_base = static_cast<std::uint32_t>(data.size() / 4);
            pc.cloth_count = inst.model->cloth_particles;
            data.insert(data.end(), inst.cloth_data.begin(), inst.cloth_data.end());
        }
        if (settings.envmaps && inst.envmapped && (data.size() + 16) * sizeof(float) <= rhi::kMaxShaderDataSize) {
            pc.envmap_block = static_cast<std::uint32_t>(data.size() / 4);
            data.insert(data.end(), {settings.eye[0], settings.eye[1], settings.eye[2], 0.0f});
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 4; ++c) {
                    data.push_back(inst.transform[c * 4 + r]); // Column-major.
                }
            }
        }
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
            // Silhouettes: opaque and two-sided (the original's ShadowBuild material is $nocull).
            const rhi::Pipeline& pipeline =
                shadow_build ? *pipelines_[0][1] : *pipelines_[blend_mode][m.no_cull ? 1 : 0];
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
                       (m.additive ? kAdditive : 0) | (m.bumpmap && settings.bumpmaps ? kBump : 0) |
                       (m.detail ? kDetail : 0);
            if (shadow_build) {
                pc.flags = (m.alpha_test ? kAlphaTest : 0) | kShadowBuild;
            }
            const rhi::BindGroup* group = mesh.frame_bind_groups.empty()
                                              ? mesh.bind_group.get()
                                              : mesh.frame_bind_groups[m.base_frame % mesh.frame_bind_groups.size()].get();
            if (m.envmap && pc.envmap_block != 0) {
                pc.flags |= kEnvmap | (static_cast<std::uint32_t>(m.envmap_mask_mode) << kEnvmapMaskShift);
                if (m.envmap_nearest && inst.cubemap && inst.cubemap != m.envmap) {
                    auto& g = mesh.cubemap_groups[inst.cubemap];
                    if (!g) {
                        const rhi::Texture* textures[] = {m.base, m.base2, inst.cubemap, m.envmap_mask,
                                                          m.bumpmap ? m.bumpmap : materials_.white(),
                                                          m.detail ? m.detail : materials_.white()};
                        g = device_.create_bind_group(*pipelines_[0][0], *sampler_, textures);
                    }
                    group = g.get();
                }
            }
            cmd.set_bind_group(*group);
            cmd.push_constants(&pc, sizeof(pc));
            cmd.draw_indexed(mesh.index_count, mesh.first_index);
        }
    }
}

void CharacterRenderer::draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                    const CharacterDrawSettings& settings) {
    draw_pass(cmd, view_projection, settings, false);
}

void CharacterRenderer::collect_translucent(const formats::BspDrawOrder& order, const CharacterDrawSettings& settings,
                                            std::uint32_t source, std::vector<TranslucentItem>& out) const {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        const Instance& inst = instances_[i];
        if (!inst.has_translucent ||
            (settings.visibility && !settings.visibility->object(inst.leaves, inst.mins, inst.maxs))) {
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

void CharacterRenderer::begin_translucent(const std::array<float, 16>& view_projection,
                                          const CharacterDrawSettings& settings) {
    pass_view_projection_ = view_projection;
    pass_settings_ = settings;
}

void CharacterRenderer::draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index) {
    if (index < instances_.size()) {
        draw_instance(cmd, pass_view_projection_, pass_settings_, instances_[index], true);
    }
}

} // namespace openbl::render
