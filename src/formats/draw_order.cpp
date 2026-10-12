#include "openbl/formats/draw_order.hpp"

#include <algorithm>

namespace openbl::formats {

BspDrawOrder::BspDrawOrder(const BspFile& bsp)
    : bsp_(bsp), leaf_slots_(bsp.leafs.size(), 0), node_slots_(bsp.nodes.size(), 0) {}

void BspDrawOrder::update(const Vec3& eye) {
    leaf_slots_.resize(bsp_.leafs.size(), 0); // The BSP may have been filled in after construction.
    node_slots_.resize(bsp_.nodes.size(), 0);
    std::uint32_t slot = 0;
    if (!bsp_.models.empty()) {
        visit(bsp_.models[0].head_node, eye, slot, 0);
    }
}

void BspDrawOrder::visit(std::int32_t node, const Vec3& eye, std::uint32_t& slot, int depth) {
    if (node < 0) {
        const auto leaf = static_cast<std::size_t>(-static_cast<std::int64_t>(node) - 1);
        if (leaf < leaf_slots_.size()) {
            leaf_slots_[leaf] = slot++;
        }
        return;
    }
    const auto n = static_cast<std::size_t>(node);
    if (n >= bsp_.nodes.size() || depth > 1024) {
        return; // Malformed tree.
    }
    const BspNode& bn = bsp_.nodes[n];
    const BspPlane& p = bsp_.planes[static_cast<std::size_t>(bn.plane)];
    const float d = p.normal[0] * eye[0] + p.normal[1] * eye[1] + p.normal[2] * eye[2] - p.dist;
    const int near_side = d >= 0 ? 0 : 1;
    // Far side first, then the faces on the plane, then the side the eye is on.
    visit(bn.children[static_cast<std::size_t>(1 - near_side)], eye, slot, depth + 1);
    node_slots_[n] = slot++;
    visit(bn.children[static_cast<std::size_t>(near_side)], eye, slot, depth + 1);
}

std::uint32_t BspDrawOrder::nearest_slot(const std::vector<std::uint32_t>& leaves) const {
    std::uint32_t slot = 0;
    for (std::uint32_t leaf : leaves) {
        if (leaf < leaf_slots_.size()) {
            slot = std::max(slot, leaf_slots_[leaf]);
        }
    }
    return slot;
}

namespace {

void collect_leaves(const BspFile& bsp, std::int32_t node, const Vec3& mins, const Vec3& maxs,
                    std::vector<std::uint32_t>& out, int depth) {
    while (node >= 0) {
        const auto n = static_cast<std::size_t>(node);
        if (n >= bsp.nodes.size() || depth > 1024) {
            return;
        }
        const BspNode& bn = bsp.nodes[n];
        const BspPlane& p = bsp.planes[static_cast<std::size_t>(bn.plane)];
        // The box corner furthest along the normal, and the one furthest against it.
        float hi = -p.dist, lo = -p.dist;
        for (std::size_t k = 0; k < 3; ++k) {
            hi += p.normal[k] * (p.normal[k] >= 0 ? maxs[k] : mins[k]);
            lo += p.normal[k] * (p.normal[k] >= 0 ? mins[k] : maxs[k]);
        }
        if (lo >= 0) {
            node = bn.children[0];
        } else if (hi < 0) {
            node = bn.children[1];
        } else {
            collect_leaves(bsp, bn.children[0], mins, maxs, out, depth + 1);
            node = bn.children[1];
        }
        ++depth;
    }
    const auto leaf = static_cast<std::size_t>(-static_cast<std::int64_t>(node) - 1);
    if (leaf < bsp.leafs.size() && !(bsp.leafs[leaf].contents & contents::Solid)) {
        out.push_back(static_cast<std::uint32_t>(leaf));
    }
}

} // namespace

std::vector<std::uint32_t> leaves_in_box(const BspFile& bsp, const Vec3& mins, const Vec3& maxs) {
    std::vector<std::uint32_t> out;
    if (!bsp.models.empty()) {
        collect_leaves(bsp, bsp.models[0].head_node, mins, maxs, out, 0);
    }
    if (out.empty()) {
        const Vec3 c{(mins[0] + maxs[0]) * 0.5f, (mins[1] + maxs[1]) * 0.5f, (mins[2] + maxs[2]) * 0.5f};
        out.push_back(static_cast<std::uint32_t>(bsp.leaf_at(c)));
    }
    return out;
}

std::vector<std::int32_t> face_nodes(const BspFile& bsp) {
    std::vector<std::int32_t> out(bsp.faces.size(), -1);
    for (std::size_t n = 0; n < bsp.nodes.size(); ++n) {
        const BspNode& node = bsp.nodes[n];
        for (std::size_t i = 0; i < node.num_faces; ++i) {
            const std::size_t f = std::size_t{node.first_face} + i;
            if (f < out.size()) {
                out[f] = static_cast<std::int32_t>(n);
            }
        }
    }
    return out;
}

float static_prop_fade(const BspStaticProp& prop, float distance) {
    if (!(prop.flags & kStaticPropFades)) {
        return 1.0f;
    }
    // engine.dll keeps min², max² and 255 / (max² - min²) per fading prop and
    // tests the squared distance: gone from max, solid up to min.
    const float d2 = distance * distance;
    const float min2 = prop.fade_min * prop.fade_min;
    const float max2 = prop.fade_max * prop.fade_max;
    if (d2 >= max2) {
        return 0.0f;
    }
    if (d2 <= min2) {
        return 1.0f;
    }
    return std::clamp((max2 - d2) / (max2 - min2), 0.0f, 1.0f);
}

} // namespace openbl::formats
