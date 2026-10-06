#pragma once

#include "openbl/formats/bsp.hpp"

#include <array>
#include <optional>
#include <vector>

namespace openbl::render {

// Approximates the light at a point from the baked lightmap of the floor
// directly below it. A stand-in until world lights and ambient cubes are
// decoded; see docs/ROADMAP.md.
class FloorLightProbe {
public:
    explicit FloorLightProbe(const formats::BspFile& bsp);

    // Linear RGB of lightmap style 0, or nullopt if no lit floor is below `point`
    // within `max_drop` units.
    std::optional<std::array<float, 3>> sample(const formats::Vec3& point, float max_drop = 1024.0f) const;

private:
    struct Floor {
        std::size_t face;
        std::vector<formats::Vec3> polygon;
        float min_x, min_y, max_x, max_y;
    };
    const formats::BspFile& bsp_;
    std::vector<Floor> floors_;
};

} // namespace openbl::render
