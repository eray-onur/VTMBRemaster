#pragma once

// Lighting for models placed in a map, Source style: an "ambient cube" of six
// colours (+X, -X, +Y, -Y, +Z, -Z) that a vertex blends by its normal.
//
// Direct light: every point, spot and emissive-surface world light, scaled by
// its light style's default brightness, whose line of sight to the sample
// point is not blocked by solid BSP leaves.
// Indirect light: approximated as a fraction of the baked floor lightmap below
// the point (bounce light is not stored in the world light list).

#include "openbl/formats/bsp.hpp"
#include "openbl/render/light_probe.hpp"
#include "openbl/render/light_styles.hpp"

#include <array>

namespace openbl::render {

struct AmbientCube {
    std::array<std::array<float, 3>, 6> color{}; // Linear, lightmap units.
};

class PropLighting {
public:
    explicit PropLighting(const formats::BspFile& bsp);

    // `rotation` is the model-to-world rotation (row-major 3x3); the cube is
    // returned in model space so shaders can use object-space normals.
    AmbientCube sample(const formats::Vec3& point, const std::array<std::array<float, 3>, 3>& rotation) const;

private:
    const formats::BspFile& bsp_;
    FloorLightProbe floor_;
    LightStyles styles_;
};

} // namespace openbl::render
