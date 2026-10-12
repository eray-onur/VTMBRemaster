#pragma once

// Distance fog from worldspawn (or a sky_camera for the 3D skybox): linear in
// view depth between `start` and `end`, blended on encoded colour
// (shaders/color.hlsli; docs/RENDERING.md). Also the water fog seen through
// a water surface.

#include "openbl/formats/bsp.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace openbl::render {

struct Fog {
    bool enabled = false;
    std::array<float, 3> color{}; // Encoded (gamma) 0-1, as authored.
    float start = 0, end = 0;     // World units of view depth.
    // Water fog below a water surface at `water_z`, seen from `eye_z` (above
    // or below it): only the part of the view ray below the surface counts.
    bool water = false;
    float water_z = 0, eye_z = 0;

    // A water material's fog ($fogcolor, $fogstart, $fogend), below `water_z`.
    static Fog underwater(const std::array<float, 3>& color, float start, float end, float water_z, float eye_z) {
        Fog fog;
        fog.enabled = end > start;
        fog.color = color;
        fog.start = start;
        fog.end = end;
        fog.water = true;
        fog.water_z = water_z;
        fog.eye_z = eye_z;
        return fog;
    }

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

    // Shader fog mode: 0 none, 1 distance, 2 water (shaders/color.hlsli).
    float mode() const { return !enabled ? 0.0f : water ? 2.0f : 1.0f; }

    // The two float4 the shaders read: (colour, mode) and (start, end, water z, eye z).
    std::array<float, 8> rows() const {
        return {color[0], color[1], color[2], mode(), start, end, water_z, eye_z};
    }
};

} // namespace openbl::render
