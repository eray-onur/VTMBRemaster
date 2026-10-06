#pragma once

// Light styles: per-style brightness multipliers for lightmaps and world lights.
//
//   0       always on (1.0)
//   1..12   animated patterns (the classic Quake style strings, 10 letters/s;
//           'a' = 0, 'm' = 1.0, 'z' = 25/12)
//   32..63  switchable lights; the compiler stores the style on the light
//           entities. A style starts on unless every light using it has
//           spawnflag 1 ("initially dark").

#include "openbl/formats/bsp.hpp"

#include <array>

namespace openbl::render {

class LightStyles {
public:
    static constexpr std::size_t kCount = 64;

    explicit LightStyles(const formats::BspFile& bsp);

    // Brightness of every style at `time` seconds.
    std::array<float, kCount> values(float time) const;
    // Brightness of one style in its default state, ignoring animation (for baked prop lighting).
    float default_value(std::size_t style) const;

private:
    std::array<bool, kCount> on_{};
};

} // namespace openbl::render
