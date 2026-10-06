#pragma once

// Internal mesh helpers shared by the model renderers.

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace openbl::render::detail {

// Area-weighted smooth vertex normals for an indexed triangle list.
inline std::vector<std::array<float, 3>> smooth_normals(const std::vector<std::array<float, 3>>& positions,
                                                        const std::vector<std::uint32_t>& indices) {
    std::vector<std::array<float, 3>> acc(positions.size(), {0.0f, 0.0f, 0.0f});
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const auto& a = positions[indices[t]];
        const auto& b = positions[indices[t + 1]];
        const auto& c = positions[indices[t + 2]];
        const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const std::array<float, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<float, 3> n{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                     ab[0] * ac[1] - ab[1] * ac[0]};
        for (std::size_t k = 0; k < 3; ++k) {
            auto& v = acc[indices[t + k]];
            v[0] += n[0];
            v[1] += n[1];
            v[2] += n[2];
        }
    }
    for (auto& v : acc) {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        v = len > 0 ? std::array<float, 3>{v[0] / len, v[1] / len, v[2] / len} : std::array<float, 3>{0, 0, 1};
    }
    return acc;
}

} // namespace openbl::render::detail
