#include "openbl/render/world_renderer.hpp"

#include "half.hpp"

#include "openbl_water_ps.h"
#include "openbl_water_vs.h"
#include "openbl_world_ps.h"
#include "openbl_world_vs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <tuple>

namespace openbl::render {

namespace {

using formats::BspFace;
using formats::BspFile;

struct Vertex {
    float position[3];
    float uv[2];
    float lightmap_uv[2]; // Style 0's block; further styles follow at style_step intervals.
    float blend;
    std::uint8_t styles[4]; // Light styles of the face; 255 = unused.
    float style_step;       // U distance between consecutive style blocks in the atlas.
    float normal[3];        // Smoothed over the face's triangles (displacements curve).
    // Bump-lit faces: U distance from a style's flat lightmap to its first of
    // three directional ones (each style holds four blocks); 0 otherwise.
    float bump_step;
    // The texture's u and v axes in world space, made perpendicular to the
    // normal: the tangent frame of $bumpmap normals (docs/RENDERING.md).
    float tangent_s[3];
    float tangent_t[3];
};
static_assert(sizeof(Vertex) == 80);

// Lightmap styles drawn per face (faces store up to 8; the Steam release uses at most 4).
constexpr std::size_t kMaxFaceStyles = 4;
constexpr std::int32_t kSurfBumpLight = formats::surf_flags::BumpLight;

struct PushConstants {
    float mvp[16];
    float alpha_ref;
    float light_scale;
    std::uint32_t flags;
    std::uint32_t params_index;
};
static_assert(sizeof(PushConstants) == 80);

struct WaterPushConstants {
    float refract[4]; // Tint (encoded), amount.
    float reflect[4]; // Tint (encoded; the original's DX9 shader does not apply it), amount.
    float bump_u[4];  // dudv and normal map transform, u row; w: flags.
    float bump_v[4];  // v row.
    float cheap[4];   // Fog colour (encoded) of cheap water.
    float cheap_range[4]; // Cheap-water start and end distances.
};
static_assert(sizeof(WaterPushConstants) == 96);

constexpr std::uint32_t kWaterReflect = 1;
constexpr std::uint32_t kWaterRefract = 2;
constexpr std::uint32_t kWaterCheap = 4;
constexpr std::uint32_t kWaterForceCheap = 8;

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kBlendLayers = 2;
constexpr std::uint32_t kUnlit = 4;
constexpr std::uint32_t kSelfIllum = 8;
constexpr std::uint32_t kTranslucent = 16;
constexpr std::uint32_t kModulate = 32;
constexpr std::uint32_t kMultiplyLayers = 64;
constexpr std::uint32_t kAdditive = 128;
constexpr std::uint32_t kEnvmap = 256;
constexpr std::uint32_t kEnvmapMaskShift = 9; // 2 bits: RenderMaterial::EnvmapMask.
constexpr std::uint32_t kBump = 2048;         // A normal map lit by the directional lightmaps.

// Source polygons are wound clockwise seen from the front, and stay clockwise
// in Vulkan framebuffer space with our Y-flipped projection (verified visually
// on ch_lotus_1: the opposite setting culls the walls facing the camera).
constexpr bool kFrontCounterClockwise = false;

using detail::to_half;

bool face_drawn(const BspFile& bsp, const BspFace& face) {
    using namespace formats::surf_flags;
    return (bsp.texinfos[face.texinfo].flags & (NoDraw | Sky | Sky2D | Trigger | Skip | Hint)) == 0 &&
           bsp.texinfos[face.texinfo].texdata >= 0;
}

bool face_has_lightmap(const BspFile& bsp, const BspFace& face) {
    return face.light_offset >= 0 && face.style_count() > 0 &&
           (bsp.texinfos[face.texinfo].flags & formats::surf_flags::NoLight) == 0;
}

std::size_t face_styles(const BspFace& face) {
    return std::min(face.style_count(), kMaxFaceStyles);
}

// Per-vertex normals of a face mesh: its triangles' normals summed and
// normalised, turned to the face's front (so displacements curve smoothly).
std::vector<std::array<float, 3>> face_normals(const BspFile& bsp, std::size_t f, const formats::BspFaceMesh& mesh) {
    const BspFace& face = bsp.faces[f];
    formats::Vec3 front = bsp.planes[face.plane].normal;
    if (face.side) {
        for (float& c : front) {
            c = -c;
        }
    }
    std::vector<std::array<float, 3>> out(mesh.positions.size(), {0, 0, 0});
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto& a = mesh.positions[mesh.indices[i]];
        const auto& b = mesh.positions[mesh.indices[i + 1]];
        const auto& c = mesh.positions[mesh.indices[i + 2]];
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (std::size_t k = 0; k < 3; ++k) {
            for (int j = 0; j < 3; ++j) {
                out[mesh.indices[i + k]][static_cast<std::size_t>(j)] += n[j];
            }
        }
    }
    for (auto& n : out) {
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-6f) {
            n = front;
            continue;
        }
        const float sign = (n[0] * front[0] + n[1] * front[1] + n[2] * front[2]) < 0.0f ? -1.0f : 1.0f;
        for (float& c : n) {
            c *= sign / len;
        }
    }
    return out;
}

// A texture axis (texinfo u or v, xyz) made perpendicular to `normal` and normalised.
void tangent_axis(const std::array<float, 4>& axis, const std::array<float, 3>& normal, float out[3]) {
    const float d = axis[0] * normal[0] + axis[1] * normal[1] + axis[2] * normal[2];
    float len = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) {
        out[k] = axis[k] - normal[k] * d;
        len += out[k] * out[k];
    }
    len = std::sqrt(len);
    for (std::size_t k = 0; k < 3; ++k) {
        out[k] = len > 1e-6f ? out[k] / len : 0.0f;
    }
}

// A brush model instance: the world (model 0) or a brush entity at its origin.
struct ModelInstance {
    std::size_t model = 0;
    formats::Vec3 origin{};
    const formats::BspEntity* entity = nullptr;
};

std::string entity_value(const formats::BspEntity& e, const char* key) {
    const auto it = e.find(key);
    return it == e.end() ? std::string() : it->second;
}

formats::Vec3 entity_vec3(const formats::BspEntity& e, const char* key) {
    formats::Vec3 v{};
    std::istringstream(entity_value(e, key)) >> v[0] >> v[1] >> v[2];
    return v;
}

std::vector<ModelInstance> model_instances(const BspFile& bsp) {
    std::vector<ModelInstance> out{{0, {}, nullptr}};
    for (const auto& e : bsp.entities) {
        auto model = e.find("model");
        if (model == e.end() || model->second.size() < 2 || model->second[0] != '*') {
            continue;
        }
        const std::size_t index = std::strtoul(model->second.c_str() + 1, nullptr, 10);
        if (index == 0 || index >= bsp.models.size()) {
            continue;
        }
        out.push_back({index, entity_vec3(e, "origin"), &e});
    }
    return out;
}

// Render-target size of a monitor picture (square).
constexpr std::uint32_t kMonitorResolution = 512;

} // namespace

WorldRenderer::WorldRenderer(rhi::Device& device, MaterialSystem& materials, const BspFile& bsp,
                             const std::vector<std::string>& shown)
    : device_(device), styles_(bsp) {
    std::vector<std::array<float, 2>> lightmap_offsets;
    build_lightmap_atlas(bsp, lightmap_offsets);
    const float inv_w = 1.0f / static_cast<float>(stats_.lightmap_width);
    const float inv_h = 1.0f / static_cast<float>(stats_.lightmap_height);

    // Group faces by material so each material is one draw.
    struct FaceRef {
        std::size_t face;
        formats::Vec3 origin;
    };
    // The 3D skybox: the sky_camera's BSP area (an area of its own in every
    // shipped map). Faces there are drawn only in the skybox pass.
    for (const auto& e : bsp.entities) {
        if (entity_value(e, "classname") == "sky_camera") {
            SkyCamera sky;
            sky.origin = entity_vec3(e, "origin");
            if (const std::string scale = entity_value(e, "scale"); !scale.empty() && std::atof(scale.c_str()) > 0) {
                sky.scale = static_cast<float>(std::atof(scale.c_str()));
            }
            sky.fog = Fog::from_entity(e);
            sky.area = bsp.leafs[bsp.leaf_at(sky.origin)].area;
            sky_camera_ = sky;
            break;
        }
    }
    std::vector<char> in_skybox(bsp.faces.size(), 0);
    if (sky_camera_) {
        for (const auto& leaf : bsp.leafs) {
            if (leaf.area != sky_camera_->area) {
                continue;
            }
            for (std::uint16_t i = 0; i < leaf.num_leaf_faces; ++i) {
                const std::size_t lf = std::size_t{leaf.first_leaf_face} + i;
                if (lf < bsp.leaf_faces.size() && bsp.leaf_faces[lf] < in_skybox.size()) {
                    in_skybox[bsp.leaf_faces[lf]] = 1;
                }
            }
        }
    }
    // Entities with StartHidden start invisible until a script shows them (the
    // haven TV until switched on, a monitor's "dead" screen until its camera
    // breaks), so they are not drawn. Monitors (func_monitor) show their
    // point_camera's view.
    std::map<std::string, const formats::BspEntity*> cameras;
    for (const auto& e : bsp.entities) {
        if (entity_value(e, "classname") == "point_camera") {
            cameras[entity_value(e, "targetname")] = &e;
        }
    }
    // Key: material name, the monitor whose picture its faces show (-1: none),
    // whether the faces are in the 3D skybox, and for translucent materials
    // what places them in the drawing order: the BSP node they lie on, the
    // face itself (detail faces lie on no node; Source draws each in the
    // nearest leaf listing it), or their brush entity.
    enum Owner : int { kUnordered, kNode, kFace, kEntity };
    std::map<std::tuple<std::string, int, bool, int, std::int64_t>, std::vector<FaceRef>> by_material;
    const auto nodes_of_face = formats::face_nodes(bsp);
    std::int64_t entity_number = 0;
    // Water faces at one height (within 2 units) make up one water plane.
    auto water_plane = [&](std::size_t f, const formats::Vec3& origin, bool skybox) {
        const auto mesh = formats::build_face_mesh(bsp, f);
        float z = 0;
        formats::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const auto& p : mesh.positions) {
            z += p[2] + origin[2];
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], p[k] + origin[k]);
                hi[k] = std::max(hi[k], p[k] + origin[k]);
            }
        }
        z /= static_cast<float>(std::max<std::size_t>(mesh.positions.size(), 1));
        for (std::size_t i = 0; i < water_planes_.size(); ++i) {
            WaterPlane& plane = water_planes_[i];
            if (plane.skybox == skybox && std::abs(plane.z - z) <= 2.0f) {
                for (int k = 0; k < 3; ++k) {
                    plane.mins[k] = std::min(plane.mins[k], lo[k]);
                    plane.maxs[k] = std::max(plane.maxs[k], hi[k]);
                }
                return i;
            }
        }
        WaterPlane plane;
        plane.z = z;
        plane.mins = lo;
        plane.maxs = hi;
        plane.skybox = skybox;
        water_planes_.push_back(plane);
        return water_planes_.size() - 1;
    };
    for (const ModelInstance& inst : model_instances(bsp)) {
        int monitor = -1;
        if (inst.entity && entity_value(*inst.entity, "StartHidden") == "1" &&
            std::find(shown.begin(), shown.end(), entity_value(*inst.entity, "targetname")) == shown.end()) {
            ++stats_.hidden_entities;
            continue;
        }
        if (inst.entity && entity_value(*inst.entity, "classname") == "func_monitor") {
            if (const auto cam = cameras.find(entity_value(*inst.entity, "target")); cam != cameras.end()) {
                const formats::BspEntity& c = *cam->second;
                MonitorView view;
                view.origin = entity_vec3(c, "origin");
                if (c.contains("look_dest")) {
                    view.look_at = entity_vec3(c, "look_dest");
                } else {
                    // Source angles: pitch (down positive), yaw.
                    const auto angles = entity_vec3(c, "angles");
                    const float p = angles[0] * 3.14159265f / 180.0f, y = angles[1] * 3.14159265f / 180.0f;
                    view.look_at = {view.origin[0] + std::cos(p) * std::cos(y), view.origin[1] + std::cos(p) * std::sin(y),
                                    view.origin[2] - std::sin(p)};
                }
                if (const std::string fov = entity_value(c, "FOV"); !fov.empty()) {
                    view.fov = static_cast<float>(std::atof(fov.c_str()));
                }
                view.target = device.create_render_target(kMonitorResolution, kMonitorResolution);
                monitor = static_cast<int>(monitors_.size());
                monitors_.push_back(std::move(view));
                ++stats_.monitors;
            }
        }
        const auto& model = bsp.models[inst.model];
        ++entity_number;
        // Brush entities belong to the skybox if their origin is in its area.
        const bool entity_in_skybox = inst.entity && sky_camera_ &&
                                      bsp.leafs[bsp.leaf_at(inst.origin)].area == sky_camera_->area;
        for (std::int32_t i = 0; i < model.num_faces; ++i) {
            const std::size_t f = static_cast<std::size_t>(model.first_face + i);
            if (!face_drawn(bsp, bsp.faces[f])) {
                ++stats_.skipped_faces;
                continue;
            }
            const auto texdata = static_cast<std::size_t>(bsp.texinfos[bsp.faces[f].texinfo].texdata);
            const std::string& name = bsp.texdatas[texdata].material;
            const RenderMaterial& mat = materials.material(name);
            const int screen = monitor >= 0 && mat.camera_feed ? monitor : -1;
            const bool skybox = inst.entity ? entity_in_skybox : in_skybox[f] != 0;
            stats_.skybox_faces += skybox;
            if (mat.water && !mat.skip) {
                // Only the upward-facing faces are water surfaces. The others
                // (undersides with the $bottommaterial, and sides) are seen
                // from below the water, which is not drawn yet.
                const auto& face = bsp.faces[f];
                const float up = bsp.planes[face.plane].normal[2] * (face.side ? -1.0f : 1.0f);
                if (up < 0.9f) {
                    ++stats_.skipped_faces;
                    continue;
                }
                WaterPlane& p = water_planes_[water_plane(f, inst.origin, skybox)];
                // The first material decides the plane's water fog.
                p.fog_material = p.fog_material ? p.fog_material : &mat;
            }
            int owner = kUnordered;
            std::int64_t owner_id = 0;
            if (mat.translucent && !mat.water) {
                if (inst.entity) {
                    owner = kEntity;
                    owner_id = entity_number;
                } else if (nodes_of_face[f] >= 0) {
                    owner = kNode;
                    owner_id = nodes_of_face[f];
                } else {
                    owner = kFace;
                    owner_id = static_cast<std::int64_t>(f);
                }
            }
            by_material[{name, screen, skybox, owner, owner_id}].push_back({f, inst.origin});
        }
    }

    std::map<std::size_t, std::vector<std::uint32_t>> leaves_of_face;
    for (const auto& [key, faces] : by_material) {
        if (std::get<3>(key) == kFace) {
            leaves_of_face[static_cast<std::size_t>(std::get<4>(key))];
        }
    }
    for (std::size_t l = 0; l < bsp.leafs.size() && !leaves_of_face.empty(); ++l) {
        const auto& leaf = bsp.leafs[l];
        for (std::uint16_t i = 0; i < leaf.num_leaf_faces; ++i) {
            const std::size_t lf = std::size_t{leaf.first_leaf_face} + i;
            if (lf < bsp.leaf_faces.size()) {
                if (auto it = leaves_of_face.find(bsp.leaf_faces[lf]); it != leaves_of_face.end()) {
                    it->second.push_back(static_cast<std::uint32_t>(l));
                }
            }
        }
    }

    // The clusters of the world leaves listing each face.
    std::vector<std::vector<std::int32_t>> face_clusters(bsp.faces.size());
    for (const auto& leaf : bsp.leafs) {
        for (std::uint16_t i = 0; i < leaf.num_leaf_faces; ++i) {
            const std::size_t lf = std::size_t{leaf.first_leaf_face} + i;
            if (lf < bsp.leaf_faces.size() && bsp.leaf_faces[lf] < face_clusters.size()) {
                auto& clusters = face_clusters[bsp.leaf_faces[lf]];
                if (std::find(clusters.begin(), clusters.end(), leaf.cluster) == clusters.end()) {
                    clusters.push_back(leaf.cluster);
                }
            }
        }
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Lightmap coordinates and styles of a point on face `f`, from its
    // face-relative luxel position.
    auto light_vertex = [&](Vertex& out, std::size_t f, const std::array<float, 2>& luxel) {
        const BspFace& face = bsp.faces[f];
        const bool lit = face_has_lightmap(bsp, face);
        if (lit) {
            // Clamp to the face's luxels so bilinear filtering never reads a neighbour.
            const float s = std::clamp(luxel[0], 0.0f, static_cast<float>(face.lightmap_size[0]));
            const float t = std::clamp(luxel[1], 0.0f, static_cast<float>(face.lightmap_size[1]));
            out.lightmap_uv[0] = (lightmap_offsets[f][0] + s + 0.5f) * inv_w;
            out.lightmap_uv[1] = (lightmap_offsets[f][1] + t + 0.5f) * inv_h;
        } else {
            out.lightmap_uv[0] = white_luxel_[0] * inv_w;
            out.lightmap_uv[1] = white_luxel_[1] * inv_h;
        }
        for (std::size_t k = 0; k < 4; ++k) {
            out.styles[k] = 255;
        }
        if (lit) {
            for (std::size_t k = 0; k < face_styles(face); ++k) {
                out.styles[k] = face.styles[k];
            }
            // Bump-lit faces store four lightmaps per style: flat, then the three directions.
            const float block = static_cast<float>(face.lightmap_size[0] + 2) * inv_w;
            const bool bump = (bsp.texinfos[face.texinfo].flags & kSurfBumpLight) != 0;
            out.style_step = bump ? 4.0f * block : block;
            out.bump_step = bump ? block : 0.0f;
        } else {
            out.styles[0] = 0; // The white luxel, at full brightness.
        }
    };
    for (const auto& [key, faces] : by_material) {
        const RenderMaterial& mat = materials.material(std::get<0>(key));
        stats_.missing_materials += mat.missing;
        if (mat.skip) {
            stats_.skipped_faces += faces.size();
            continue;
        }
        Batch batch;
        batch.material = &mat;
        batch.monitor = std::get<1>(key);
        batch.skybox = std::get<2>(key);
        const int owner = std::get<3>(key);
        batch.node = owner == kNode ? static_cast<std::int32_t>(std::get<4>(key)) : -1;
        batch.entity = owner == kEntity;
        batch.first_index = static_cast<std::uint32_t>(indices.size());
        const std::size_t first_vertex = vertices.size();
        // Each face's mesh and clusters: those of the leaves listing it, or
        // (brush entities, displacements) of the leaves its bounds touch.
        struct Prepared {
            const FaceRef* ref;
            formats::BspFaceMesh mesh;
            std::vector<std::int32_t> clusters;
            formats::Vec3 mins, maxs;
        };
        std::vector<Prepared> prepared;
        const bool opaque = !mat.water && !mat.translucent;
        for (const FaceRef& ref : faces) {
            Prepared p{&ref, formats::build_face_mesh(bsp, ref.face), {}, {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
            for (const auto& v : p.mesh.positions) {
                for (int k = 0; k < 3; ++k) {
                    p.mins[k] = std::min(p.mins[k], v[k] + ref.origin[k]);
                    p.maxs[k] = std::max(p.maxs[k], v[k] + ref.origin[k]);
                }
            }
            if (opaque) {
                const bool moved = ref.origin[0] != 0 || ref.origin[1] != 0 || ref.origin[2] != 0;
                if (!moved) {
                    p.clusters = face_clusters[ref.face];
                }
                if (p.clusters.empty() && !p.mesh.positions.empty()) {
                    for (std::uint32_t l : formats::leaves_in_box(bsp, p.mins, p.maxs)) {
                        const std::int32_t c = bsp.leafs[l].cluster;
                        if (std::find(p.clusters.begin(), p.clusters.end(), c) == p.clusters.end()) {
                            p.clusters.push_back(c);
                        }
                    }
                }
                std::sort(p.clusters.begin(), p.clusters.end());
            }
            prepared.push_back(std::move(p));
        }
        std::stable_sort(prepared.begin(), prepared.end(),
                         [](const Prepared& a, const Prepared& b) { return a.clusters < b.clusters; });
        for (const Prepared& p : prepared) {
            const FaceRef& ref = *p.ref;
            const auto& mesh = p.mesh;
            if (opaque && (batch.runs.empty() || batch.runs.back().clusters != p.clusters)) {
                Batch::Run run;
                run.first_index = static_cast<std::uint32_t>(indices.size());
                run.clusters = p.clusters;
                run.mins = p.mins;
                run.maxs = p.maxs;
                batch.runs.push_back(std::move(run));
            }
            if (opaque) {
                Batch::Run& run = batch.runs.back();
                for (int k = 0; k < 3; ++k) {
                    run.mins[k] = std::min(run.mins[k], p.mins[k]);
                    run.maxs[k] = std::max(run.maxs[k], p.maxs[k]);
                }
            }
            const auto base = static_cast<std::uint32_t>(vertices.size());
            const auto normals = face_normals(bsp, ref.face, mesh);
            for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
                Vertex out{};
                for (int k = 0; k < 3; ++k) {
                    out.position[k] = mesh.positions[v][k] + ref.origin[k];
                }
                out.uv[0] = mesh.uvs[v][0];
                out.uv[1] = mesh.uvs[v][1];
                light_vertex(out, ref.face, mesh.lightmap_uvs[v]);
                out.blend = mesh.alphas[v];
                std::copy(normals[v].begin(), normals[v].end(), out.normal);
                const auto& tex = bsp.texinfos[bsp.faces[ref.face].texinfo].texture_vecs;
                tangent_axis(tex[0], normals[v], out.tangent_s);
                tangent_axis(tex[1], normals[v], out.tangent_t);
                vertices.push_back(out);
            }
            for (std::uint32_t idx : mesh.indices) {
                indices.push_back(base + idx);
            }
            if (opaque) {
                batch.runs.back().index_count = static_cast<std::uint32_t>(indices.size()) - batch.runs.back().first_index;
            }
            ++stats_.faces;
        }
        batch.index_count = static_cast<std::uint32_t>(indices.size()) - batch.first_index;
        stats_.triangles += batch.index_count / 3;
        std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (std::size_t v = first_vertex; v < vertices.size(); ++v) {
            for (std::size_t k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], vertices[v].position[k]);
                hi[k] = std::max(hi[k], vertices[v].position[k]);
            }
        }
        for (std::size_t k = 0; k < 3; ++k) {
            batch.center[k] = (lo[k] + hi[k]) * 0.5f;
        }
        if (owner == kFace) {
            const auto it = leaves_of_face.find(static_cast<std::size_t>(std::get<4>(key)));
            if (it != leaves_of_face.end()) {
                batch.leaves = it->second;
            }
        }
        if ((owner == kEntity || owner == kFace) && batch.leaves.empty() && !faces.empty()) {
            batch.leaves = formats::leaves_in_box(bsp, {lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]});
        }
        (mat.water ? water_ : mat.translucent ? translucent_ : opaque_).push_back(std::move(batch));
    }

    // infodecals: the base texture's size times $decalscale, projected onto
    // nearby brush faces and lifted slightly off them to avoid z-fighting.
    constexpr float kDecalLift = 0.1f;
    std::map<std::pair<const RenderMaterial*, bool>, std::vector<formats::BspDecalPiece>> decal_pieces;
    for (const auto& e : bsp.entities) {
        const auto cls = e.find("classname");
        const auto texture = e.find("texture");
        const auto origin = e.find("origin");
        if (cls == e.end() || cls->second != "infodecal" || texture == e.end() || origin == e.end()) {
            continue;
        }
        const RenderMaterial& mat = materials.material(texture->second);
        if (mat.missing || mat.skip) {
            ++stats_.unplaced_decals;
            continue;
        }
        formats::Vec3 at{};
        std::istringstream(origin->second) >> at[0] >> at[1] >> at[2];
        const auto& size = mat.base->desc();
        auto pieces = formats::project_decal(bsp, at, static_cast<float>(size.width) * mat.decal_scale,
                                             static_cast<float>(size.height) * mat.decal_scale);
        (pieces.empty() ? stats_.unplaced_decals : stats_.decals) += 1;
        const bool decal_in_skybox = sky_camera_ && bsp.leafs[bsp.leaf_at(at)].area == sky_camera_->area;
        auto& list = decal_pieces[{&mat, decal_in_skybox}];
        list.insert(list.end(), std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
    }
    for (const auto& [key, pieces] : decal_pieces) {
        Batch batch;
        batch.material = key.first;
        batch.skybox = key.second;
        batch.decal = true;
        batch.first_index = static_cast<std::uint32_t>(indices.size());
        for (const auto& piece : pieces) {
            const auto base = static_cast<std::uint32_t>(vertices.size());
            for (std::size_t v = 0; v < piece.positions.size(); ++v) {
                Vertex out{};
                for (std::size_t k = 0; k < 3; ++k) {
                    out.position[k] = piece.positions[v][k] + piece.normal[k] * kDecalLift;
                }
                out.uv[0] = piece.uvs[v][0];
                out.uv[1] = piece.uvs[v][1];
                light_vertex(out, piece.face, piece.lightmap_uvs[v]);
                std::copy(piece.normal.begin(), piece.normal.end(), out.normal);
                vertices.push_back(out);
            }
            // A convex polygon in the face's winding: a triangle fan.
            for (std::uint32_t k = 2; k < piece.positions.size(); ++k) {
                indices.insert(indices.end(), {base, base + k - 1, base + k});
            }
        }
        batch.index_count = static_cast<std::uint32_t>(indices.size()) - batch.first_index;
        if (batch.index_count > 0) {
            decals_.push_back(std::move(batch));
        }
    }
    stats_.batches = opaque_.size() + translucent_.size() + water_.size() + decals_.size();
    stats_.water_planes = water_planes_.size();

    if (!vertices.empty()) {
        vertices_ = device.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                         std::as_bytes(std::span(vertices)));
        indices_ = device.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                        std::as_bytes(std::span(indices)));
    }

    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_world_vs;
    pd.pixel.spirv = openbl_world_ps;
    pd.vertex_stride = sizeof(Vertex);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0},
                     {1, rhi::Format::RG32_Float, 12},
                     {2, rhi::Format::RG32_Float, 20},
                     {3, rhi::Format::R32_Float, 28},
                     {4, rhi::Format::RGBA8_UInt, 32},
                     {5, rhi::Format::R32_Float, 36},
                     {6, rhi::Format::RGB32_Float, 40},
                     {7, rhi::Format::R32_Float, 52},
                     {8, rhi::Format::RGB32_Float, 56},
                     {9, rhi::Format::RGB32_Float, 68}};
    pd.texture_count = 6; // Base, second base, lightmap, envmap (cube), envmap mask, bump map.
    pd.push_constant_size = sizeof(PushConstants);
    pd.shader_data = true;
    pd.front_counter_clockwise = kFrontCounterClockwise;
    const rhi::BlendMode blends[] = {rhi::BlendMode::Opaque, rhi::BlendMode::Alpha, rhi::BlendMode::Additive};
    for (std::size_t b = 0; b < 3; ++b) {
        for (std::size_t c = 0; c < 2; ++c) {
            pd.blend = blends[b];
            pd.depth_write = b == 0;
            pd.cull = c == 0 ? rhi::CullMode::Back : rhi::CullMode::None;
            pipelines_[b][c] = device.create_pipeline(pd);
        }
    }

    pd.cull = rhi::CullMode::Back;
    pd.depth_write = false;
    pd.blend = rhi::BlendMode::Alpha;
    decal_pipelines_[0] = device.create_pipeline(pd);
    pd.blend = rhi::BlendMode::Modulate;
    decal_pipelines_[1] = device.create_pipeline(pd);

    rhi::PipelineDesc wd;
    wd.vertex.spirv = openbl_water_vs;
    wd.pixel.spirv = openbl_water_ps;
    wd.vertex_stride = sizeof(Vertex);
    wd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}};
    wd.texture_count = 5; // dudv, normal map, reflection, refraction, envmap (cheap water).
    wd.push_constant_size = sizeof(WaterPushConstants);
    wd.shader_data = true;
    wd.cull = rhi::CullMode::None;
    // The refraction replaces what is behind the surface.
    wd.blend = rhi::BlendMode::Opaque;
    wd.depth_write = false;
    water_pipeline_ = device.create_pipeline(wd);
    const std::array<std::uint8_t, 4> black{0, 0, 0, 255};
    const std::span<const std::byte> black_mip = std::as_bytes(std::span(black));
    black_ = device.create_texture({1, 1, 1, rhi::Format::RGBA8_UNorm}, std::span(&black_mip, 1));
    black_cube_ = materials.black_cube();

    // Materials without an environment map bind stand-ins (the shader skips them).
    auto envmap_or = [&](const RenderMaterial& m) { return m.envmap ? m.envmap : materials.black_cube(); };
    auto mask_or = [&](const RenderMaterial& m) { return m.envmap_mask ? m.envmap_mask : materials.white(); };
    auto bump_or = [&](const RenderMaterial& m) { return m.bumpmap ? m.bumpmap : materials.white(); };
    sampler_ = device.create_sampler({});
    for (auto* list : {&opaque_, &translucent_}) {
        for (Batch& batch : *list) {
            const RenderMaterial& m = *batch.material;
            const rhi::Texture* base =
                batch.monitor >= 0 ? &monitors_[static_cast<std::size_t>(batch.monitor)].target->texture() : m.base;
            const rhi::Texture* textures[] = {base, m.base2, lightmap_.get(), envmap_or(m), mask_or(m), bump_or(m)};
            batch.bind_group = device.create_bind_group(*pipelines_[0][0], *sampler_, textures);
            if (batch.monitor >= 0) {
                continue; // The picture is the render target, not texture frames.
            }
            for (const rhi::Texture* frame : m.base_frames) {
                const rhi::Texture* frame_textures[] = {frame, (m.blend_layers || m.multiply_layers) ? m.base2 : frame,
                                                        lightmap_.get(), envmap_or(m), mask_or(m), bump_or(m)};
                batch.frame_bind_groups.push_back(device.create_bind_group(*pipelines_[0][0], *sampler_, frame_textures));
            }
        }
    }
    create_water_targets(0, 0); // Bind groups with stand-ins until the viewer sizes the targets.
    rhi::SamplerDesc clamped;
    clamped.address = rhi::AddressMode::Clamp;
    decal_sampler_ = device.create_sampler(clamped);
    for (Batch& batch : decals_) {
        const rhi::Texture* textures[] = {batch.material->base, batch.material->base2, lightmap_.get(),
                                          envmap_or(*batch.material), mask_or(*batch.material),
                                          bump_or(*batch.material)};
        batch.bind_group = device.create_bind_group(*decal_pipelines_[0], *decal_sampler_, textures);
    }
}

WorldRenderer::~WorldRenderer() = default;

void WorldRenderer::create_water_targets(std::uint32_t width, std::uint32_t height) {
    const bool built = water_.empty() || water_.front().bind_group;
    if (built && water_target_size_ == std::array<std::uint32_t, 2>{width, height}) {
        return;
    }
    if (built) {
        device_.wait_idle(); // The old targets and bind groups may still be in use.
    }
    water_target_size_ = {width, height};
    const bool targets = width > 0 && height > 0 &&
                         std::any_of(water_planes_.begin(), water_planes_.end(), [](const WaterPlane& p) { return !p.skybox; });
    water_reflection_ = targets ? device_.create_render_target(width, height) : nullptr;
    water_refraction_ = targets ? device_.create_render_target(width, height) : nullptr;
    for (Batch& batch : water_) {
        // Water in the 3D skybox is not drawn into the targets' view, so it does not sample them.
        const bool sampled = water_reflection_ && !batch.skybox;
        const rhi::Texture* reflection = sampled ? &water_reflection_->texture() : black_.get();
        const rhi::Texture* refraction = sampled ? &water_refraction_->texture() : black_.get();
        const RenderMaterial& m = *batch.material;
        const rhi::Texture* envmap = m.envmap ? m.envmap : black_cube_;
        auto group = [&](const rhi::Texture* dudv, const rhi::Texture* normal) {
            const rhi::Texture* textures[] = {dudv, normal, reflection, refraction, envmap};
            return device_.create_bind_group(*water_pipeline_, *sampler_, textures);
        };
        batch.bind_group = group(m.dudv, m.normal_map);
        batch.frame_bind_groups.clear();
        // The dudv and normal maps step through their frames together.
        const std::size_t frames = std::max(m.dudv_frames.size(), m.normal_frames.size());
        for (std::size_t f = 0; f < frames; ++f) {
            batch.frame_bind_groups.push_back(group(f < m.dudv_frames.size() ? m.dudv_frames[f] : m.dudv,
                                                    f < m.normal_frames.size() ? m.normal_frames[f] : m.normal_map));
        }
    }
}

const rhi::BindGroup& WorldRenderer::Batch::current_bind_group() const {
    if (!frame_bind_groups.empty()) {
        const std::uint32_t frame = material->water ? material->normal_frame : material->base_frame;
        return *frame_bind_groups[frame % frame_bind_groups.size()];
    }
    return *bind_group;
}

void WorldRenderer::build_lightmap_atlas(const BspFile& bsp, std::vector<std::array<float, 2>>& offsets) {
    offsets.assign(bsp.faces.size(), {0, 0});
    struct Rect {
        std::size_t face;
        std::uint32_t w, h;
    };
    std::vector<Rect> rects;
    std::uint64_t area = 0;
    for (std::size_t f = 0; f < bsp.faces.size(); ++f) {
        const BspFace& face = bsp.faces[f];
        if (!face_has_lightmap(bsp, face)) {
            continue;
        }
        // Bump-lit faces: four blocks per style (flat, then the three directions).
        const bool bump = (bsp.texinfos[face.texinfo].flags & kSurfBumpLight) != 0;
        const auto blocks = static_cast<std::uint32_t>(face_styles(face)) * (bump ? 4 : 1);
        const auto w = static_cast<std::uint32_t>(face.lightmap_size[0] + 1) * blocks + (blocks - 1);
        const auto h = static_cast<std::uint32_t>(face.lightmap_size[1] + 1);
        rects.push_back({f, w, h});
        area += std::uint64_t{w + 1} * (h + 1);
    }
    std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) { return a.h > b.h; });

    const std::uint32_t width = area > 1500u * 1500u ? 4096 : 2048;
    // Shelf packing with a 1-luxel gap; a 2x2 white block for unlit faces comes first.
    std::uint32_t x = 3, y = 0, shelf = 2;
    std::vector<std::array<std::uint32_t, 2>> placed(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (x + rects[i].w > width) {
            x = 0;
            y += shelf + 1;
            shelf = 0;
        }
        placed[i] = {x, y};
        x += rects[i].w + 1;
        shelf = std::max(shelf, rects[i].h);
    }
    const std::uint32_t height = std::max<std::uint32_t>(4, (y + shelf + 4) & ~3u);
    stats_.lightmap_width = width;
    stats_.lightmap_height = height;

    std::vector<std::uint16_t> pixels(std::size_t{width} * height * 4, 0);
    const std::uint16_t one = to_half(1.0f);
    for (std::uint32_t py = 0; py < 2; ++py) {
        for (std::uint32_t px = 0; px < 2; ++px) {
            const std::size_t o = (std::size_t{py} * width + px) * 4;
            pixels[o] = pixels[o + 1] = pixels[o + 2] = pixels[o + 3] = one;
        }
    }
    white_luxel_ = {1.0f, 1.0f};

    for (std::size_t i = 0; i < rects.size(); ++i) {
        const BspFace& face = bsp.faces[rects[i].face];
        offsets[rects[i].face] = {static_cast<float>(placed[i][0]), static_cast<float>(placed[i][1])};
        const auto lw = static_cast<std::uint32_t>(face.lightmap_size[0] + 1);
        const std::size_t luxels = std::size_t{lw} * rects[i].h;
        const bool bump = (bsp.texinfos[face.texinfo].flags & kSurfBumpLight) != 0;
        // The file holds the blocks in the same order: per style, flat then the three directions.
        for (std::size_t k = 0; k < face_styles(face) * (bump ? 4 : 1); ++k) {
            const std::byte* src = bsp.lighting.data() + face.light_offset + k * luxels * 4;
            const std::uint32_t x0 = placed[i][0] + static_cast<std::uint32_t>(k) * (lw + 1);
            for (std::uint32_t ly = 0; ly < rects[i].h; ++ly) {
                for (std::uint32_t lx = 0; lx < lw; ++lx) {
                    const auto rgb = formats::decode_rgbe(src + (std::size_t{ly} * lw + lx) * 4);
                    const std::size_t o = ((std::size_t{placed[i][1]} + ly) * width + x0 + lx) * 4;
                    pixels[o] = to_half(rgb[0]);
                    pixels[o + 1] = to_half(rgb[1]);
                    pixels[o + 2] = to_half(rgb[2]);
                    pixels[o + 3] = one;
                }
            }
        }
    }
    const std::span<const std::byte> mip = std::as_bytes(std::span(pixels));
    lightmap_ = device_.create_texture({width, height, 1, rhi::Format::RGBA16_Float}, std::span(&mip, 1));
}

const rhi::Pipeline& WorldRenderer::pipeline_for(const RenderMaterial& m) const {
    const std::size_t blend = !m.translucent ? 0 : (m.additive ? 2 : 1);
    return *pipelines_[blend][m.no_cull ? 1 : 0];
}

std::size_t WorldRenderer::build_shader_data(const WorldDrawSettings& settings) {
    auto values = styles_.values(settings.time);
    if (!settings.light_styles) {
        std::fill(values.begin() + 1, values.end(), 0.0f);
    }
    // Light styles, the fog, then as many material params rows as fit in the shader data.
    constexpr std::size_t kStyleFloats = 4 * 16;
    constexpr std::size_t kFogFloats = 8;
    constexpr std::size_t kEyeFloats = 4;
    constexpr std::size_t kHeadFloats = kStyleFloats + kFogFloats + kEyeFloats;
    constexpr std::size_t kRowFloats = sizeof(MaterialParams) / sizeof(float);
    const std::size_t rows = std::min<std::size_t>(settings.material_params ? settings.material_params->size() : 1,
                                                   (rhi::kMaxShaderDataSize / sizeof(float) - kHeadFloats) / kRowFloats);
    shader_data_.assign(kHeadFloats + std::max<std::size_t>(rows, 1) * kRowFloats, 0.0f);
    std::copy(values.begin(), values.end(), shader_data_.begin());
    const auto fog = settings.fog.rows();
    std::copy(fog.begin(), fog.end(), shader_data_.begin() + kStyleFloats);
    std::copy(settings.camera_position.begin(), settings.camera_position.end(),
              shader_data_.begin() + kStyleFloats + kFogFloats);
    for (std::size_t r = 0; r < std::max<std::size_t>(rows, 1); ++r) {
        const MaterialParams p = settings.material_params && r < rows ? (*settings.material_params)[r] : MaterialParams{};
        std::memcpy(shader_data_.data() + kHeadFloats + r * kRowFloats, &p, sizeof(p));
    }
    return std::max<std::size_t>(rows, 1);
}

void WorldRenderer::draw_batch(rhi::CommandList& cmd, const Batch& batch, const std::array<float, 16>& vp,
                               const WorldDrawSettings& settings, std::size_t rows, const rhi::Pipeline*& bound,
                               const rhi::ShaderDataRef* data) {
    if ((batch.monitor >= 0 && !settings.draw_monitors) || batch.skybox != settings.skybox) {
        return;
    }
    const RenderMaterial& m = *batch.material;
    const rhi::Pipeline& pipeline = batch.decal ? *decal_pipelines_[m.modulate ? 1 : 0] : pipeline_for(m);
    if (&pipeline != bound) {
        cmd.set_pipeline(pipeline);
        if (data) {
            cmd.bind_shader_data(*data);
        } else {
            cmd.set_shader_data(shader_data_.data(), shader_data_.size() * sizeof(float));
        }
        bound = &pipeline;
    }
    PushConstants pc{};
    std::memcpy(pc.mvp, vp.data(), sizeof(pc.mvp));
    pc.light_scale = settings.light_scale;
    pc.alpha_ref = m.alpha_ref;
    pc.flags = (m.alpha_test ? kAlphaTest : 0) | (m.blend_layers ? kBlendLayers : 0) |
               ((m.unlit || settings.fullbright) ? kUnlit : 0) | (m.self_illum ? kSelfIllum : 0) |
               ((m.translucent || batch.decal) ? kTranslucent : 0) | ((batch.decal && m.modulate) ? kModulate : 0) |
               (m.multiply_layers ? kMultiplyLayers : 0) | ((m.additive && !batch.decal) ? kAdditive : 0) |
               (m.envmap && settings.envmaps ? kEnvmap | (static_cast<std::uint32_t>(m.envmap_mask_mode) << kEnvmapMaskShift) : 0) |
               (m.bumpmap && settings.bumpmaps && !batch.decal ? kBump : 0);
    pc.params_index = m.params_index < rows ? m.params_index : 0;
    cmd.set_bind_group(batch.current_bind_group());
    cmd.push_constants(&pc, sizeof(pc));
    const formats::ViewVisibility* vis = settings.visibility;
    if (!vis || batch.runs.empty()) {
        cmd.draw_indexed(batch.index_count, batch.first_index);
        return;
    }
    // The visible runs, adjacent ones merged into one draw.
    std::uint32_t first = 0, count = 0;
    for (const Batch::Run& run : batch.runs) {
        bool visible = vis->box(run.mins, run.maxs);
        if (visible && !vis->sees_all_clusters()) {
            visible = run.clusters.empty() || std::any_of(run.clusters.begin(), run.clusters.end(),
                                                         [&](std::int32_t c) { return vis->cluster(c); });
        }
        if (!visible) {
            continue;
        }
        if (count > 0 && first + count == run.first_index) {
            count += run.index_count;
            continue;
        }
        if (count > 0) {
            cmd.draw_indexed(count, first);
        }
        first = run.first_index;
        count = run.index_count;
    }
    if (count > 0) {
        cmd.draw_indexed(count, first);
    }
}

void WorldRenderer::draw_batches(rhi::CommandList& cmd, const std::vector<Batch>& batches,
                                 const std::array<float, 16>& vp, const WorldDrawSettings& settings) {
    const std::size_t rows = build_shader_data(settings);
    const rhi::Pipeline* bound = nullptr;
    for (const Batch& batch : batches) {
        draw_batch(cmd, batch, vp, settings, rows, bound, nullptr);
    }
}

void WorldRenderer::draw_opaque(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                const WorldDrawSettings& settings) {
    if (!vertices_) {
        return;
    }
    cmd.set_pipeline(*pipelines_[0][0]);
    cmd.set_vertex_buffer(*vertices_);
    cmd.set_index_buffer(*indices_);
    draw_batches(cmd, opaque_, view_projection, settings);
    draw_batches(cmd, decals_, view_projection, settings);
}

void WorldRenderer::draw_water_surfaces(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                        const WorldDrawSettings& settings) {
    if (!vertices_) {
        return;
    }
    cmd.set_pipeline(*pipelines_[0][0]);
    cmd.set_vertex_buffer(*vertices_);
    cmd.set_index_buffer(*indices_);
    draw_water(cmd, view_projection, settings);
}

void WorldRenderer::collect_translucent(const formats::BspDrawOrder& order, const WorldDrawSettings& settings,
                                        std::uint32_t source, std::vector<TranslucentItem>& out) const {
    const auto& eye = settings.camera_position;
    for (std::size_t i = 0; i < translucent_.size(); ++i) {
        const Batch& b = translucent_[i];
        if (b.skybox != settings.skybox || (b.monitor >= 0 && !settings.draw_monitors)) {
            continue;
        }
        TranslucentItem item;
        item.slot = b.node >= 0 ? order.node_slot(static_cast<std::size_t>(b.node)) : order.nearest_slot(b.leaves);
        item.world = !b.entity;
        const float dx = b.center[0] - eye[0], dy = b.center[1] - eye[1], dz = b.center[2] - eye[2];
        item.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        item.source = source;
        item.index = static_cast<std::uint32_t>(i);
        out.push_back(item);
    }
}

void WorldRenderer::begin_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                      const WorldDrawSettings& settings) {
    pass_.view_projection = view_projection;
    pass_.settings = settings;
    pass_.rows = build_shader_data(settings);
    pass_.bound = nullptr;
    if (!vertices_) {
        return;
    }
    cmd.set_pipeline(*pipelines_[1][0]);
    pass_.data = cmd.set_shader_data(shader_data_.data(), shader_data_.size() * sizeof(float));
}

void WorldRenderer::draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index, bool rebind) {
    if (!vertices_ || index >= translucent_.size()) {
        return;
    }
    if (rebind) {
        pass_.bound = nullptr;
    }
    if (!pass_.bound) {
        cmd.set_vertex_buffer(*vertices_);
        cmd.set_index_buffer(*indices_);
    }
    draw_batch(cmd, translucent_[index], pass_.view_projection, pass_.settings, pass_.rows, pass_.bound, &pass_.data);
}

void WorldRenderer::draw_water(rhi::CommandList& cmd, const std::array<float, 16>& vp,
                               const WorldDrawSettings& settings) {
    if (water_.empty() || !settings.draw_water) {
        return;
    }
    cmd.set_pipeline(*water_pipeline_);
    // Shader data: camera position and time, the world fog, then the
    // view-projection matrix (the same for every water batch).
    const auto fog = settings.fog.rows();
    water_data_.assign({settings.camera_position[0], settings.camera_position[1], settings.camera_position[2],
                        settings.time});
    water_data_.insert(water_data_.end(), fog.begin(), fog.end());
    water_data_.insert(water_data_.end(), vp.begin(), vp.end());
    cmd.set_shader_data(water_data_.data(), water_data_.size() * sizeof(float));
    WaterPushConstants pc{};
    for (const Batch& batch : water_) {
        if (batch.skybox != settings.skybox) {
            continue;
        }
        const RenderMaterial& m = *batch.material;
        const bool targets = settings.water_targets && water_reflection_ && !batch.skybox;
        const bool refract = targets && m.refract;
        for (std::size_t k = 0; k < 3; ++k) {
            // Without a refraction, the surface shows its fog colour.
            pc.refract[k] = refract ? m.refract_tint[k] : m.fog_color[k];
            pc.reflect[k] = m.reflect_tint[k];
            pc.bump_u[k] = m.bump_transform[k];
            pc.bump_v[k] = m.bump_transform[3 + k];
        }
        pc.refract[3] = m.refract_amount;
        pc.reflect[3] = m.reflect_amount;
        // Cheap water needs no render targets, so it works in the 3D skybox too
        // (la_dane_1's open sea is skybox water).
        const bool cheap = m.envmap && (m.force_cheap || m.cheap_end > m.cheap_start);
        pc.bump_u[3] = static_cast<float>((targets && m.reflect ? kWaterReflect : 0) | (refract ? kWaterRefract : 0) |
                                          (cheap ? kWaterCheap : 0) | (cheap && m.force_cheap ? kWaterForceCheap : 0));
        for (std::size_t k = 0; k < 3; ++k) {
            pc.cheap[k] = m.fog_color[k];
        }
        // Distances are world units; the 3D skybox is drawn at 1/scale (as for its fog).
        const float scale = batch.skybox && sky_camera_ ? sky_camera_->scale : 1.0f;
        pc.cheap_range[0] = m.cheap_start / scale;
        pc.cheap_range[1] = m.cheap_end / scale;
        cmd.set_bind_group(batch.current_bind_group());
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(batch.index_count, batch.first_index);
    }
}

} // namespace openbl::render
