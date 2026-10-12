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
// How far the sun's ray looks for the sky.
constexpr float kSunRayLength = 65536.0f;

// Weight of the gathered lightmaps. Fitted against original-game captures of
// sp_tutorial_1 (docs/PARITY.md); one room so far. Rays that reach the sky
// take the sky's ambient light at full weight: a face open to the whole sky
// then gets what an upward world surface there gets.
constexpr float kAmbientWeight = 0.5f;

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float luminance(const std::array<float, 3>& c) {
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

// Adds `color` arriving along unit `to_light` to the cube's faces, spread
// over the six model-space directions by cosine.
void fold(AmbientCube& cube, const std::array<Vec3, 3>& axes, const Vec3& to_light, const std::array<float, 3>& color) {
    for (std::size_t k = 0; k < 3; ++k) {
        const float c = dot(to_light, axes[k]);
        auto& face = cube.color[k * 2 + (c >= 0 ? 0 : 1)];
        for (std::size_t ch = 0; ch < 3; ++ch) {
            face[ch] += color[ch] * std::abs(c);
        }
    }
}

} // namespace

std::array<float, kLocalLightFloats> pack_local_lights(const ModelLighting& lighting,
                                                       const std::array<float, 16>& m, float scale) {
    // World to model space: the transposed rotation (column k of the matrix
    // is model axis k in world space).
    auto to_model_direction = [&](const Vec3& d) {
        return Vec3{m[0] * d[0] + m[1] * d[1] + m[2] * d[2], m[4] * d[0] + m[5] * d[1] + m[6] * d[2],
                    m[8] * d[0] + m[9] * d[1] + m[10] * d[2]};
    };
    std::array<float, kLocalLightFloats> out{};
    for (std::size_t i = 0; i < lighting.lights.size(); ++i) {
        const LocalLight& l = lighting.lights[i];
        float* r = out.data() + i * 16;
        if (l.type == LocalLight::Type::None) {
            continue;
        }
        Vec3 p;
        if (l.type == LocalLight::Type::Directional) {
            p = to_model_direction({-l.direction[0], -l.direction[1], -l.direction[2]});
        } else {
            p = to_model_direction({l.position[0] - m[12], l.position[1] - m[13], l.position[2] - m[14]});
        }
        const Vec3 spot = to_model_direction(l.direction);
        const float values[16] = {p[0], p[1], p[2], 0.0f,
                                  l.color[0] * scale, l.color[1] * scale, l.color[2] * scale, static_cast<float>(l.type),
                                  l.attenuation[0], l.attenuation[1], l.attenuation[2], l.exponent,
                                  spot[0] * l.spot_scale, spot[1] * l.spot_scale, spot[2] * l.spot_scale,
                                  -l.stopdot2 * l.spot_scale};
        std::copy(std::begin(values), std::end(values), r);
    }
    return out;
}

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

ModelLighting PropLighting::sample(const Vec3& point, const std::array<std::array<float, 3>, 3>& rotation,
                                   const std::array<Vec3, 2>& bounds) const {
    ModelLighting out;
    // Model-space axis k in world space is column k of the rotation.
    std::array<Vec3, 3> axes{};
    for (std::size_t k = 0; k < 3; ++k) {
        axes[k] = {rotation[0][k], rotation[1][k], rotation[2][k]};
    }

    // Every visible light, with what it brings to the point (before n.l).
    struct Candidate {
        LocalLight light;
        Vec3 to_light{};
        std::array<float, 3> arriving{};
    };
    std::vector<Candidate> visible;
    std::array<float, 3> sky_ambient{};
    for (const auto& light : bsp_.world_lights) {
        const float style = light.style >= 0 ? styles_.default_value(static_cast<std::size_t>(light.style)) : 0.0f;
        if (style <= 0) {
            continue;
        }
        if (light.type == BspLightType::SkyAmbient) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                sky_ambient[ch] += light.intensity[ch] * style;
            }
            continue;
        }
        Candidate c;
        c.light.color = {light.intensity[0] * style, light.intensity[1] * style, light.intensity[2] * style};
        if (light.type == BspLightType::Sky) {
            // The sun: a directional light, where the sky is seen against it.
            c.to_light = {-light.normal[0], -light.normal[1], -light.normal[2]};
            const Vec3 end{point[0] + c.to_light[0] * kSunRayLength, point[1] + c.to_light[1] * kSunRayLength,
                           point[2] + c.to_light[2] * kSunRayLength};
            bool sky = false;
            bsp_.trace_lightmap(point, end, &sky);
            if (!sky) {
                continue;
            }
            c.light.type = LocalLight::Type::Directional;
            c.light.direction = light.normal;
            c.arriving = c.light.color;
            visible.push_back(c);
            continue;
        }
        if (light.type != BspLightType::Point && light.type != BspLightType::Spot &&
            light.type != BspLightType::Surface) {
            continue;
        }
        Vec3 to_light{light.origin[0] - point[0], light.origin[1] - point[1], light.origin[2] - point[2]};
        const float dist_sq = dot(to_light, to_light);
        const float dist = std::sqrt(dist_sq);
        // The radius cuts the light off, as in the map compiler's lightmaps.
        // (The shaders' saturate(1 - d^2 w) range term with w = 1/radius^2
        // made props 20% darker than the original's; docs/RENDERING.md.)
        if (dist < 1e-3f || (light.radius > 0 && dist > light.radius)) {
            continue;
        }
        for (float& v : to_light) {
            v /= dist;
        }
        // Emissive-surface lights store no attenuation terms (all zero); they
        // fall off with the inverse square of the distance.
        const bool unattenuated = light.constant_attn == 0 && light.linear_attn == 0 && light.quadratic_attn == 0;
        const float r = std::max(dist, 1.0f);
        const float attenuation =
            unattenuated ? r * r : light.constant_attn + light.linear_attn * r + light.quadratic_attn * r * r;
        float scale = 1.0f / std::max(attenuation, 1.0f);
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
        c.to_light = to_light;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            c.arriving[ch] = c.light.color[ch] * scale;
        }
        const bool inside = light.origin[0] >= bounds[0][0] && light.origin[0] <= bounds[1][0] &&
                            light.origin[1] >= bounds[0][1] && light.origin[1] <= bounds[1][1] &&
                            light.origin[2] >= bounds[0][2] && light.origin[2] <= bounds[1][2];
        if (light.type != BspLightType::Surface && !inside) {
            c.light.type = light.type == BspLightType::Spot ? LocalLight::Type::Spot : LocalLight::Type::Point;
            c.light.position = light.origin;
            c.light.direction = light.normal;
            c.light.attenuation = {light.constant_attn, light.linear_attn, light.quadratic_attn};
            if (light.type == BspLightType::Spot) {
                // Exponents 0 and 1 both give a linear ramp, as above.
                c.light.stopdot2 = light.stopdot2;
                c.light.spot_scale = 1.0f / std::max(light.stopdot - light.stopdot2, 1e-4f);
                c.light.exponent = light.exponent != 0 ? light.exponent : 1.0f;
            }
        }
        visible.push_back(c);
    }

    // The two brightest point, spot or sun lights outside the model become
    // local lights; the rest are folded into the cube.
    std::stable_sort(visible.begin(), visible.end(), [](const Candidate& a, const Candidate& b) {
        const bool la = a.light.type != LocalLight::Type::None, lb = b.light.type != LocalLight::Type::None;
        return la != lb ? la : luminance(a.arriving) > luminance(b.arriving);
    });
    std::size_t locals = 0;
    for (const Candidate& c : visible) {
        fold(out.folded, axes, c.to_light, c.arriving);
        if (c.light.type != LocalLight::Type::None && locals < out.lights.size()) {
            out.arriving[locals] = c.arriving;
            out.lights[locals++] = c.light;
        } else {
            fold(out.cube, axes, c.to_light, c.arriving);
        }
    }

    // Indirect light: the lightmaps of the surfaces seen from the point,
    // averaged per cube direction with cosine weights. Rays that reach the
    // sky see its ambient light; rays that leave the world or hit unlit
    // surfaces count as black.
    std::array<std::array<float, 3>, 6> sum{};
    std::array<float, 6> weight{};
    for (const Vec3& d : directions_) {
        const Vec3 end{point[0] + d[0] * kAmbientRayLength, point[1] + d[1] * kAmbientRayLength,
                       point[2] + d[2] * kAmbientRayLength};
        std::array<float, 3> color{};
        bool sky = false;
        if (const auto hit = bsp_.trace_lightmap(point, end, &sky)) {
            color = lightmap_at(*hit);
            for (float& v : color) {
                v *= kAmbientWeight;
            }
        } else if (sky) {
            color = sky_ambient;
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
            const float indirect = weight[f] > 0 ? sum[f][ch] / weight[f] : 0.0f;
            out.cube.color[f][ch] += indirect;
            out.folded.color[f][ch] += indirect;
        }
    }
    return out;
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
