#pragma once

// Collision models (.phy): one or more solids, each a set of convex pieces,
// followed by a KeyValues text block with mass, damping and ragdoll data.
// See docs/formats/physics.md.

#include "openbl/formats/keyvalues.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

struct PhyConvex {
    // Model space, in inches: the file stores metres with axes (x, -z, y).
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles; // Indices into vertices.
};

struct PhySolid {
    std::array<float, 3> mass_center{}; // Model space, inches.
    std::array<float, 3> inertia{};     // As stored (physics space).
    float radius = 0;                   // Bounding radius, inches.
    std::vector<PhyConvex> pieces;
};

struct PhyFile {
    std::int32_t checksum = 0; // Equals the .mdl checksum when the file is current.
    std::vector<PhySolid> solids;
    KvDocument text; // "solid" blocks (index, name, mass, ...), "editparams", ragdoll constraints, ...
};

// Throws FormatError on malformed binary data.
PhyFile parse_phy(std::span<const std::byte> data);

} // namespace openbl::formats
