#include "openbl/render/sprite_renderer.hpp"

#include "openbl/formats/path.hpp"

#include "openbl_sprite_ps.h"
#include "openbl_sprite_vs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>

namespace openbl::render {

namespace {

struct PushConstants {
    float view_projection[16];
    float center[4]; // w: 1 additive, 0 alpha-blended.
    float right[4];  // w: origin u.
    float up[4];     // w: origin v.
    float color[4];  // rgb render colour, a amount.
};
static_assert(sizeof(PushConstants) == 128);

// Render modes (rendermode) and effects (renderfx) as in Source.
constexpr int kRenderGlow = 3;
constexpr int kRenderTransAdd = 5;
constexpr int kRenderWorldGlow = 9; // A glow sized in the world rather than on screen.
constexpr int kRenderFxPulseFast = 2;

bool is_glow(int mode) { return mode == kRenderGlow || mode == kRenderWorldGlow; }
// Glows do not fade with distance in Bloodlines' client code (no Half-Life 1
// style 1 / distance^2 dissipation), so renderfx 14 ("no dissipation") changes nothing.

std::string value(const formats::BspEntity& e, const char* key) {
    const auto it = e.find(key);
    return it == e.end() ? std::string() : it->second;
}

std::array<float, 3> vec3(const std::string& s) {
    std::array<float, 3> v{};
    std::istringstream(s) >> v[0] >> v[1] >> v[2];
    return v;
}

using V3 = std::array<float, 3>;
V3 scale(const V3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
V3 normalize(const V3& a) {
    const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return len > 1e-6f ? scale(a, 1.0f / len) : a;
}

// Source angles (pitch, yaw, roll in degrees) to right and up vectors.
void angle_vectors(const V3& angles, V3& right, V3& up) {
    constexpr float kRad = 3.14159265f / 180.0f;
    const float sp = std::sin(angles[0] * kRad), cp = std::cos(angles[0] * kRad);
    const float sy = std::sin(angles[1] * kRad), cy = std::cos(angles[1] * kRad);
    const float sr = std::sin(angles[2] * kRad), cr = std::cos(angles[2] * kRad);
    right = {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    up = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
}

} // namespace

SpriteRenderer::SpriteRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp,
                               const std::vector<std::string>& shown)
    : device_(device), bsp_(bsp), materials_(materials) {
    std::optional<std::uint16_t> sky_area;
    for (const auto& e : bsp.entities) {
        if (value(e, "classname") == "sky_camera") {
            const auto o = vec3(value(e, "origin"));
            sky_area = bsp.leafs[bsp.leaf_at({o[0], o[1], o[2]})].area;
        }
    }
    for (const auto& e : bsp.entities) {
        if (value(e, "classname") != "env_sprite") {
            continue;
        }
        const bool named = std::find(shown.begin(), shown.end(), value(e, "targetname")) != shown.end();
        const int flags = std::atoi(value(e, "spawnflags").c_str());
        // "Start on" unset: a script switches it on.
        const bool off = !named && (!(flags & 1) || value(e, "StartHidden") == "1");
        stats_.off += off;
        // "materials/sprites/glowa.vmt" (or an old-style "sprites/x.spr") -> material "sprites/glowa".
        std::string name = formats::normalize_game_path(value(e, "model"));
        if (name.starts_with("materials/")) {
            name = name.substr(10);
        }
        if (const auto dot = name.rfind('.'); dot != std::string::npos) {
            name = name.substr(0, dot);
        }
        const RenderMaterial& m = materials.material(name);
        if (m.missing || m.skip) {
            ++stats_.missing_materials;
            continue;
        }
        Sprite s;
        s.material = &m;
        s.origin = vec3(value(e, "origin"));
        s.angles = vec3(value(e, "angles"));
        const auto c = vec3(value(e, "rendercolor"));
        s.color = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f};
        if (value(e, "rendercolor").empty()) {
            s.color = {1, 1, 1};
        }
        const std::string amt = value(e, "renderamt");
        s.amount = amt.empty() ? 1.0f : std::clamp(static_cast<float>(std::atof(amt.c_str())) / 255.0f, 0.0f, 1.0f);
        s.mode = std::atoi(value(e, "rendermode").c_str());
        s.fx = std::atoi(value(e, "renderfx").c_str());
        const std::string sc = value(e, "scale");
        s.scale = sc.empty() ? 1.0f : static_cast<float>(std::atof(sc.c_str()));
        const std::string fr = value(e, "framerate");
        s.framerate = fr.empty() ? 10.0f : static_cast<float>(std::atof(fr.c_str()));
        s.skybox = sky_area && bsp.leafs[bsp.leaf_at({s.origin[0], s.origin[1], s.origin[2]})].area == *sky_area;
        // Its bounds, for the drawing order: half the picture's larger side around the origin.
        const float r = 0.5f * static_cast<float>(std::max(m.base->desc().width, m.base->desc().height)) * s.scale;
        s.leaves = formats::leaves_in_box(bsp, {s.origin[0] - r, s.origin[1] - r, s.origin[2] - r},
                                          {s.origin[0] + r, s.origin[1] + r, s.origin[2] + r});
        s.phase = static_cast<float>(&e - bsp.entities.data()) * 363.0f; // Pulses desynchronised per entity.
        s.entity = static_cast<std::int32_t>(&e - bsp.entities.data());
        s.hidden = off;
        sprites_.push_back(s);
    }
    stats_.sprites = static_cast<std::size_t>(
        std::count_if(sprites_.begin(), sprites_.end(), [](const Sprite& s) { return !s.hidden; }));

    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_sprite_vs;
    pd.pixel.spirv = openbl_sprite_ps;
    pd.vertex_stride = 3 * sizeof(float);
    pd.attributes = {{0, rhi::Format::RGB32_Float, 0}};
    pd.texture_count = 1;
    pd.push_constant_size = sizeof(PushConstants);
    pd.shader_data = true;
    pd.cull = rhi::CullMode::None;
    pd.depth_write = false;
    pd.blend = rhi::BlendMode::Premultiplied;
    pipeline_ = device.create_pipeline(pd);
    rhi::SamplerDesc sd;
    sd.address = rhi::AddressMode::Clamp;
    sampler_ = device.create_sampler(sd);

    const float corners[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    const std::uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    corners_ = device.create_buffer({sizeof(corners), rhi::BufferUsage::Vertex}, std::as_bytes(std::span(corners)));
    indices_ = device.create_buffer({sizeof(indices), rhi::BufferUsage::Index}, std::as_bytes(std::span(indices)));
}

SpriteRenderer::~SpriteRenderer() = default;

const rhi::BindGroup& SpriteRenderer::bind_group(const rhi::Texture* texture) {
    auto& g = bind_groups_[texture];
    if (!g) {
        const rhi::Texture* textures[] = {texture};
        g = device_.create_bind_group(*pipeline_, *sampler_, textures);
    }
    return *g;
}

void SpriteRenderer::set_entity_states(const std::vector<EntityState>& states) {
    for (Sprite& s : sprites_) {
        s.hidden = s.entity >= 0 && static_cast<std::size_t>(s.entity) < states.size() &&
                   states[static_cast<std::size_t>(s.entity)].hidden;
    }
}

void SpriteRenderer::collect_translucent(const formats::BspDrawOrder& order, const std::array<float, 3>& eye,
                                         bool skybox, std::uint32_t source, std::vector<TranslucentItem>& out,
                                         const formats::ViewVisibility* visibility) const {
    for (std::size_t i = 0; i < sprites_.size(); ++i) {
        const Sprite& s = sprites_[i];
        if (s.hidden || s.skybox != skybox || (visibility && !visibility->any_leaf(s.leaves))) {
            continue;
        }
        TranslucentItem item;
        item.slot = order.nearest_slot(s.leaves);
        const float dx = s.origin[0] - eye[0], dy = s.origin[1] - eye[1], dz = s.origin[2] - eye[2];
        item.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        item.source = source;
        item.index = static_cast<std::uint32_t>(i);
        out.push_back(item);
    }
}

void SpriteRenderer::begin_translucent(rhi::CommandList& cmd, const std::array<float, 16>& view_projection,
                                       const std::array<float, 16>& view, const std::array<float, 3>& eye, float time,
                                       const Fog& fog, bool skybox, float world_scale) {
    pass_.view_projection = view_projection;
    pass_.view = view;
    pass_.eye = eye;
    pass_.time = time;
    pass_.skybox = skybox;
    pass_.world_scale = world_scale;
    const auto fog_rows = fog.rows();
    cmd.set_pipeline(*pipeline_);
    pass_.data = cmd.set_shader_data(fog_rows.data(), sizeof(fog_rows));
    pass_.bound = false;
}

void SpriteRenderer::draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index, bool rebind) {
    if (rebind) {
        pass_.bound = false; // Even if this sprite turns out not to be drawn.
    }
    if (index >= sprites_.size()) {
        return;
    }
    const Sprite& s = sprites_[index];
    const auto& view = pass_.view;
    const auto& eye = pass_.eye;
    const float time = pass_.time;
    // Camera axes in world space: the rows of the view rotation.
    const V3 cam_right{view[0], view[4], view[8]};
    const V3 cam_up{view[1], view[5], view[9]};
    const RenderMaterial& m = *s.material;
    const bool glow = is_glow(s.mode);
    float amount = s.amount;
    if (s.fx == kRenderFxPulseFast) {
        amount = std::clamp(amount + 16.0f / 255.0f * std::sin(time * 8.0f + s.phase), 0.0f, 1.0f);
    }
    // Material proxies that depend on this sprite's place relative to the
    // camera (PlayerProximity light cones, LampBeam, LampHalo) set $alpha.
    const V3 to_sprite{s.origin[0] - eye[0], s.origin[1] - eye[1], s.origin[2] - eye[2]};
    const float distance = std::sqrt(to_sprite[0] * to_sprite[0] + to_sprite[1] * to_sprite[1] + to_sprite[2] * to_sprite[2]);
    formats::ProxyContext context;
    context.time = time;
    context.player_distance = distance * pass_.world_scale;
    context.view_direction_z = distance > 0.0f ? to_sprite[2] / distance : 0.0f;
    amount *= materials_.entity_alpha(m, context);
    if (glow) {
        // The world hides a glow whose centre it blocks. Props don't, even
        // a lamp globe around the centre: the original's glow visibility
        // test only sees the world. The glow is depth-tested like any
        // sprite, so the globe still covers the middle of it.
        if (bsp_.segment_blocked({eye[0], eye[1], eye[2]}, {s.origin[0], s.origin[1], s.origin[2]})) {
            return;
        }
    }
    if (amount <= 0.0f) {
        return;
    }
    // The picture: an animated texture steps at the entity's frame rate.
    const rhi::Texture* texture = m.base;
    if (!m.base_frames.empty()) {
        const auto frame = static_cast<std::size_t>(std::max(0.0f, time * s.framerate));
        texture = m.base_frames[frame % m.base_frames.size()];
    }
    const float width = static_cast<float>(texture->desc().width) * s.scale;
    const float height = static_cast<float>(texture->desc().height) * s.scale;
    V3 right = cam_right, up = cam_up;
    switch (m.sprite_orientation) {
    case RenderMaterial::SpriteOrientation::ParallelUpright: {
        // Upright, turned about z to face the camera.
        up = {0, 0, 1};
        right = normalize(V3{cam_right[0], cam_right[1], 0.0f});
        break;
    }
    case RenderMaterial::SpriteOrientation::Oriented:
        angle_vectors(s.angles, right, up);
        break;
    case RenderMaterial::SpriteOrientation::ViewParallelOriented: {
        const float roll = s.angles[2] * 3.14159265f / 180.0f;
        const float c = std::cos(roll), sn = std::sin(roll);
        right = {cam_right[0] * c + cam_up[0] * sn, cam_right[1] * c + cam_up[1] * sn, cam_right[2] * c + cam_up[2] * sn};
        up = {cam_up[0] * c - cam_right[0] * sn, cam_up[1] * c - cam_right[1] * sn, cam_up[2] * c - cam_right[2] * sn};
        break;
    }
    case RenderMaterial::SpriteOrientation::ViewParallel:
        break;
    }
    const V3 r = scale(right, width), u = scale(up, height);
    PushConstants pc{};
    std::memcpy(pc.view_projection, pass_.view_projection.data(), sizeof(pc.view_projection));
    pc.center[0] = s.origin[0];
    pc.center[1] = s.origin[1];
    pc.center[2] = s.origin[2];
    pc.center[3] = (glow || s.mode == kRenderTransAdd || m.additive) ? 1.0f : 0.0f;
    std::copy(r.begin(), r.end(), pc.right);
    pc.right[3] = m.sprite_origin[0];
    std::copy(u.begin(), u.end(), pc.up);
    pc.up[3] = m.sprite_origin[1];
    std::copy(s.color.begin(), s.color.end(), pc.color);
    pc.color[3] = amount;

    if (!pass_.bound) {
        cmd.set_pipeline(*pipeline_);
        cmd.set_vertex_buffer(*corners_);
        cmd.set_index_buffer(*indices_);
        cmd.bind_shader_data(pass_.data);
        pass_.bound = true;
    }
    cmd.set_bind_group(bind_group(texture));
    cmd.push_constants(&pc, sizeof(pc));
    cmd.draw_indexed(6);
}

} // namespace openbl::render
