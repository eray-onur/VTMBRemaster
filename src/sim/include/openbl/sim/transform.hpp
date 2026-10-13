#pragma once

// A rigid motion in world space: where an entity is drawn relative to its pose
// in the map (a door swung open, a fan turned, a knob carried by its door).

#include "openbl/formats/bsp.hpp"

#include <array>
#include <cmath>

namespace openbl::sim {

struct Transform {
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1}; // Row-major 3x3.
    formats::Vec3 translation{};

    formats::Vec3 apply(const formats::Vec3& p) const {
        const auto& r = rotation;
        return {r[0] * p[0] + r[1] * p[1] + r[2] * p[2] + translation[0],
                r[3] * p[0] + r[4] * p[1] + r[5] * p[2] + translation[1],
                r[6] * p[0] + r[7] * p[1] + r[8] * p[2] + translation[2]};
    }
    // This after `first`.
    Transform after(const Transform& first) const {
        Transform out;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                out.rotation[i * 3 + j] = rotation[i * 3] * first.rotation[j] + rotation[i * 3 + 1] * first.rotation[3 + j] +
                                          rotation[i * 3 + 2] * first.rotation[6 + j];
            }
        }
        out.translation = apply(first.translation);
        return out;
    }

    static Transform translate(const formats::Vec3& d) {
        Transform t;
        t.translation = d;
        return t;
    }
    // `degrees` about the unit `axis` through `pivot` (right-handed).
    static Transform rotate(const formats::Vec3& axis, float degrees, const formats::Vec3& pivot) {
        const float a = degrees * 3.14159265358979f / 180.0f;
        const float c = std::cos(a), s = std::sin(a), k = 1.0f - c;
        const float x = axis[0], y = axis[1], z = axis[2];
        Transform t;
        t.rotation = {c + x * x * k,     x * y * k - z * s, x * z * k + y * s,
                      y * x * k + z * s, c + y * y * k,     y * z * k - x * s,
                      z * x * k - y * s, z * y * k + x * s, c + z * z * k};
        // p' = R (p - pivot) + pivot
        const formats::Vec3 rp = t.apply(pivot);
        t.translation = {pivot[0] - rp[0], pivot[1] - rp[1], pivot[2] - rp[2]};
        return t;
    }
};

} // namespace openbl::sim
