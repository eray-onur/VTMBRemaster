#pragma once

// Projecting an infodecal onto a triangle mesh (a prop), with the same
// conventions as world decals (project_decal in bsp.hpp): the decal faces the
// surface nearest its origin, and its up axis follows world +z on walls and
// world +y on floors and ceilings.

#include "openbl/formats/bsp.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openbl::formats {

struct MeshDecal {
    std::vector<Vec3> positions; // Mesh space, lifted slightly off the surface.
    std::vector<Vec3> normals;   // The triangle each vertex came from.
    std::vector<std::array<float, 2>> uvs; // Decal texture coordinates, 0..1.
    std::vector<std::uint32_t> indices;
    // Where each vertex came from, to interpolate other attributes: the first
    // index (into `indices` of the input) of its source triangle, and its
    // barycentric weights on that triangle's three vertices.
    std::vector<std::size_t> source_triangles;
    std::vector<std::array<float, 3>> barycentrics;
    float distance = 0;          // From the origin to the surface it was projected on.
};

// `origin`, `world_z` and `world_y` are in the mesh's space (world +z and +y
// rotated into it). Nullopt if no front-facing triangle is within `reach`.
std::optional<MeshDecal> project_mesh_decal(std::span<const Vec3> positions, std::span<const std::uint32_t> indices,
                                            const Vec3& origin, float width, float height, const Vec3& world_z,
                                            const Vec3& world_y, float reach = 4.0f, float lift = 0.1f);

} // namespace openbl::formats
