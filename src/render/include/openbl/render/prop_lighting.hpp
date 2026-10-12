#pragma once

// Lighting for models placed in a map, as the original's model shaders take
// it (vertexlit_and_unlit_generic_vs20; docs/RENDERING.md): an "ambient
// cube" of six colours (+X, -X, +Y, -Y, +Z, -Z) that a vertex blends by the
// squared components of its normal, plus up to two lights evaluated per
// vertex.
//
// Direct light: every point, spot and emissive-surface world light, scaled by
// its light style's default brightness, and the sun, whose line of sight to
// the sample point is not blocked by solid BSP leaves (the sun: that reaches
// the sky). The two brightest point, spot or sun lights there are kept as
// local lights; the rest are folded into the cube, and so are lights inside
// the model's bounds (lamp fixtures, the 3D skybox's own lights): lit per
// vertex from inside, the model's outer surfaces would face away from them.
// Indirect light: the baked lightmaps of the surfaces around the point, and
// the sky's ambient light where the sky is seen, gathered with rays (bounce
// light is not stored in the world light list).
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
// colour x max(n.l, 0)). Chosen only within the light's radius.
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
};

struct ModelLighting {
    AmbientCube cube;                 // Model space; without the local lights.
    std::array<LocalLight, 2> lights; // Brightest first; unused ones have Type::None.
    AmbientCube folded;               // The cube with the local lights folded in too.
};

// Floats per instance for the shaders: pack_local_lights' 8 float4.
constexpr std::size_t kLocalLightFloats = 32;

// The local lights in the model space of `model_to_world` (column-major, a
// rotation and a translation), times `scale`, as the shaders read them: per
// light 4 float4: (position, or the unit direction to a directional light;
// unused), (colour; type: 0 none, 1 directional, 2 point, 3 spot),
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
    // `bounds`: the model's world-space box (min, max).
    ModelLighting sample(const formats::Vec3& point, const std::array<std::array<float, 3>, 3>& rotation,
                         const std::array<formats::Vec3, 2>& bounds) const;

private:
    // Summed light styles (at their default brightness) at a traced point.
    std::array<float, 3> lightmap_at(const formats::BspSurfaceHit& hit) const;

    const formats::BspFile& bsp_;
    LightStyles styles_;
    std::vector<formats::Vec3> directions_;
};

} // namespace openbl::render
