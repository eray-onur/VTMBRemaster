#include "openbl/render/prop_lighting.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::render {

namespace {

using formats::BspLightType;
using formats::Vec3;

// Share of the floor's baked light used as uniform bounce light. An estimate
// until the original engine's ambient term is reproduced (see docs/ROADMAP.md).
constexpr float kIndirectFraction = 0.3f;

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

PropLighting::PropLighting(const formats::BspFile& bsp) : bsp_(bsp), floor_(bsp), styles_(bsp) {}

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

    if (auto floor = floor_.sample(point)) {
        for (auto& face : cube.color) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                face[ch] += (*floor)[ch] * kIndirectFraction;
            }
        }
    }
    return cube;
}

} // namespace openbl::render
