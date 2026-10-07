#include "openbl/formats/mesh_decal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openbl::formats {

namespace {

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalize(Vec3 v) {
    const float l = std::sqrt(dot(v, v));
    if (l > 0) {
        for (float& c : v) {
            c /= l;
        }
    }
    return v;
}

// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5).
Vec3 closest_on_triangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) {
        return a;
    }
    const Vec3 bp = sub(p, b);
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const float v = d1 / (d1 - d3);
        return {a[0] + ab[0] * v, a[1] + ab[1] * v, a[2] + ab[2] * v};
    }
    const Vec3 cp = sub(p, c);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const float w = d2 / (d2 - d6);
        return {a[0] + ac[0] * w, a[1] + ac[1] * w, a[2] + ac[2] * w};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {b[0] + (c[0] - b[0]) * w, b[1] + (c[1] - b[1]) * w, b[2] + (c[2] - b[2]) * w};
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    return {a[0] + ab[0] * v + ac[0] * w, a[1] + ab[1] * v + ac[1] * w, a[2] + ab[2] * v + ac[2] * w};
}

} // namespace

std::optional<MeshDecal> project_mesh_decal(std::span<const Vec3> positions, std::span<const std::uint32_t> indices,
                                            const Vec3& origin, float width, float height, const Vec3& world_z,
                                            const Vec3& world_y, float reach, float lift) {
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    auto vertex = [&](std::size_t i) -> const Vec3& { return positions[indices[i]]; };
    // The nearest triangle the origin is in front of.
    float best = std::numeric_limits<float>::max();
    Vec3 n{}, nearest{};
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        if (indices[t] >= positions.size() || indices[t + 1] >= positions.size() || indices[t + 2] >= positions.size()) {
            continue;
        }
        const Vec3& a = vertex(t);
        const Vec3& b = vertex(t + 1);
        const Vec3& c = vertex(t + 2);
        const Vec3 tn = normalize(cross(sub(b, a), sub(c, a)));
        if (dot(tn, tn) == 0 || dot(sub(origin, a), tn) < -0.5f) {
            continue;
        }
        const Vec3 q = closest_on_triangle(origin, a, b, c);
        const Vec3 d = sub(origin, q);
        const float dist = std::sqrt(dot(d, d));
        if (dist < best) {
            best = dist;
            n = tn;
            nearest = q;
        }
    }
    if (best > reach) {
        return std::nullopt;
    }
    // Same axes as world decals.
    const Vec3 up_hint = std::abs(dot(n, world_z)) < 0.7f ? world_z : world_y;
    const Vec3 right = normalize(cross(up_hint, n));
    const Vec3 up = cross(n, right);
    const float plane_d = dot(sub(origin, nearest), n);
    const Vec3 center{origin[0] - n[0] * plane_d, origin[1] - n[1] * plane_d, origin[2] - n[2] * plane_d};
    const float depth = std::max(width, height) * 0.5f;

    MeshDecal out;
    out.distance = best;
    const std::array<std::pair<Vec3, float>, 4> edges{{{right, width * 0.5f},
                                                        {Vec3{-right[0], -right[1], -right[2]}, width * 0.5f},
                                                        {up, height * 0.5f},
                                                        {Vec3{-up[0], -up[1], -up[2]}, height * 0.5f}}};
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        if (indices[t] >= positions.size() || indices[t + 1] >= positions.size() || indices[t + 2] >= positions.size()) {
            continue;
        }
        std::vector<Vec3> poly{vertex(t), vertex(t + 1), vertex(t + 2)};
        const Vec3 tn = normalize(cross(sub(poly[1], poly[0]), sub(poly[2], poly[0])));
        if (dot(tn, n) < 0.1f) {
            continue; // Facing away from the projection.
        }
        bool near = false;
        for (const Vec3& p : poly) {
            near = near || std::abs(dot(sub(p, center), n)) <= depth;
        }
        if (!near) {
            continue;
        }
        for (const auto& [axis, half] : edges) {
            std::vector<Vec3> clipped;
            for (std::size_t k = 0; k < poly.size(); ++k) {
                const Vec3& a = poly[k];
                const Vec3& b = poly[(k + 1) % poly.size()];
                const float da = dot(sub(a, center), axis) - half;
                const float db = dot(sub(b, center), axis) - half;
                if (da <= 0.0f) {
                    clipped.push_back(a);
                }
                if ((da < 0.0f) != (db < 0.0f) && da != db) {
                    const float s = da / (da - db);
                    clipped.push_back({a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, a[2] + (b[2] - a[2]) * s});
                }
            }
            poly = std::move(clipped);
            if (poly.size() < 3) {
                break;
            }
        }
        if (poly.size() < 3) {
            continue;
        }
        const auto base = static_cast<std::uint32_t>(out.positions.size());
        // Barycentrics on the source triangle (before lifting).
        const Vec3& a = vertex(t);
        const Vec3 e1 = sub(vertex(t + 1), a), e2 = sub(vertex(t + 2), a);
        const float d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2);
        const float denom = d11 * d22 - d12 * d12;
        for (const Vec3& p : poly) {
            const Vec3 ap = sub(p, a);
            const float d1 = dot(ap, e1), d2 = dot(ap, e2);
            const float v = denom != 0 ? (d22 * d1 - d12 * d2) / denom : 0.0f;
            const float w = denom != 0 ? (d11 * d2 - d12 * d1) / denom : 0.0f;
            out.source_triangles.push_back(t);
            out.barycentrics.push_back({1.0f - v - w, v, w});
            const Vec3 rel = sub(p, center);
            out.positions.push_back({p[0] + tn[0] * lift, p[1] + tn[1] * lift, p[2] + tn[2] * lift});
            out.normals.push_back(tn);
            out.uvs.push_back({dot(rel, right) / width + 0.5f, 0.5f - dot(rel, up) / height});
        }
        for (std::uint32_t k = 2; k < poly.size(); ++k) {
            out.indices.insert(out.indices.end(), {base, base + k - 1, base + k});
        }
    }
    if (out.indices.empty()) {
        return std::nullopt;
    }
    return out;
}

} // namespace openbl::formats
