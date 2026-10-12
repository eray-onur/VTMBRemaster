#pragma once

// Lighting for models placed in a map, as the original's model shaders take
// it (vertexlit_and_unlit_generic_vs20; docs/RENDERING.md): an "ambient
// cube" of six colours (+X, -X, +Y, -Y, +Z, -Z) that a vertex blends by the
// squared components of its normal, plus up to two lights evaluated per
// vertex.
//
// This follows how the original builds a model's lighting state (and bakes
// static props' vertex colours from it when a map loads; docs/RENDERING.md),
// sampled at one point:
// Direct light: every point, spot and emissive-surface world light, scaled by
// its light style's default brightness, whose line of sight to the point is
// not blocked by solid BSP leaves, and the sun where the sky is seen against
// it. The two brightest there (by attenuated intensity, ignoring spot cones)
// are kept as local lights, evaluated per vertex; the rest are folded into
// the cube (the sun never is).
// Indirect light: the baked lightmaps of the surfaces around the point, each
// times its texture's reflectivity, and the sky's ambient light where the sky
// is seen, gathered with rays (bounce light is not stored in the world light
// list).
// Lights with a style take part at the style's current brightness: the
// original rebuilds a model's lighting when one of its styles changes, so the
// choice is split into gathering (once) and resolving (per style change).
// In the same units as the world: lit colour = base x light in linear terms.

#include "openbl/formats/bsp.hpp"
#include "openbl/render/light_styles.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace openbl::render {

struct AmbientCube {
    std::array<std::array<float, 3>, 6> color{}; // Linear, lightmap units.
};

// A light the shaders evaluate per vertex: colour x max(n.l, 0) /
// (constant + linear d + quadratic d^2) x the spot cone (directional lights:
// colour x max(n.l, 0)), and nothing beyond its range. Emissive surfaces are
// spots along their normal.
struct LocalLight {
    enum class Type : std::uint8_t { None, Directional, Point, Spot };
    Type type = Type::None;
    formats::Vec3 position{};           // World space (point and spot).
    formats::Vec3 direction{};          // Unit; the way the light shines (directional and spot).
    std::array<float, 3> color{};       // Intensity x the style's default brightness.
    std::array<float, 3> attenuation{}; // Constant, linear, quadratic.
    float stopdot2 = 0;                 // Spot: cosine of the outer cone,
    float spot_scale = 0;               // 1 / (inner cosine - outer cosine),
    float exponent = 1;                 // and the falloff's exponent.
    float range = 0;                    // Point and spot: no light beyond this distance (0: unlimited).
};

struct ModelLighting {
    AmbientCube cube;                 // Model space; without the local lights.
    std::array<LocalLight, 2> lights; // Brightest first; unused ones have Type::None.
    // Each local light as it arrives at the sample point (after attenuation
    // and the spot cone), linear: what picks the light a character's shadow
    // falls away from.
    std::array<std::array<float, 3>, 2> arriving{};
    AmbientCube folded;               // The cube with the local lights folded in too.
};

// Everything a model's lighting is chosen from, before light styles are
// applied: the indirect light, and every light that reaches the point with
// what it would bring at full brightness.
struct LightingInputs {
    struct Candidate {
        LocalLight light;                 // Colour: the light's intensity, without its style.
        formats::Vec3 to_light{};         // Unit, world space (the sun: towards it).
        std::array<float, 3> arriving{};  // At the point, after attenuation and spot cone.
        float brightness = 0;             // Luminance x attenuation, without the cone: what ranks it.
        float strongest = 0;              // Brightest channel x attenuation: under r_worldlightmin it is left out.
        std::int32_t style = 0;
        bool sun = false;
    };
    std::array<formats::Vec3, 3> axes{};  // Model axes in world space (the cube's directions).
    AmbientCube indirect;                 // Model space.
    std::vector<Candidate> candidates;

    // True if some candidate has a light style other than 0.
    bool styled() const;
};

// The lighting with each light at its style's brightness `styles[style]`.
ModelLighting resolve(const LightingInputs& inputs, const std::array<float, LightStyles::kCount>& styles);

// A model lit by styled lights: its inputs, kept to rebuild its lighting when
// one of those styles changes brightness.
struct StyledLighting {
    LightingInputs inputs;
    std::array<float, LightStyles::kCount> used{}; // The style values `lighting` was last resolved with.

    // Resolves again into `lighting` if a style it depends on changed; true if it did.
    bool update(const std::array<float, LightStyles::kCount>& styles, ModelLighting& lighting);
};

// Floats per instance for the shaders: pack_local_lights' 8 float4.
constexpr std::size_t kLocalLightFloats = 32;

// The local lights in the model space of `model_to_world` (column-major, a
// rotation and a translation), times `scale`, as the shaders read them: per
// light 4 float4: (position, or the unit direction to a directional light;
// range squared, 0 for none), (colour; type: 0 none, 1 directional, 2 point, 3 spot),
// (attenuation; spot exponent), (spot direction x spot scale; -outer cosine
// x spot scale), so the cone's ramp is one dot product. See local_light in
// shaders/color.hlsli.
std::array<float, kLocalLightFloats> pack_local_lights(const ModelLighting& lighting,
                                                       const std::array<float, 16>& model_to_world, float scale);

class PropLighting {
public:
    explicit PropLighting(const formats::BspFile& bsp);

    // `rotation` is the model-to-world rotation (row-major 3x3); the cube is
    // returned in model space so shaders can use object-space normals.
    LightingInputs gather(const formats::Vec3& point, const std::array<std::array<float, 3>, 3>& rotation) const;
    // Gathered and resolved with every style at its default brightness.
    ModelLighting sample(const formats::Vec3& point, const std::array<std::array<float, 3>, 3>& rotation) const;
    const std::array<float, LightStyles::kCount>& default_styles() const { return default_styles_; }

private:
    // Summed light styles (at their default brightness) at a traced point.
    std::array<float, 3> lightmap_at(const formats::BspSurfaceHit& hit) const;

    const formats::BspFile& bsp_;
    LightStyles styles_;
    std::array<float, LightStyles::kCount> default_styles_{};
    std::vector<formats::Vec3> directions_;
};

} // namespace openbl::render
