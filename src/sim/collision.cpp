#include "openbl/sim/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openbl::sim {

namespace {

using formats::Vec3;

// Traces stop this far short of what they hit (Quake's SURFACE_CLIP_EPSILON),
// so the next trace does not start inside it.
constexpr float kClipEpsilon = 1.0f / 32.0f;

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

// How far the box reaches along `n` from its centre.
float reach(const Vec3& n, const Vec3& extents) {
    return std::abs(n[0]) * extents[0] + std::abs(n[1]) * extents[1] + std::abs(n[2]) * extents[2];
}

} // namespace

// One trace in progress: the box's centre moves from start to end.
struct Collision::Sweep {
    Vec3 start, end, delta, extents;
    Vec3 lo, hi; // Bounds of the whole sweep.
    std::int32_t mask = 0;
    Trace result;
};

Collision::Collision(const formats::BspFile& bsp) : bsp_(bsp), brush_stamp_(bsp.brushes.size(), 0) {
    // Displacements, as triangles of their grids (they belong to the world, model 0).
    for (std::size_t f = 0; f < bsp.faces.size(); ++f) {
        if (bsp.faces[f].dispinfo < 0) {
            continue;
        }
        const auto mesh = formats::build_face_mesh(bsp, f);
        Displacement d;
        d.mins = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        d.maxs = {-d.mins[0], -d.mins[1], -d.mins[2]};
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Triangle t{mesh.positions[mesh.indices[i]], mesh.positions[mesh.indices[i + 1]],
                             mesh.positions[mesh.indices[i + 2]]};
            for (const Vec3& p : {t.a, t.b, t.c}) {
                for (int k = 0; k < 3; ++k) {
                    d.mins[k] = std::min(d.mins[k], p[k]);
                    d.maxs[k] = std::max(d.maxs[k], p[k]);
                }
            }
            d.triangles.push_back(t);
        }
        if (!d.triangles.empty()) {
            displacements_.push_back(std::move(d));
        }
    }
}

Trace Collision::trace(std::size_t model, const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                       std::int32_t mask) const {
    Sweep s;
    const Vec3 centre{(mins[0] + maxs[0]) * 0.5f, (mins[1] + maxs[1]) * 0.5f, (mins[2] + maxs[2]) * 0.5f};
    s.extents = {(maxs[0] - mins[0]) * 0.5f, (maxs[1] - mins[1]) * 0.5f, (maxs[2] - mins[2]) * 0.5f};
    s.start = {start[0] + centre[0], start[1] + centre[1], start[2] + centre[2]};
    s.end = {end[0] + centre[0], end[1] + centre[1], end[2] + centre[2]};
    s.delta = sub(s.end, s.start);
    for (int k = 0; k < 3; ++k) {
        s.lo[k] = std::min(s.start[k], s.end[k]) - s.extents[k] - 1.0f;
        s.hi[k] = std::max(s.start[k], s.end[k]) + s.extents[k] + 1.0f;
    }
    s.mask = mask;
    if (++stamp_ == 0) { // Wrapped: forget every brush's stamp.
        std::fill(brush_stamp_.begin(), brush_stamp_.end(), 0);
        stamp_ = 1;
    }
    if (model < bsp_.models.size() && !bsp_.nodes.empty()) {
        trace_node(s, bsp_.models[model].head_node);
    }
    if (model == 0 && (mask & contents::Solid)) {
        trace_displacements(s);
    }
    Trace& r = s.result;
    for (int k = 0; k < 3; ++k) {
        r.end[k] = start[k] + (end[k] - start[k]) * r.fraction;
    }
    return r;
}

Trace Collision::trace_moved(std::size_t model, const Transform& transform, const Vec3& start, const Vec3& end,
                             const Vec3& mins, const Vec3& maxs, std::int32_t mask) const {
    // Into model space: the inverse of the rigid transform, R^T (p - t).
    const auto& r = transform.rotation;
    auto to_local = [&](const Vec3& p) {
        const Vec3 d = sub(p, transform.translation);
        return Vec3{r[0] * d[0] + r[3] * d[1] + r[6] * d[2], r[1] * d[0] + r[4] * d[1] + r[7] * d[2],
                    r[2] * d[0] + r[5] * d[1] + r[8] * d[2]};
    };
    const Vec3 centre{(mins[0] + maxs[0]) * 0.5f, (mins[1] + maxs[1]) * 0.5f, (mins[2] + maxs[2]) * 0.5f};
    const Vec3 e{(maxs[0] - mins[0]) * 0.5f, (maxs[1] - mins[1]) * 0.5f, (maxs[2] - mins[2]) * 0.5f};
    // The box around the box, turned into model space.
    const Vec3 local_e{std::abs(r[0]) * e[0] + std::abs(r[3]) * e[1] + std::abs(r[6]) * e[2],
                       std::abs(r[1]) * e[0] + std::abs(r[4]) * e[1] + std::abs(r[7]) * e[2],
                       std::abs(r[2]) * e[0] + std::abs(r[5]) * e[1] + std::abs(r[8]) * e[2]};
    const Vec3 a = to_local({start[0] + centre[0], start[1] + centre[1], start[2] + centre[2]});
    const Vec3 b = to_local({end[0] + centre[0], end[1] + centre[1], end[2] + centre[2]});
    Trace t = trace(model, a, b, {-local_e[0], -local_e[1], -local_e[2]}, local_e, mask);
    // Back to the world.
    const Vec3 n = t.normal;
    t.normal = {r[0] * n[0] + r[1] * n[1] + r[2] * n[2], r[3] * n[0] + r[4] * n[1] + r[5] * n[2],
                r[6] * n[0] + r[7] * n[1] + r[8] * n[2]};
    for (int k = 0; k < 3; ++k) {
        t.end[k] = start[k] + (end[k] - start[k]) * t.fraction;
    }
    return t;
}

void Collision::trace_node(Sweep& s, std::int32_t node) const {
    while (node >= 0) {
        const formats::BspNode& n = bsp_.nodes[static_cast<std::size_t>(node)];
        const formats::BspPlane& p = bsp_.planes[static_cast<std::size_t>(n.plane)];
        const float t1 = dot(p.normal, s.start) - p.dist;
        const float t2 = dot(p.normal, s.end) - p.dist;
        const float offset = reach(p.normal, s.extents) + 1.0f;
        if (t1 >= offset && t2 >= offset) {
            node = n.children[0];
        } else if (t1 < -offset && t2 < -offset) {
            node = n.children[1];
        } else {
            trace_node(s, n.children[0]);
            node = n.children[1];
        }
    }
    const formats::BspLeaf& leaf = bsp_.leafs[static_cast<std::size_t>(-(node + 1))];
    for (std::uint32_t i = 0; i < leaf.num_leaf_brushes; ++i) {
        const std::size_t lb = std::size_t{leaf.first_leaf_brush} + i;
        if (lb >= bsp_.leaf_brushes.size()) {
            break;
        }
        const std::size_t brush = bsp_.leaf_brushes[lb];
        if (brush >= bsp_.brushes.size() || brush_stamp_[brush] == stamp_) {
            continue;
        }
        brush_stamp_[brush] = stamp_;
        if (s.mask == contents::Any || (bsp_.brushes[brush].contents & s.mask)) {
            trace_brush(s, brush);
        }
    }
}

// Quake 3's CM_TraceThroughBrush: the box against the brush's side planes,
// each pushed out by how far the box reaches along it.
void Collision::trace_brush(Sweep& s, std::size_t brush) const {
    const formats::BspBrush& b = bsp_.brushes[brush];
    float enter = -1.0f, leave = 1.0f;
    bool start_out = false, end_out = false;
    Vec3 hit_normal{};
    for (std::int32_t i = 0; i < b.num_sides; ++i) {
        const std::size_t side = static_cast<std::size_t>(b.first_side + i);
        if (side >= bsp_.brush_sides.size()) {
            return;
        }
        const formats::BspPlane& p = bsp_.planes[bsp_.brush_sides[side].plane];
        const float dist = p.dist + reach(p.normal, s.extents);
        const float d1 = dot(s.start, p.normal) - dist;
        const float d2 = dot(s.end, p.normal) - dist;
        end_out = end_out || d2 > 0;
        start_out = start_out || d1 > 0;
        if (d1 > 0 && (d2 >= kClipEpsilon || d2 >= d1)) {
            return; // Entirely in front of this side: misses the brush.
        }
        if (d1 <= 0 && d2 <= 0) {
            continue;
        }
        if (d1 > d2) { // Entering through this side.
            const float f = std::max((d1 - kClipEpsilon) / (d1 - d2), 0.0f);
            if (f > enter) {
                enter = f;
                hit_normal = p.normal;
            }
        } else { // Leaving.
            leave = std::min(leave, (d1 + kClipEpsilon) / (d1 - d2));
        }
    }
    Trace& r = s.result;
    if (!start_out) {
        r.start_solid = true;
        if (!end_out) {
            r.all_solid = true;
            r.fraction = 0;
            r.contents = b.contents;
        }
        return;
    }
    if (enter < leave && enter > -1 && enter < r.fraction) {
        r.fraction = std::max(enter, 0.0f);
        r.normal = hit_normal;
        r.contents = b.contents;
    }
}

// Displacements: the box against each triangle by separating axes (the
// triangle's normal, the box's axes, and the triangle's edges crossed with
// them), each giving the times the box overlaps the triangle along it.
void Collision::trace_displacements(Sweep& s) const {
    const float length = std::sqrt(dot(s.delta, s.delta));
    for (const Displacement& d : displacements_) {
        if (d.maxs[0] < s.lo[0] || d.mins[0] > s.hi[0] || d.maxs[1] < s.lo[1] || d.mins[1] > s.hi[1] ||
            d.maxs[2] < s.lo[2] || d.mins[2] > s.hi[2]) {
            continue;
        }
        for (const Triangle& t : d.triangles) {
            bool outside = false;
            for (int k = 0; k < 3 && !outside; ++k) {
                const float tmin = std::min({t.a[k], t.b[k], t.c[k]}), tmax = std::max({t.a[k], t.b[k], t.c[k]});
                outside = tmax < s.lo[k] || tmin > s.hi[k];
            }
            if (outside) {
                continue;
            }
            const Vec3 e0 = sub(t.b, t.a), e1 = sub(t.c, t.b), e2 = sub(t.a, t.c);
            Vec3 axes[13];
            axes[0] = cross(e0, sub(t.c, t.a));
            axes[1] = {1, 0, 0};
            axes[2] = {0, 1, 0};
            axes[3] = {0, 0, 1};
            int count = 4;
            for (const Vec3& e : {e0, e1, e2}) {
                for (int k = 1; k <= 3; ++k) {
                    axes[count++] = cross(e, axes[k]);
                }
            }
            float enter = -std::numeric_limits<float>::max(), leave = std::numeric_limits<float>::max();
            Vec3 normal{};
            bool separated = false;
            for (int i = 0; i < count && !separated; ++i) {
                Vec3 a = axes[i];
                const float len = std::sqrt(dot(a, a));
                if (len < 1e-6f) {
                    continue;
                }
                a = {a[0] / len, a[1] / len, a[2] / len};
                const float p0 = dot(a, t.a), p1 = dot(a, t.b), p2 = dot(a, t.c);
                const float r = reach(a, s.extents);
                const float c0 = dot(a, s.start), v = dot(a, s.delta);
                const float lo = std::min({p0, p1, p2}) - r - c0;
                const float hi = std::max({p0, p1, p2}) + r - c0;
                if (std::abs(v) < 1e-9f) {
                    separated = lo > 0 || hi < 0;
                    continue;
                }
                float t0 = lo / v, t1 = hi / v;
                if (t0 > t1) {
                    std::swap(t0, t1);
                }
                if (t0 > enter) {
                    enter = t0;
                    normal = v > 0 ? Vec3{-a[0], -a[1], -a[2]} : a;
                }
                leave = std::min(leave, t1);
                separated = enter > leave;
            }
            // Overlapping already at the start: let it move out (as resting on a surface).
            if (separated || enter < 0 || enter > 1 || enter >= s.result.fraction) {
                continue;
            }
            const float approach = -dot(normal, s.delta);
            const float back_off = approach > 1e-6f ? kClipEpsilon / approach : kClipEpsilon / std::max(length, 1e-6f);
            s.result.fraction = std::max(enter - back_off, 0.0f);
            s.result.normal = normal;
            s.result.contents = contents::Solid;
        }
    }
}

} // namespace openbl::sim
