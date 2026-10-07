#pragma once

// Distance fog from worldspawn (or a sky_camera for the 3D skybox): linear in
// view depth between `start` and `end`, blended on encoded colour
// (shaders/color.hlsli; docs/RENDERING.md).

#include "openbl/formats/bsp.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace openbl::render {

struct Fog {
    bool enabled = false;
    std::array<float, 3> color{}; // Encoded (gamma) 0-1, as authored.
    float start = 0, end = 0;     // World units of view depth.

    // From "fogenable", "fogcolor" (0-255), "fogstart" and "fogend".
    static Fog from_entity(const formats::BspEntity& e) {
        Fog fog;
        auto value = [&](const char* key) -> const char* {
            const auto it = e.find(key);
            return it == e.end() ? nullptr : it->second.c_str();
        };
        if (const char* on = value("fogenable")) {
            fog.enabled = std::atoi(on) != 0;
        }
        if (const char* c = value("fogcolor")) {
            float r = 0, g = 0, b = 0;
            if (std::sscanf(c, "%f %f %f", &r, &g, &b) == 3) {
                fog.color = {r / 255.0f, g / 255.0f, b / 255.0f};
            }
        }
        if (const char* s = value("fogstart")) {
            fog.start = static_cast<float>(std::atof(s));
        }
        if (const char* s = value("fogend")) {
            fog.end = static_cast<float>(std::atof(s));
        }
        fog.enabled = fog.enabled && fog.end > fog.start;
        return fog;
    }

    // The two float4 the shaders read: (colour, enabled) and (start, end, 0, 0).
    std::array<float, 8> rows() const {
        return {color[0], color[1], color[2], enabled ? 1.0f : 0.0f, start, end, 0.0f, 0.0f};
    }
};

} // namespace openbl::render
