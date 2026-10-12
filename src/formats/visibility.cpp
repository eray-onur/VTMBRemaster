#include "openbl/formats/visibility.hpp"

#include "openbl/formats/error.hpp"

namespace openbl::formats {

ViewVisibility::ViewVisibility(const BspFile& bsp) : bsp_(bsp) {}

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
    if (bsp_.visibility.clusters > 0 && !bsp_.leafs.empty() && !bsp_.nodes.empty()) {
        const BspLeaf& leaf = bsp_.leafs[bsp_.leaf_at(eye)];
        if (!(leaf.contents & contents::Solid)) {
            cluster = leaf.cluster;
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
    return byte < pvs_.size() && (pvs_[byte] >> (cluster & 7) & 1) != 0;
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
