#pragma once

// What a viewpoint can see of a map (docs/RENDERING.md#visibility): the
// clusters in the potentially visible set (PVS) of the eye's cluster, and the
// view frustum's four sides. Area portals are not applied: every portal
// counts as open.

#include "openbl/formats/bsp.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

class ViewVisibility {
public:
    explicit ViewVisibility(const BspFile& bsp);

    // `eye`: the point whose PVS is used. `view_projection`: the view's
    // column-major clip matrix, whose left, right, bottom and top planes
    // bound what is visible. An eye outside the visibility data (in solid,
    // or a map without any) sees every cluster.
    void update(const Vec3& eye, const std::array<float, 16>& view_projection);

    bool sees_all_clusters() const { return all_; }
    // Clusters below 0 (leaves outside the visibility data) count as visible.
    bool cluster(std::int32_t cluster) const;
    bool leaf(std::size_t leaf) const;
    // True if any of the leaves is in the PVS (and true for an empty list).
    bool any_leaf(std::span<const std::uint32_t> leaves) const;
    // True if the box is at least partly inside the frustum's sides.
    bool box(const Vec3& mins, const Vec3& maxs) const;
    // An object: its box in the frustum and one of its leaves in the PVS.
    bool object(std::span<const std::uint32_t> leaves, const Vec3& mins, const Vec3& maxs) const {
        return box(mins, maxs) && any_leaf(leaves);
    }

private:
    const BspFile& bsp_;
    std::int32_t eye_cluster_ = -2; // -2: not computed yet.
    bool all_ = true;
    std::vector<std::uint8_t> pvs_; // One bit per cluster.
    std::array<std::array<float, 4>, 4> planes_{};
};

} // namespace openbl::formats
