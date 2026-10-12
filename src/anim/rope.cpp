#include "openbl/anim/rope.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::anim {

namespace {

float distance(const Vec3& a, const Vec3& b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

std::vector<Vec3> rope_rest_nodes(const Vec3& a, const Vec3& b, float slack, int subdiv) {
    const std::size_t count = static_cast<std::size_t>(std::clamp(subdiv, 0, 8)) + 2;
    const float span = distance(a, b);
    const float link = (span + std::max(slack, 0.0f)) / static_cast<float>(count - 1);
    std::vector<Vec3> nodes(count), previous(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        for (int k = 0; k < 3; ++k) {
            nodes[i][k] = a[k] + (b[k] - a[k]) * t;
        }
    }
    if (count == 2 || link * static_cast<float>(count - 1) <= span + 1e-3f) {
        return nodes; // Taut: straight.
    }
    // Settle with Verlet steps: gravity, damping, and links held at their
    // length (relaxed in passes), ends fixed. Units and seconds don't matter
    // for the rest shape; the step only sets how fast it settles.
    previous = nodes;
    constexpr int kSteps = 3000;
    constexpr int kPasses = 8;
    const float gravity = link * 0.01f;
    for (int step = 0; step < kSteps; ++step) {
        for (std::size_t i = 1; i + 1 < count; ++i) {
            Vec3 next{};
            for (int k = 0; k < 3; ++k) {
                next[k] = nodes[i][k] + (nodes[i][k] - previous[i][k]) * 0.9f;
            }
            next[2] -= gravity;
            previous[i] = nodes[i];
            nodes[i] = next;
        }
        for (int pass = 0; pass < kPasses; ++pass) {
            for (std::size_t i = 0; i + 1 < count; ++i) {
                Vec3& p = nodes[i];
                Vec3& q = nodes[i + 1];
                const float d = distance(p, q);
                if (d < 1e-6f) {
                    continue;
                }
                const float error = (d - link) / d;
                // Fixed ends take none of the correction.
                const float wp = i == 0 ? 0.0f : 1.0f;
                const float wq = i + 2 == count ? 0.0f : 1.0f;
                if (wp + wq == 0.0f) {
                    continue;
                }
                for (int k = 0; k < 3; ++k) {
                    const float delta = (q[k] - p[k]) * error / (wp + wq);
                    p[k] += delta * wp;
                    q[k] -= delta * wq;
                }
            }
        }
    }
    return nodes;
}

std::vector<Vec3> rope_curve(const std::vector<Vec3>& nodes, int steps) {
    if (nodes.size() < 2 || steps < 1) {
        return nodes;
    }
    const std::size_t n = nodes.size();
    auto node = [&](std::ptrdiff_t i) -> Vec3 {
        if (i < 0) { // Mirror the first link.
            return {2 * nodes[0][0] - nodes[1][0], 2 * nodes[0][1] - nodes[1][1], 2 * nodes[0][2] - nodes[1][2]};
        }
        if (static_cast<std::size_t>(i) >= n) {
            return {2 * nodes[n - 1][0] - nodes[n - 2][0], 2 * nodes[n - 1][1] - nodes[n - 2][1],
                    2 * nodes[n - 1][2] - nodes[n - 2][2]};
        }
        return nodes[static_cast<std::size_t>(i)];
    };
    std::vector<Vec3> out;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const auto j = static_cast<std::ptrdiff_t>(i);
        const Vec3 p0 = node(j - 1), p1 = node(j), p2 = node(j + 1), p3 = node(j + 2);
        for (int s = 0; s < steps; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const float t2 = t * t, t3 = t2 * t;
            Vec3 p{};
            for (int k = 0; k < 3; ++k) {
                p[k] = 0.5f * (2 * p1[k] + (p2[k] - p0[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                               (3 * p1[k] - p0[k] - 3 * p2[k] + p3[k]) * t3);
            }
            out.push_back(p);
        }
    }
    out.push_back(nodes.back());
    return out;
}

} // namespace openbl::anim
