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
};
static_assert(sizeof(Vertex) == 40);

// Lightmap styles drawn per face (faces store up to 8; the Steam release uses at most 4).
constexpr std::size_t kMaxFaceStyles = 4;
constexpr std::int32_t kSurfBumpLight = formats::surf_flags::BumpLight;

struct PushConstants {
    float mvp[16];
    float alpha_ref;
    float light_scale;
    std::uint32_t flags;
    float padding;
};
static_assert(sizeof(PushConstants) == 80);

struct WaterPushConstants {
    float mvp[16];
    float camera_time[4];
    float fog_color[4];
    float reflect_tint[4];
};
static_assert(sizeof(WaterPushConstants) == 112);

// Water opacity; the original refracts what is below instead.
constexpr float kWaterOpacity = 0.85f;

constexpr std::uint32_t kAlphaTest = 1;
constexpr std::uint32_t kBlendLayers = 2;
constexpr std::uint32_t kUnlit = 4;
constexpr std::uint32_t kSelfIllum = 8;
constexpr std::uint32_t kTranslucent = 16;
constexpr std::uint32_t kModulate = 32;

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

// A brush model instance: the world (model 0) or a brush entity at its origin.
struct ModelInstance {
    std::size_t model = 0;
    formats::Vec3 origin{};
};

std::vector<ModelInstance> model_instances(const BspFile& bsp) {
    std::vector<ModelInstance> out{{0, {}}};
    for (const auto& e : bsp.entities) {
        auto model = e.find("model");
        if (model == e.end() || model->second.size() < 2 || model->second[0] != '*') {
            continue;
        }
        const std::size_t index = std::strtoul(model->second.c_str() + 1, nullptr, 10);
        if (index == 0 || index >= bsp.models.size()) {
            continue;
        }
        ModelInstance inst{index, {}};
        if (auto origin = e.find("origin"); origin != e.end()) {
            std::istringstream(origin->second) >> inst.origin[0] >> inst.origin[1] >> inst.origin[2];
        }
        out.push_back(inst);
    }
    return out;
}

} // namespace

WorldRenderer::WorldRenderer(rhi::Device& device, MaterialSystem& materials, const BspFile& bsp)
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
    std::map<std::string, std::vector<FaceRef>> by_material;
    for (const ModelInstance& inst : model_instances(bsp)) {
        const auto& model = bsp.models[inst.model];
        for (std::int32_t i = 0; i < model.num_faces; ++i) {
            const std::size_t f = static_cast<std::size_t>(model.first_face + i);
            if (!face_drawn(bsp, bsp.faces[f])) {
                ++stats_.skipped_faces;
                continue;
            }
            const auto texdata = static_cast<std::size_t>(bsp.texinfos[bsp.faces[f].texinfo].texdata);
            by_material[bsp.texdatas[texdata].material].push_back({f, inst.origin});
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
            out.style_step = static_cast<float>(face.lightmap_size[0] + 2) * inv_w;
        } else {
            out.styles[0] = 0; // The white luxel, at full brightness.
        }
    };
    for (const auto& [name, faces] : by_material) {
        const RenderMaterial& mat = materials.material(name);
        stats_.missing_materials += mat.missing;
        if (mat.skip) {
            stats_.skipped_faces += faces.size();
            continue;
        }
        Batch batch;
        batch.material = &mat;
        batch.first_index = static_cast<std::uint32_t>(indices.size());
        for (const FaceRef& ref : faces) {
            const auto mesh = formats::build_face_mesh(bsp, ref.face);
            const auto base = static_cast<std::uint32_t>(vertices.size());
            for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
                Vertex out{};
                for (int k = 0; k < 3; ++k) {
                    out.position[k] = mesh.positions[v][k] + ref.origin[k];
                }
                out.uv[0] = mesh.uvs[v][0];
                out.uv[1] = mesh.uvs[v][1];
                light_vertex(out, ref.face, mesh.lightmap_uvs[v]);
                out.blend = mesh.alphas[v];
                vertices.push_back(out);
            }
            for (std::uint32_t idx : mesh.indices) {
                indices.push_back(base + idx);
            }
            ++stats_.faces;
        }
        batch.index_count = static_cast<std::uint32_t>(indices.size()) - batch.first_index;
        stats_.triangles += batch.index_count / 3;
        (mat.water ? water_ : mat.translucent ? translucent_ : opaque_).push_back(std::move(batch));
    }

    // infodecals: the base texture's size times $decalscale, projected onto
    // nearby brush faces and lifted slightly off them to avoid z-fighting.
    constexpr float kDecalLift = 0.1f;
    std::map<const RenderMaterial*, std::vector<formats::BspDecalPiece>> decal_pieces;
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
        auto& list = decal_pieces[&mat];
        list.insert(list.end(), std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
    }
    for (const auto& [mat, pieces] : decal_pieces) {
        Batch batch;
        batch.material = mat;
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
                     {5, rhi::Format::R32_Float, 36}};
    pd.texture_count = 3;
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
    wd.texture_count = 1;
    wd.push_constant_size = sizeof(WaterPushConstants);
    wd.cull = rhi::CullMode::None;
    wd.blend = rhi::BlendMode::Alpha;
    wd.depth_write = false;
    water_pipeline_ = device.create_pipeline(wd);

    sampler_ = device.create_sampler({});
    for (auto* list : {&opaque_, &translucent_}) {
        for (Batch& batch : *list) {
            const rhi::Texture* textures[] = {batch.material->base, batch.material->base2, lightmap_.get()};
            batch.bind_group = device.create_bind_group(*pipelines_[0][0], *sampler_, textures);
        }
    }
    for (Batch& batch : water_) {
        const rhi::Texture* textures[] = {batch.material->normal_map};
        batch.bind_group = device.create_bind_group(*water_pipeline_, *sampler_, textures);
    }
    rhi::SamplerDesc clamped;
    clamped.address = rhi::AddressMode::Clamp;
    decal_sampler_ = device.create_sampler(clamped);
    for (Batch& batch : decals_) {
        const rhi::Texture* textures[] = {batch.material->base, batch.material->base2, lightmap_.get()};
        batch.bind_group = device.create_bind_group(*decal_pipelines_[0], *decal_sampler_, textures);
    }
}

WorldRenderer::~WorldRenderer() = default;

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
        const auto styles = static_cast<std::uint32_t>(face_styles(face));
        const auto w = static_cast<std::uint32_t>(face.lightmap_size[0] + 1) * styles + (styles - 1);
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
        for (std::size_t k = 0; k < face_styles(face); ++k) {
            const std::byte* src = bsp.lighting.data() + face.light_offset + k * luxels * 4 * (bump ? 4 : 1);
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

void WorldRenderer::draw_batches(rhi::CommandList& cmd, const std::vector<Batch>& batches,
                                 const std::array<float, 16>& vp, const WorldDrawSettings& settings) {
    const rhi::Pipeline* bound = nullptr;
    PushConstants pc{};
    std::memcpy(pc.mvp, vp.data(), sizeof(pc.mvp));
    pc.light_scale = settings.light_scale;
    auto values = styles_.values(settings.time);
    if (!settings.light_styles) {
        std::fill(values.begin() + 1, values.end(), 0.0f);
    }
    std::copy(values.begin(), values.end(), style_values_.begin());
    for (const Batch& batch : batches) {
        const RenderMaterial& m = *batch.material;
        const rhi::Pipeline& pipeline = batch.decal ? *decal_pipelines_[m.modulate ? 1 : 0] : pipeline_for(m);
        if (&pipeline != bound) {
            cmd.set_pipeline(pipeline);
            cmd.set_shader_data(style_values_.data(), sizeof(style_values_));
            bound = &pipeline;
        }
        pc.alpha_ref = m.alpha_ref;
        pc.flags = (m.alpha_test ? kAlphaTest : 0) | (m.blend_layers ? kBlendLayers : 0) |
                   ((m.unlit || settings.fullbright) ? kUnlit : 0) | (m.self_illum ? kSelfIllum : 0) |
                   ((m.translucent || batch.decal) ? kTranslucent : 0) | ((batch.decal && m.modulate) ? kModulate : 0);
        cmd.set_bind_group(*batch.bind_group);
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(batch.index_count, batch.first_index);
    }
}

void WorldRenderer::draw(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                         const WorldDrawSettings& settings) {
    draw_opaque(cmd, view_projection, settings);
    draw_translucent(cmd, view_projection, settings);
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

void WorldRenderer::draw_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                     const WorldDrawSettings& settings) {
    if (!vertices_) {
        return;
    }
    cmd.set_pipeline(*pipelines_[0][0]);
    cmd.set_vertex_buffer(*vertices_);
    cmd.set_index_buffer(*indices_);
    draw_water(cmd, view_projection, settings);
    draw_batches(cmd, translucent_, view_projection, settings);
}

void WorldRenderer::draw_water(rhi::CommandList& cmd, const std::array<float, 16>& vp,
                               const WorldDrawSettings& settings) {
    if (water_.empty()) {
        return;
    }
    cmd.set_pipeline(*water_pipeline_);
    WaterPushConstants pc{};
    std::memcpy(pc.mvp, vp.data(), sizeof(pc.mvp));
    for (int k = 0; k < 3; ++k) {
        pc.camera_time[k] = settings.camera_position[static_cast<std::size_t>(k)];
    }
    pc.camera_time[3] = settings.time;
    for (const Batch& batch : water_) {
        const RenderMaterial& m = *batch.material;
        for (std::size_t k = 0; k < 3; ++k) {
            pc.fog_color[k] = m.fog_color[k];
            pc.reflect_tint[k] = m.reflect_tint[k];
        }
        pc.fog_color[3] = kWaterOpacity;
        cmd.set_bind_group(*batch.bind_group);
        cmd.push_constants(&pc, sizeof(pc));
        cmd.draw_indexed(batch.index_count, batch.first_index);
    }
}

} // namespace openbl::render
