#include "openbl/render/prop_lighting.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::render {

namespace {

using formats::BspLightType;
using formats::Vec3;

// Rays traced to gather the surrounding lightmaps, and how far they look (as
// far as the original's, which also looks for the sky against the sun).
constexpr std::size_t kAmbientRays = 162;
constexpr float kRayLength = 57016.0f;

// Lights that bring at most this much at the sample point (brightest channel)
// are left out, and only lights at least this bright there (luminance) become
// local lights: the original's r_worldlightmin.
constexpr float kWorldLightMin = 1.0f / 128.0f;
// How far short of a light its line of sight stops.
constexpr float kLightClearance = 8.0f;
// Emissive surfaces lit per vertex: a spot along the surface normal whose cone
// reaches 80 degrees from it; spot lights' outer cones are kept within the
// same 80 degrees and no narrower than 0.99 in cosine.
constexpr float kWideConeCos = 0.173648f;
constexpr float kNarrowConeCos = 0.99f;
// Without a radius, a local light reaches as far as its intensity over its
// attenuation stays above this.
constexpr float kRangeCutoff = 0.03f;

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
        const float values[16] = {p[0], p[1], p[2], l.range * l.range,
                                  l.color[0] * scale, l.color[1] * scale, l.color[2] * scale, static_cast<float>(l.type),
                                  l.attenuation[0], l.attenuation[1], l.attenuation[2], l.exponent,
                                  spot[0] * l.spot_scale, spot[1] * l.spot_scale, spot[2] * l.spot_scale,
                                  -l.stopdot2 * l.spot_scale};
        std::copy(std::begin(values), std::end(values), r);
    }
    return out;
}

PropLighting::PropLighting(const formats::BspFile& bsp) : bsp_(bsp), styles_(bsp) {
    for (std::size_t i = 0; i < LightStyles::kCount; ++i) {
        default_styles_[i] = styles_.default_value(i);
    }
    // Evenly spread directions (Fibonacci sphere).
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (std::size_t i = 0; i < kAmbientRays; ++i) {
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / kAmbientRays;
        const float r = std::sqrt(1.0f - z * z);
        const float a = golden * static_cast<float>(i);
        directions_.push_back({r * std::cos(a), r * std::sin(a), z});
    }
}

bool LightingInputs::styled() const {
    return std::any_of(candidates.begin(), candidates.end(), [](const Candidate& c) { return c.style != 0; });
}

ModelLighting resolve(const LightingInputs& inputs, const std::array<float, LightStyles::kCount>& styles) {
    ModelLighting out;
    out.cube = inputs.indirect;
    out.folded = inputs.indirect;
    struct Lit {
        const LightingInputs::Candidate* c;
        float value;
        float brightness;
    };
    std::vector<Lit> lit;
    lit.reserve(inputs.candidates.size());
    for (const auto& c : inputs.candidates) {
        const float value =
            c.style >= 0 && static_cast<std::size_t>(c.style) < styles.size() ? styles[static_cast<std::size_t>(c.style)] : 0.0f;
        if (value <= 0 || (!c.sun && c.strongest * value <= kWorldLightMin)) {
            continue;
        }
        lit.push_back({&c, value, c.brightness * value});
    }
    // The two brightest become local lights. The rest are folded into the
    // cube; the sun never is (it lights a model only as a local light).
    std::stable_sort(lit.begin(), lit.end(), [](const Lit& a, const Lit& b) { return a.brightness > b.brightness; });
    std::size_t locals = 0;
    for (const Lit& l : lit) {
        std::array<float, 3> arriving = l.c->arriving;
        for (float& v : arriving) {
            v *= l.value;
        }
        if (locals < out.lights.size() && l.brightness >= kWorldLightMin) {
            LocalLight light = l.c->light;
            for (float& v : light.color) {
                v *= l.value;
            }
            out.arriving[locals] = arriving;
            out.lights[locals++] = light;
            fold(out.folded, inputs.axes, l.c->to_light, arriving);
        } else if (!l.c->sun) {
            fold(out.cube, inputs.axes, l.c->to_light, arriving);
            fold(out.folded, inputs.axes, l.c->to_light, arriving);
        }
    }
    return out;
}

bool StyledLighting::update(const std::array<float, LightStyles::kCount>& styles, ModelLighting& lighting) {
    const bool changed = std::any_of(inputs.candidates.begin(), inputs.candidates.end(), [&](const auto& c) {
        return c.style >= 0 && static_cast<std::size_t>(c.style) < styles.size() &&
               styles[static_cast<std::size_t>(c.style)] != used[static_cast<std::size_t>(c.style)];
    });
    if (!changed) {
        return false;
    }
    lighting = resolve(inputs, styles);
    used = styles;
    return true;
}

ModelLighting PropLighting::sample(const Vec3& point, const std::array<std::array<float, 3>, 3>& rotation) const {
    return resolve(gather(point, rotation), default_styles_);
}

LightingInputs PropLighting::gather(const Vec3& point, const std::array<std::array<float, 3>, 3>& rotation) const {
    LightingInputs out;
    // Model-space axis k in world space is column k of the rotation.
    for (std::size_t k = 0; k < 3; ++k) {
        out.axes[k] = {rotation[0][k], rotation[1][k], rotation[2][k]};
    }
    const auto& axes = out.axes;

    // Every light that can reach the point: what it brings there at full
    // brightness (after the spot cone, before n.l), and how bright it counts
    // when the local lights are chosen. Lights whose style starts dark are
    // kept too, for when it changes.
    std::array<float, 3> sky_ambient{};
    bool sun_seen = false;
    for (const auto& light : bsp_.world_lights) {
        if (light.type == BspLightType::SkyAmbient) {
            const float style = light.style >= 0 ? styles_.default_value(static_cast<std::size_t>(light.style)) : 0.0f;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                sky_ambient[ch] += light.intensity[ch] * style;
            }
            continue;
        }
        LightingInputs::Candidate c;
        c.style = light.style;
        c.light.color = light.intensity;
        if (light.type == BspLightType::Sky) {
            // The sun: a directional light where the sky is seen against it.
            // Only the first counts.
            if (sun_seen) {
                continue;
            }
            c.to_light = {-light.normal[0], -light.normal[1], -light.normal[2]};
            const Vec3 end{point[0] + c.to_light[0] * kRayLength, point[1] + c.to_light[1] * kRayLength,
                           point[2] + c.to_light[2] * kRayLength};
            bool sky = false;
            bsp_.trace_lightmap(point, end, &sky);
            if (!sky) {
                continue;
            }
            sun_seen = true;
            c.sun = true;
            c.light.type = LocalLight::Type::Directional;
            c.light.direction = light.normal;
            c.arriving = c.light.color;
            c.brightness = luminance(c.light.color);
            out.candidates.push_back(c);
            continue;
        }
        if (light.type != BspLightType::Point && light.type != BspLightType::Spot &&
            light.type != BspLightType::Surface) {
            continue;
        }
        Vec3 to_light{light.origin[0] - point[0], light.origin[1] - point[1], light.origin[2] - point[2]};
        const float dist = std::sqrt(dot(to_light, to_light));
        // The radius cuts the light off, as in the map compiler's lightmaps.
        if (dist < 1e-3f || (light.radius > 0 && dist > light.radius)) {
            continue;
        }
        for (float& v : to_light) {
            v /= dist;
        }
        // Emissive-surface lights store no attenuation terms (all zero); they
        // fall off with the inverse square of the distance.
        const bool surface = light.type == BspLightType::Surface;
        const std::array<float, 3> terms =
            surface ? std::array<float, 3>{0.0f, 0.0f, 1.0f}
                    : std::array<float, 3>{light.constant_attn, light.linear_attn, light.quadratic_attn};
        const float r = std::max(dist, 1.0f);
        const float scale = 1.0f / std::max(terms[0] + terms[1] * r + terms[2] * r * r, 1.0f);
        c.strongest = std::max({c.light.color[0], c.light.color[1], c.light.color[2]}) * scale;
        // The style can make a light brighter than its intensity ('z' is
        // 25/12), so only lights too faint even then are dropped here.
        if (c.strongest * (25.0f / 12.0f) <= kWorldLightMin) {
            continue;
        }
        // The line of sight stops just short of the light, which often sits
        // inside its fixture.
        const float reach = std::max(dist - kLightClearance, 0.0f);
        if (bsp_.segment_blocked(point, {point[0] + to_light[0] * reach, point[1] + to_light[1] * reach,
                                          point[2] + to_light[2] * reach})) {
            continue;
        }
        float cone = 1.0f;
        const float d = -dot(to_light, light.normal);
        if (light.type == BspLightType::Spot) {
            if (d <= light.stopdot2) {
                cone = 0.0f;
            } else if (d < light.stopdot) {
                cone = (d - light.stopdot2) / (light.stopdot - light.stopdot2);
                if (light.exponent != 0 && light.exponent != 1) {
                    cone = std::pow(cone, light.exponent);
                }
            }
        } else if (surface) {
            cone = d > 0.01f ? d : 0.0f;
        }
        c.to_light = to_light;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            c.arriving[ch] = c.light.color[ch] * scale * cone;
        }
        // Ranked without the cone: a spot light aimed past the point can
        // still light parts of the model, which are lit per vertex.
        c.brightness = luminance(c.light.color) * scale;

        c.light.type = light.type == BspLightType::Point ? LocalLight::Type::Point : LocalLight::Type::Spot;
        c.light.position = light.origin;
        c.light.direction = light.normal;
        c.light.attenuation = terms;
        float outer = kWideConeCos, inner = 1.0f, exponent = 1.0f;
        if (light.type == BspLightType::Spot) {
            outer = std::clamp(light.stopdot2, kWideConeCos, kNarrowConeCos);
            inner = light.stopdot > 1.0f ? 1.0f : std::max(light.stopdot, outer + 0.001f);
            // Exponents 0 and 1 both give a linear ramp, as above.
            exponent = light.exponent != 0 ? light.exponent : 1.0f;
        }
        if (c.light.type == LocalLight::Type::Spot) {
            c.light.stopdot2 = outer;
            c.light.spot_scale = 1.0f / (inner - outer);
            c.light.exponent = exponent;
        }
        // How far it reaches per vertex: its radius, else where its intensity
        // over its attenuation falls to kRangeCutoff.
        if (light.radius > 0) {
            c.light.range = light.radius;
        } else {
            const float reach_at = std::sqrt(dot(c.light.color, c.light.color)) / kRangeCutoff;
            if (terms[2] == 0) {
                c.light.range = terms[1] != 0 ? std::max((reach_at - terms[0]) / terms[1], 0.0f) : 0.0f;
            } else {
                const float disc = terms[1] * terms[1] - 4.0f * terms[2] * (terms[0] - reach_at);
                c.light.range = disc > 0 ? std::max((std::sqrt(disc) - terms[1]) / (2.0f * terms[2]), 0.0f) : 0.0f;
            }
        }
        out.candidates.push_back(c);
    }

    // Indirect light: the lightmaps of the surfaces seen from the point, each
    // times its texture's reflectivity, averaged per cube direction with
    // cosine weights. Rays that reach the sky see its ambient light; rays that
    // leave the world or hit unlit surfaces count as black.
    std::array<std::array<float, 3>, 6> sum{};
    std::array<float, 6> weight{};
    for (const Vec3& d : directions_) {
        const Vec3 end{point[0] + d[0] * kRayLength, point[1] + d[1] * kRayLength, point[2] + d[2] * kRayLength};
        std::array<float, 3> color{};
        bool sky = false;
        if (const auto hit = bsp_.trace_lightmap(point, end, &sky)) {
            color = lightmap_at(*hit);
            const std::int32_t texdata = bsp_.texinfos[bsp_.faces[hit->face].texinfo].texdata;
            const Vec3 reflectivity = texdata >= 0 && static_cast<std::size_t>(texdata) < bsp_.texdatas.size()
                                          ? bsp_.texdatas[static_cast<std::size_t>(texdata)].reflectivity
                                          : Vec3{};
            for (std::size_t ch = 0; ch < 3; ++ch) {
                color[ch] *= reflectivity[ch];
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
            out.indirect.color[f][ch] = weight[f] > 0 ? sum[f][ch] / weight[f] : 0.0f;
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
