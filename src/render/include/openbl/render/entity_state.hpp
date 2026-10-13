#pragma once

// What the client draws of one map entity, as the simulation reports it
// (docs/ARCHITECTURE.md, decision 1). Indexed like BspFile::entities.

#include <array>
#include <cstdint>
#include <optional>

namespace openbl::render {

struct EntityState {
    bool hidden = false;    // StartHidden, ScriptHide, Kill, sprites switched off...
    std::int32_t skin = -1; // Models: the skin to draw; -1 keeps the map's.
    // How far it has moved from its pose in the map, in world space: three
    // rows of a 3x4 matrix (rotation | translation). Empty: not moved.
    std::optional<std::array<float, 12>> transform;
};

// A moved entity's transform as a column-major 4x4 matrix.
inline std::array<float, 16> transform_matrix(const std::array<float, 12>& rows) {
    return {rows[0], rows[4], rows[8], 0.0f, rows[1], rows[5], rows[9], 0.0f,
            rows[2], rows[6], rows[10], 0.0f, rows[3], rows[7], rows[11], 1.0f};
}

// a x b for column-major 4x4 matrices.
inline std::array<float, 16> multiply(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> out{};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + r] * b[c * 4 + k];
            }
            out[c * 4 + r] = sum;
        }
    }
    return out;
}

} // namespace openbl::render
