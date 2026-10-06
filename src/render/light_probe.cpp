#include "openbl/render/light_probe.hpp"

#include <algorithm>
#include <cmath>

namespace openbl::render {

FloorLightProbe::FloorLightProbe(const formats::BspFile& bsp) : bsp_(bsp) {
    for (std::size_t f = 0; f < bsp.faces.size(); ++f) {
        const auto& face = bsp.faces[f];
        if (face.light_offset < 0 || face.style_count() == 0 || face.dispinfo >= 0) {
            continue;
        }
        const auto& plane = bsp.planes[face.plane];
        const float nz = face.side ? -plane.normal[2] : plane.normal[2];
        if (nz < 0.7f) {
            continue; // Not a floor.
        }
        Floor floor{f, bsp.face_polygon(face), INFINITY, INFINITY, -INFINITY, -INFINITY};
        for (const auto& p : floor.polygon) {
            floor.min_x = std::min(floor.min_x, p[0]);
            floor.min_y = std::min(floor.min_y, p[1]);
            floor.max_x = std::max(floor.max_x, p[0]);
            floor.max_y = std::max(floor.max_y, p[1]);
        }
        floors_.push_back(std::move(floor));
    }
}

std::optional<std::array<float, 3>> FloorLightProbe::sample(const formats::Vec3& point, float max_drop) const {
    const Floor* best = nullptr;
    float best_z = -INFINITY;
    for (const Floor& floor : floors_) {
        if (point[0] < floor.min_x || point[0] > floor.max_x || point[1] < floor.min_y || point[1] > floor.max_y) {
            continue;
        }
        // Point-in-polygon in XY (crossing count).
        bool inside = false;
        const auto& poly = floor.polygon;
        for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            if ((poly[i][1] > point[1]) != (poly[j][1] > point[1]) &&
                point[0] < (poly[j][0] - poly[i][0]) * (point[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0]) {
                inside = !inside;
            }
        }
        if (!inside) {
            continue;
        }
        const auto& plane = bsp_.planes[bsp_.faces[floor.face].plane];
        const float z = (plane.dist - plane.normal[0] * point[0] - plane.normal[1] * point[1]) / plane.normal[2];
        if (z <= point[2] + 1.0f && z >= point[2] - max_drop && z > best_z) {
            best_z = z;
            best = &floor;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    const auto& face = bsp_.faces[best->face];
    const auto& ti = bsp_.texinfos[face.texinfo];
    const formats::Vec3 p{point[0], point[1], best_z};
    auto luxel = [&](int axis) {
        const auto& v = ti.lightmap_vecs[static_cast<std::size_t>(axis)];
        const float l = p[0] * v[0] + p[1] * v[1] + p[2] * v[2] + v[3] - static_cast<float>(face.lightmap_mins[static_cast<std::size_t>(axis)]);
        return static_cast<std::size_t>(std::clamp(std::lround(l), 0L, static_cast<long>(face.lightmap_size[static_cast<std::size_t>(axis)])));
    };
    const std::size_t width = static_cast<std::size_t>(face.lightmap_size[0]) + 1;
    const std::byte* data = bsp_.lighting.data() + face.light_offset + (luxel(1) * width + luxel(0)) * 4;
    return formats::decode_rgbe(data);
}

} // namespace openbl::render
