#include "openbl/formats/visibility.hpp"

#include "openbl/formats/error.hpp"

#include <algorithm>
#include <cstdlib>

namespace openbl::formats {

ViewVisibility::ViewVisibility(const BspFile& bsp) : bsp_(bsp) {
    for (const BspEntity& e : bsp.entities) {
        const auto cls = e.find("classname");
        const auto number = e.find("portalnumber");
        const auto open = e.find("StartOpen");
        if (cls != e.end() && cls->second == "func_areaportal" && number != e.end() && open != e.end() &&
            std::atoi(open->second.c_str()) == 0) {
            set_portal_open(static_cast<std::uint16_t>(std::atoi(number->second.c_str())), false);
        }
    }
    if (bsp.visibility.clusters > 0 && !bsp.areas.empty()) {
        cluster_areas_.resize(static_cast<std::size_t>(bsp.visibility.clusters));
        for (const BspLeaf& leaf : bsp.leafs) {
            if (leaf.cluster >= 0 && static_cast<std::size_t>(leaf.cluster) < cluster_areas_.size()) {
                auto& areas = cluster_areas_[static_cast<std::size_t>(leaf.cluster)];
                if (std::find(areas.begin(), areas.end(), leaf.area) == areas.end()) {
                    areas.push_back(leaf.area);
                }
            }
        }
    }
}

void ViewVisibility::set_portal_open(std::uint16_t key, bool open) {
    const auto it = std::lower_bound(closed_.begin(), closed_.end(), key);
    const bool closed = it != closed_.end() && *it == key;
    if (open && closed) {
        closed_.erase(it);
        portals_changed_ = true;
    } else if (!open && !closed) {
        closed_.insert(it, key);
        portals_changed_ = true;
    }
}

bool ViewVisibility::portal_open(std::uint16_t key) const {
    return !std::binary_search(closed_.begin(), closed_.end(), key);
}

// The areas reachable from the eye's through open portals, and the clusters lying in them.
void ViewVisibility::flood_areas(std::size_t eye_area) {
    area_open_.assign(bsp_.areas.size(), 0);
    std::vector<std::size_t> stack;
    if (eye_area < area_open_.size()) {
        area_open_[eye_area] = 1;
        stack.push_back(eye_area);
    }
    while (!stack.empty()) {
        const BspArea& a = bsp_.areas[stack.back()];
        stack.pop_back();
        for (std::int32_t i = 0; i < a.num_portals; ++i) {
            const auto p = static_cast<std::size_t>(a.first_portal + i);
            if (p >= bsp_.area_portals.size()) {
                break;
            }
            const BspAreaPortal& portal = bsp_.area_portals[p];
            if (portal.other_area < area_open_.size() && !area_open_[portal.other_area] && portal_open(portal.key)) {
                area_open_[portal.other_area] = 1;
                stack.push_back(portal.other_area);
            }
        }
    }
    cluster_reached_.assign(cluster_areas_.size(), 0);
    for (std::size_t c = 0; c < cluster_areas_.size(); ++c) {
        for (const std::uint16_t a : cluster_areas_[c]) {
            cluster_reached_[c] = cluster_reached_[c] || this->area(a);
        }
    }
}

bool ViewVisibility::area(std::size_t area) const {
    // Area 0 holds the leaves in solid and outside the map.
    return eye_area_ < 0 || area == 0 || area >= area_open_.size() || area_open_[area] != 0;
}

void ViewVisibility::update(const Vec3& eye, const std::array<float, 16>& view_projection) {
    // Clip-space rows of the matrix; a point is inside a side where
    // w +/- x (or y) >= 0.
    std::array<std::array<float, 4>, 4> row{};
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            row[r][c] = view_projection[c * 4 + r];
        }
    }
    for (std::size_t k = 0; k < 4; ++k) {
        planes_[0][k] = row[3][k] + row[0][k];
        planes_[1][k] = row[3][k] - row[0][k];
        planes_[2][k] = row[3][k] + row[1][k];
        planes_[3][k] = row[3][k] - row[1][k];
    }

    std::int32_t cluster = -1;
    std::int64_t eye_area = -1;
    if (bsp_.visibility.clusters > 0 && !bsp_.leafs.empty() && !bsp_.nodes.empty()) {
        const BspLeaf& leaf = bsp_.leafs[bsp_.leaf_at(eye)];
        if (!(leaf.contents & contents::Solid)) {
            cluster = leaf.cluster;
            eye_area = cluster_areas_.empty() || leaf.area == 0 ? -1 : leaf.area;
        }
    }
    if (eye_area != eye_area_ || portals_changed_) {
        eye_area_ = eye_area;
        portals_changed_ = false;
        if (eye_area >= 0) {
            flood_areas(static_cast<std::size_t>(eye_area));
        }
    }
    if (cluster == eye_cluster_) {
        return;
    }
    eye_cluster_ = cluster;
    all_ = cluster < 0;
    pvs_.clear();
    if (!all_) {
        try {
            pvs_ = bsp_.visibility.decompress(cluster);
        } catch (const FormatError&) {
            all_ = true;
        }
    }
}

bool ViewVisibility::cluster(std::int32_t cluster) const {
    if (all_ || cluster < 0) {
        return true;
    }
    const auto byte = static_cast<std::size_t>(cluster) >> 3;
    if (byte >= pvs_.size() || (pvs_[byte] >> (cluster & 7) & 1) == 0) {
        return false;
    }
    const auto c = static_cast<std::size_t>(cluster);
    return eye_area_ < 0 || c >= cluster_reached_.size() || cluster_reached_[c] != 0;
}

bool ViewVisibility::leaf(std::size_t leaf) const {
    return leaf >= bsp_.leafs.size() || cluster(bsp_.leafs[leaf].cluster);
}

bool ViewVisibility::any_leaf(std::span<const std::uint32_t> leaves) const {
    if (all_ || leaves.empty()) {
        return true;
    }
    for (std::uint32_t l : leaves) {
        if (leaf(l)) {
            return true;
        }
    }
    return false;
}

bool ViewVisibility::box(const Vec3& mins, const Vec3& maxs) const {
    for (const auto& p : planes_) {
        // The corner farthest along the plane's normal.
        const float x = p[0] >= 0 ? maxs[0] : mins[0];
        const float y = p[1] >= 0 ? maxs[1] : mins[1];
        const float z = p[2] >= 0 ? maxs[2] : mins[2];
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) {
            return false;
        }
    }
    return true;
}

} // namespace openbl::formats
