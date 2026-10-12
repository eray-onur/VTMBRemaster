#include "openbl/render/particle_renderer.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/image_read.hpp"

#include "openbl_particle_ps.h"
#include "openbl_particle_vs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>

namespace openbl::render {

namespace {

// Particles per draw: 4 float4 each, within the shader-data limit.
constexpr std::size_t kFloatsPerParticle = 16;
constexpr std::size_t kMaxPerDraw = rhi::kMaxShaderDataSize / (kFloatsPerParticle * sizeof(float));

struct PushConstants {
    float view_projection[16];
    float right[4]; // w: fog start.
    float up[4];    // w: fog end.
    float eye[4];
    float fog[4];   // rgb: fog colour, a: enabled.
};
static_assert(sizeof(PushConstants) == 128);

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string value(const formats::BspEntity& e, const char* key) {
    const auto it = e.find(key);
    return it == e.end() ? std::string() : it->second;
}

} // namespace

ParticleRenderer::ParticleRenderer(rhi::Device& device, const AssetSource& assets, const formats::BspFile& bsp,
                                   const std::vector<std::string>& shown)
    : device_(device), assets_(assets), bsp_(bsp), library_([&assets](const std::string& path) -> std::optional<std::string> {
          auto data = assets.read(path);
          if (!data) {
              return std::nullopt;
          }
          return std::string(reinterpret_cast<const char*>(data->data()), data->size());
      }) {
    std::uint32_t seed = 1;
    std::optional<std::uint16_t> sky_area;
    for (const auto& e : bsp.entities) {
        if (value(e, "classname") == "sky_camera") {
            formats::Vec3 o{};
            std::istringstream(value(e, "origin")) >> o[0] >> o[1] >> o[2];
            sky_area = bsp.leafs[bsp.leaf_at(o)].area;
        }
    }
    for (const auto& e : bsp.entities) {
        if (value(e, "classname") != "env_particle") {
            continue;
        }
        const bool named = std::find(shown.begin(), shown.end(), value(e, "targetname")) != shown.end();
        if (!named && (value(e, "active") != "1" || value(e, "StartHidden") == "1")) {
            continue; // Started later by the map's scripts.
        }
        Emitter em;
        em.definition = value(e, "particle_definition");
        std::istringstream(value(e, "origin")) >> em.origin[0] >> em.origin[1] >> em.origin[2];
        std::istringstream(value(e, "angles")) >> em.angles[0] >> em.angles[1] >> em.angles[2];
        em.seed = seed++;
        em.skybox = sky_area && bsp.leafs[bsp.leaf_at(em.origin)].area == *sky_area;
        em.system = std::make_unique<particles::ParticleSystem>(library_, em.definition, em.origin, em.seed, em.angles);
        if (!em.system->valid()) {
            ++stats_.missing_definitions;
            continue;
        }
        emitters_.push_back(std::move(em));
    }
    stats_.systems = emitters_.size();

    rhi::PipelineDesc pd;
    pd.vertex.spirv = openbl_particle_vs;
    pd.pixel.spirv = openbl_particle_ps;
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

    // One quad per particle slot: corners (-1,-1), (1,-1), (1,1), (-1,1) and the slot index.
    std::vector<float> corners;
    std::vector<std::uint32_t> indices;
    static constexpr float kCorner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (std::size_t i = 0; i < kMaxPerDraw; ++i) {
        for (const auto& c : kCorner) {
            corners.insert(corners.end(), {c[0], c[1], static_cast<float>(i)});
        }
        const auto b = static_cast<std::uint32_t>(i * 4);
        indices.insert(indices.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    corners_ = device.create_buffer({corners.size() * sizeof(float), rhi::BufferUsage::Vertex},
                                    std::as_bytes(std::span(corners)));
    indices_ = device.create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                    std::as_bytes(std::span(indices)));
    const std::uint8_t white[4] = {255, 255, 255, 255};
    const std::span<const std::byte> mip = std::as_bytes(std::span(white));
    white_ = device.create_texture({1, 1, 1, rhi::Format::RGBA8_sRGB}, std::span(&mip, 1));
}

ParticleRenderer::~ParticleRenderer() = default;

const rhi::Texture* ParticleRenderer::sprite(const std::string& name) {
    if (auto it = sprite_by_definition_.find(&name); it != sprite_by_definition_.end()) {
        return it->second;
    }
    const std::string key = lower(name);
    auto it = sprites_.find(key);
    if (it == sprites_.end()) {
        std::unique_ptr<rhi::Texture> tex;
        if (auto data = assets_.read("particles/" + key + ".tga")) {
            try {
                const auto image = formats::decode_tga(*data);
                const std::span<const std::byte> mip = std::as_bytes(std::span(image.rgba));
                tex = device_.create_texture({image.width, image.height, 1, rhi::Format::RGBA8_sRGB}, std::span(&mip, 1));
            } catch (const formats::FormatError& e) {
                std::fprintf(stderr, "particle sprite %s: %s\n", key.c_str(), e.what());
            }
        }
        stats_.missing_sprites += !tex;
        it = sprites_.emplace(key, std::move(tex)).first;
    }
    const rhi::Texture* resolved = it->second ? it->second.get() : white_.get();
    sprite_by_definition_.emplace(&name, resolved);
    return resolved;
}

void ParticleRenderer::update(float time) {
    if (time < time_) {
        for (Emitter& em : emitters_) {
            em.system = std::make_unique<particles::ParticleSystem>(library_, em.definition, em.origin, em.seed, em.angles);
        }
        time_ = 0;
    }
    for (Emitter& em : emitters_) {
        em.system->advance(time - time_);
    }
    time_ = time;
}

void ParticleRenderer::collect_translucent(const formats::BspDrawOrder& order, const std::array<float, 3>& eye,
                                           bool skybox, std::uint32_t source, std::vector<TranslucentItem>& out,
                                           const formats::ViewVisibility* visibility) {
    for (std::size_t i = 0; i < emitters_.size(); ++i) {
        Emitter& em = emitters_[i];
        em.quads.clear();
        if (em.skybox != skybox) {
            continue;
        }
        em.system->collect(em.quads);
        if (em.quads.empty()) {
            continue;
        }
        formats::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const auto& q : em.quads) {
            const float r = 0.5f * std::max(q.width, q.height);
            for (std::size_t k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], q.position[k] - r);
                hi[k] = std::max(hi[k], q.position[k] + r);
            }
        }
        const auto leaves = formats::leaves_in_box(bsp_, lo, hi);
        if (visibility && !visibility->object(leaves, lo, hi)) {
            em.quads.clear();
            continue;
        }
        TranslucentItem item;
        item.slot = order.nearest_slot(leaves);
        const float dx = em.origin[0] - eye[0], dy = em.origin[1] - eye[1], dz = em.origin[2] - eye[2];
        item.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        item.source = source;
        item.index = static_cast<std::uint32_t>(i);
        out.push_back(item);
    }
}

void ParticleRenderer::begin_translucent(const std::array<float, 16>& view_projection,
                                         const std::array<float, 3>& right, const std::array<float, 3>& up,
                                         const std::array<float, 3>& eye, const Fog& fog) {
    pass_view_projection_ = view_projection;
    pass_right_ = right;
    pass_up_ = up;
    pass_eye_ = eye;
    pass_fog_ = fog;
}

void ParticleRenderer::draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index) {
    if (index >= emitters_.size() || emitters_[index].quads.empty()) {
        return;
    }
    auto& quads = emitters_[index].quads;
    // Back to front within the effect.
    const auto& eye = pass_eye_;
    auto distance = [&](const particles::ParticleQuad& q) {
        const float dx = q.position[0] - eye[0], dy = q.position[1] - eye[1], dz = q.position[2] - eye[2];
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(quads.begin(), quads.end(), [&](const auto& a, const auto& b) { return distance(a) > distance(b); });

    PushConstants pc{};
    std::memcpy(pc.view_projection, pass_view_projection_.data(), sizeof(pc.view_projection));
    std::copy(pass_right_.begin(), pass_right_.end(), pc.right);
    std::copy(pass_up_.begin(), pass_up_.end(), pc.up);
    std::copy(eye.begin(), eye.end(), pc.eye);
    pc.right[3] = pass_fog_.start;
    pc.up[3] = pass_fog_.end;
    std::copy(pass_fog_.color.begin(), pass_fog_.color.end(), pc.fog);
    pc.fog[3] = pass_fog_.mode();
    pc.eye[3] = pass_fog_.water_z;
    cmd.set_pipeline(*pipeline_);
    cmd.set_vertex_buffer(*corners_);
    cmd.set_index_buffer(*indices_);
    cmd.push_constants(&pc, sizeof(pc));

    // Consecutive particles with the same sprite share a draw.
    std::size_t start = 0;
    while (start < quads.size()) {
        const rhi::Texture* tex = sprite(*quads[start].sprite);
        std::size_t end = start;
        data_.clear();
        while (end < quads.size() && end - start < kMaxPerDraw && sprite(*quads[end].sprite) == tex) {
            const auto& q = quads[end];
            data_.insert(data_.end(), {q.position[0], q.position[1], q.position[2], q.width, q.height, q.rotation,
                                       q.align ? 1.0f : 0.0f, 0.0f, q.color[0], q.color[1], q.color[2], q.mask,
                                       q.velocity[0], q.velocity[1], q.velocity[2], 0.0f});
            ++end;
        }
        auto& group = bind_groups_[tex];
        if (!group) {
            const rhi::Texture* textures[] = {tex};
            group = device_.create_bind_group(*pipeline_, *sampler_, textures);
        }
        cmd.set_bind_group(*group);
        cmd.set_shader_data(data_.data(), data_.size() * sizeof(float));
        cmd.draw_indexed(static_cast<std::uint32_t>((end - start) * 6), 0);
        start = end;
    }
}

} // namespace openbl::render
