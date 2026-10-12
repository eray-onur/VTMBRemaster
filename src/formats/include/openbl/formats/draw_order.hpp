#pragma once

// The order Source draws translucent things in (docs/RENDERING.md): the
// world's leaves back to front from the eye, with the faces lying on each BSP
// node between the node's far and near sides. Translucent entities go in the
// leaf holding their origin, after that leaf's world surfaces.
//
// Also the static prop distance fade, read from the original's engine.dll.

#include "openbl/formats/bsp.hpp"

#include <cstdint>
#include <vector>

namespace openbl::formats {

class BspDrawOrder {
public:
    explicit BspDrawOrder(const BspFile& bsp);

    // Recomputes the order for a viewpoint (`bsp` as it is now; it may have
    // been filled in after construction). Slots grow from back to front.
    void update(const Vec3& eye);

    std::uint32_t leaf_slot(std::size_t leaf) const { return leaf_slots_[leaf]; }
    // The slot of the faces lying on node `node` (its first_face, num_faces).
    std::uint32_t node_slot(std::size_t node) const { return node_slots_[node]; }
    std::uint32_t point_slot(const Vec3& point) const { return leaf_slots_[bsp_.leaf_at(point)]; }
    // An entity touching several leaves is drawn in the nearest of them.
    std::uint32_t nearest_slot(const std::vector<std::uint32_t>& leaves) const;

private:
    void visit(std::int32_t node, const Vec3& eye, std::uint32_t& slot, int depth);

    const BspFile& bsp_;
    std::vector<std::uint32_t> leaf_slots_;
    std::vector<std::uint32_t> node_slots_;
};

// For each face of the world model (model 0): the node it lies on, or -1.
std::vector<std::int32_t> face_nodes(const BspFile& bsp);

// The world's leaves overlapping a box (the leaf at its centre if none).
std::vector<std::uint32_t> leaves_in_box(const BspFile& bsp, const Vec3& mins, const Vec3& maxs);

// Static prop fade (props with flag 1): the opacity (0 to 1) at `distance`
// from the camera to the prop's origin. Linear in the squared distance: 1 up
// to fade_min, 0 from fade_max.
constexpr std::uint8_t kStaticPropFades = 0x1;
float static_prop_fade(const BspStaticProp& prop, float distance);

} // namespace openbl::formats
