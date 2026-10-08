#include "openbl/anim/cloth.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::anim {

namespace {

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

} // namespace

ClothSimulation::ClothSimulation(const formats::MdlCloth& cloth, std::span<const Vec3> skinned, ClothSettings settings)
    : attached_(std::min<std::uint32_t>(cloth.attached_particles, static_cast<std::uint32_t>(cloth.particles.size()))),
      constraints_(cloth.constraints), triangles_(cloth.triangles), settings_(settings) {
    positions_.assign(cloth.particles.size(), Vec3{});
    for (std::size_t i = 0; i < positions_.size() && i < skinned.size(); ++i) {
        positions_[i] = skinned[i];
    }
    previous_ = positions_;
    primary_ = std::min<std::size_t>(cloth.primary_constraints, constraints_.size());
    // Drop references the file could not satisfy, so steps need no checks.
    const std::size_t n = positions_.size();
    const auto bad = [n](const formats::MdlClothConstraint& c) { return c.a >= n || c.b >= n; };
    primary_ -= static_cast<std::size_t>(std::count_if(constraints_.begin(), constraints_.begin() + primary_, bad));
    std::erase_if(constraints_, bad);
    std::erase_if(triangles_, [n](const auto& t) { return t[0] >= n || t[1] >= n || t[2] >= n; });
}

void ClothSimulation::update(float dt, std::span<const Vec3> skinned, std::span<const ClothCapsule> capsules) {
    pending_ = std::min(pending_ + std::max(dt, 0.0f), settings_.step * static_cast<float>(settings_.max_steps));
    while (pending_ >= settings_.step) {
        step(skinned, capsules);
        pending_ -= settings_.step;
    }
}

void ClothSimulation::step(std::span<const Vec3> skinned, std::span<const ClothCapsule> capsules) {
    const std::size_t n = std::min(positions_.size(), skinned.size());
    // Attached particles follow the skeleton; their motion carries into the
    // free ones through the constraints.
    for (std::size_t i = 0; i < attached_ && i < n; ++i) {
        previous_[i] = positions_[i];
        positions_[i] = skinned[i];
    }
    // Verlet integration with damping and gravity for the free particles.
    const float keep = 1.0f - settings_.damping;
    const float fall = settings_.gravity * settings_.step * settings_.step;
    for (std::size_t i = attached_; i < n; ++i) {
        Vec3& p = positions_[i];
        const Vec3 velocity = sub(p, previous_[i]);
        previous_[i] = p;
        for (int k = 0; k < 3; ++k) {
            p[k] += velocity[k] * keep;
        }
        p[2] -= fall;
        // Towards the authored shape.
        for (int k = 0; k < 3; ++k) {
            p[k] += (skinned[i][k] - p[k]) * settings_.shape_stiffness;
        }
    }
    for (std::uint32_t it = 0; it < settings_.iterations; ++it) {
        // Distance constraints, with the square root approximated around the
        // rest length: d *= rest^2 / (|d|^2 + rest^2) - 1/2. Each end moves by
        // its weight times that (the weights' magnitudes sum to 2, and an
        // attached end weighs 0). The second group's rest lengths are a
        // fraction of the bind distance (the same per piece, 0.001 to 1 of its
        // square) and act only as a minimum: they stop the cloth collapsing.
        for (std::size_t i = 0; i < constraints_.size(); ++i) {
            const auto& c = constraints_[i];
            Vec3& a = positions_[c.a];
            Vec3& b = positions_[c.b];
            Vec3 d = sub(b, a);
            const float rest2 = c.rest_length_squared;
            const float d2 = dot(d, d);
            if (i >= primary_ && d2 >= rest2) {
                continue;
            }
            const float f = rest2 / (d2 + rest2) - 0.5f;
            for (int k = 0; k < 3; ++k) {
                d[k] *= f;
                a[k] -= c.weight_a * d[k];
                b[k] -= c.weight_b * d[k];
            }
        }
        // Push free particles out of the capsules.
        for (const ClothCapsule& cap : capsules) {
            const Vec3 axis = sub(cap.end, cap.start);
            const float len2 = dot(axis, axis);
            for (std::size_t i = attached_; i < n; ++i) {
                Vec3& p = positions_[i];
                const Vec3 rel = sub(p, cap.start);
                const float t = len2 > 0.0f ? std::clamp(dot(rel, axis) / len2, 0.0f, 1.0f) : 0.0f;
                const Vec3 closest{cap.start[0] + axis[0] * t, cap.start[1] + axis[1] * t, cap.start[2] + axis[2] * t};
                const Vec3 out = sub(p, closest);
                const float dist2 = dot(out, out);
                if (dist2 >= cap.radius * cap.radius || dist2 < 1e-12f) {
                    continue;
                }
                const float scale = cap.radius / std::sqrt(dist2);
                for (int k = 0; k < 3; ++k) {
                    p[k] = closest[k] + out[k] * scale;
                }
            }
        }
    }
    for (std::size_t i = attached_; i < n; ++i) {
        const Vec3 off = sub(positions_[i], skinned[i]);
        if (!(dot(off, off) <= settings_.max_stray * settings_.max_stray)) { // Also catches NaN.
            std::copy(skinned.begin(), skinned.begin() + static_cast<std::ptrdiff_t>(n), positions_.begin());
            std::copy(skinned.begin(), skinned.begin() + static_cast<std::ptrdiff_t>(n), previous_.begin());
            break;
        }
    }
}

std::vector<Vec3> ClothSimulation::normals() const {
    std::vector<Vec3> out(positions_.size(), Vec3{});
    for (const auto& t : triangles_) {
        const Vec3 n = cross(sub(positions_[t[1]], positions_[t[0]]), sub(positions_[t[2]], positions_[t[0]]));
        for (const auto i : t) {
            for (int k = 0; k < 3; ++k) {
                out[i][k] += n[k];
            }
        }
    }
    for (Vec3& n : out) {
        const float len = std::sqrt(dot(n, n));
        if (len > 0.0f) {
            for (float& v : n) {
                v /= len;
            }
        }
    }
    return out;
}

} // namespace openbl::anim
