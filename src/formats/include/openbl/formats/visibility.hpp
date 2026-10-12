#pragma once

// What a viewpoint can see of a map (docs/RENDERING.md#visibility): the
// clusters in the potentially visible set (PVS) of the eye's cluster that lie
// in an area reachable from the eye's area through open area portals, and
// the view frustum's four sides.

#include "openbl/formats/bsp.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

class ViewVisibility {
public:
    // Area portals start as the map sets them: a func_areaportal with
    // "StartOpen" "0" is closed (most belong to doors, drawn closed), every
    // other portal, and every func_areaportalwindow, is open.
    explicit ViewVisibility(const BspFile& bsp);

    // Opens or closes the portals with this key (a func_areaportal's
    // "portalnumber"); takes effect at the next update().
    void set_portal_open(std::uint16_t key, bool open);
    bool portal_open(std::uint16_t key) const;

    // `eye`: the point whose PVS is used. `view_projection`: the view's
    // column-major clip matrix, whose left, right, bottom and top planes
    // bound what is visible. An eye outside the visibility data (in solid,
    // or a map without any) sees every cluster.
    void update(const Vec3& eye, const std::array<float, 16>& view_projection);

    bool sees_all_clusters() const { return all_; }
    // Whether an area is reachable from the eye's area (true when areas do not apply).
    bool area(std::size_t area) const;
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
    void flood_areas(std::size_t eye_area);

    std::int32_t eye_cluster_ = -2; // -2: not computed yet.
    std::int64_t eye_area_ = -1;    // -1: none (everything reachable).
    bool portals_changed_ = true;
    bool all_ = true;
    std::vector<std::uint8_t> pvs_; // One bit per cluster.
    std::vector<std::uint16_t> closed_;                    // Keys of closed portals, sorted.
    std::vector<std::vector<std::uint16_t>> cluster_areas_; // The areas of each cluster's leaves.
    std::vector<std::uint8_t> area_open_;                  // Per area: reachable from the eye.
    std::vector<std::uint8_t> cluster_reached_;            // Per cluster: in a reachable area.
    std::array<std::array<float, 4>, 4> planes_{};
};

} // namespace openbl::formats
