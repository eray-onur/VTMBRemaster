#include "openbl/render/prop_lighting.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::render {

namespace {

using formats::BspLightType;
using formats::Vec3;

// Rays traced to gather the surrounding lightmaps.
constexpr std::size_t kAmbientRays = 162;
constexpr float kAmbientRayLength = 4096.0f;

// Weight of the gathered lightmaps. Fitted against original-game captures of
// sp_tutorial_1 (docs/PARITY.md); one room so far.
constexpr float kAmbientWeight = 0.5f;

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

PropLighting::PropLighting(const formats::BspFile& bsp) : bsp_(bsp), styles_(bsp) {
    // Evenly spread directions (Fibonacci sphere).
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (std::size_t i = 0; i < kAmbientRays; ++i) {
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / kAmbientRays;
        const float r = std::sqrt(1.0f - z * z);
        const float a = golden * static_cast<float>(i);
        directions_.push_back({r * std::cos(a), r * std::sin(a), z});
    }
}

AmbientCube PropLighting::sample(const Vec3& point, const std::array<std::array<float, 3>, 3>& rotation) const {
    AmbientCube cube;
    // Model-space axis k in world space is column k of the rotation.
    std::array<Vec3, 3> axes{};
    for (std::size_t k = 0; k < 3; ++k) {
        axes[k] = {rotation[0][k], rotation[1][k], rotation[2][k]};
    }

    for (const auto& light : bsp_.world_lights) {
        const float style = light.style >= 0 ? styles_.default_value(static_cast<std::size_t>(light.style)) : 0.0f;
        if (style <= 0 ||
            (light.type != BspLightType::Point && light.type != BspLightType::Spot && light.type != BspLightType::Surface)) {
            continue;
        }
        Vec3 to_light{light.origin[0] - point[0], light.origin[1] - point[1], light.origin[2] - point[2]};
        const float dist = std::sqrt(dot(to_light, to_light));
        if (dist < 1e-3f || (light.radius > 0 && dist > light.radius)) {
            continue;
        }
        for (float& c : to_light) {
            c /= dist;
        }
        // Emissive-surface lights store no attenuation terms (all zero); they
        // fall off with the inverse square of the distance.
        const bool unattenuated = light.constant_attn == 0 && light.linear_attn == 0 && light.quadratic_attn == 0;
        const float r = std::max(dist, 1.0f);
        const float attenuation =
            unattenuated ? r * r : light.constant_attn + light.linear_attn * r + light.quadratic_attn * r * r;
        float scale = style / std::max(attenuation, 1.0f);
        if (light.type == BspLightType::Spot) {
            const float d = -dot(to_light, light.normal);
            if (d <= light.stopdot2) {
                continue;
            }
            if (d < light.stopdot) {
                float falloff = (d - light.stopdot2) / (light.stopdot - light.stopdot2);
                if (light.exponent != 0 && light.exponent != 1) {
                    falloff = std::pow(falloff, light.exponent);
                }
                scale *= falloff;
            }
        } else if (light.type == BspLightType::Surface) {
            scale *= std::max(0.0f, -dot(to_light, light.normal));
        }
        if (scale <= 0 || bsp_.segment_blocked(point, light.origin)) {
            continue;
        }
        // Distribute over the six model-space directions by cosine.
        for (std::size_t k = 0; k < 3; ++k) {
            const float c = dot(to_light, axes[k]);
            auto& face = cube.color[k * 2 + (c >= 0 ? 0 : 1)];
            for (std::size_t ch = 0; ch < 3; ++ch) {
                face[ch] += light.intensity[ch] * scale * std::abs(c);
            }
        }
    }

    // Indirect light: the lightmaps of the surfaces seen from the point,
    // averaged per cube direction with cosine weights. Rays that leave the
    // world or hit unlit surfaces count as black.
    std::array<std::array<float, 3>, 6> sum{};
    std::array<float, 6> weight{};
    for (const Vec3& d : directions_) {
        const Vec3 end{point[0] + d[0] * kAmbientRayLength, point[1] + d[1] * kAmbientRayLength,
                       point[2] + d[2] * kAmbientRayLength};
        std::array<float, 3> color{};
        if (const auto hit = bsp_.trace_lightmap(point, end)) {
            color = lightmap_at(*hit);
        }
        for (std::size_t k = 0; k < 3; ++k) {
            const float c = dot(d, axes[k]);
            const std::size_t f = k * 2 + (c >= 0 ? 0 : 1);
            weight[f] += std::abs(c);
            for (std::size_t ch = 0; ch < 3; ++ch) {
                sum[f][ch] += color[ch] * std::abs(c);
            }
        }
    }
    for (std::size_t f = 0; f < 6; ++f) {
        for (std::size_t ch = 0; ch < 3; ++ch) {
            cube.color[f][ch] += weight[f] > 0 ? sum[f][ch] / weight[f] * kAmbientWeight : 0.0f;
        }
    }
    return cube;
}

std::array<float, 3> PropLighting::lightmap_at(const formats::BspSurfaceHit& hit) const {
    const auto& face = bsp_.faces[hit.face];
    const std::size_t width = static_cast<std::size_t>(face.lightmap_size[0]) + 1;
    const std::size_t height = static_cast<std::size_t>(face.lightmap_size[1]) + 1;
    const std::size_t x = std::min(static_cast<std::size_t>(std::lround(hit.luxel[0])), width - 1);
    const std::size_t y = std::min(static_cast<std::size_t>(std::lround(hit.luxel[1])), height - 1);
    const std::size_t maps = (bsp_.texinfos[face.texinfo].flags & formats::surf_flags::BumpLight) ? 4 : 1;
    std::array<float, 3> out{};
    for (std::size_t s = 0; s < face.style_count(); ++s) {
        const float value = styles_.default_value(face.styles[s]);
        if (value <= 0) {
            continue;
        }
        const std::byte* luxel =
            bsp_.lighting.data() + face.light_offset + ((s * maps * height + y) * width + x) * 4;
        const auto c = formats::decode_rgbe(luxel);
        for (std::size_t ch = 0; ch < 3; ++ch) {
            out[ch] += c[ch] * value;
        }
    }
    return out;
}

} // namespace openbl::render
