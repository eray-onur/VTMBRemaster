#pragma once

// Collision models (.phy): one or more solids, each a set of convex pieces,
// followed by a KeyValues text block with mass, damping and ragdoll data.
// See docs/formats/physics.md.

#include "openbl/formats/keyvalues.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
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

// A "solid" text block: physical properties of one solid.
struct PhySolidInfo {
    std::int32_t index = -1;  // Into PhyFile::solids.
    std::string name;         // Bone name for ragdolls.
    std::string parent;       // Parent bone name (ragdolls only).
    std::string surface_prop;
    std::array<float, 3> origin{}, angles{};
    float mass = 0.0f, mass_bias = 1.0f;
    float damping = 0.0f, rotation_damping = 0.0f, inertia = 1.0f, volume = 0.0f;
};

struct PhyJointLimit {
    float min = 0.0f, max = 0.0f; // Degrees.
    float friction = 0.0f;
};

// A ragdoll joint between two solids. Limits are rotations about the child
// bone's own x, y and z axes, measured from the bind pose (x runs along the
// bone, so its limits bound twist). See docs/formats/physics.md.
struct PhyRagdollConstraint {
    std::int32_t parent = -1, child = -1; // Solid indices.
    std::array<PhyJointLimit, 3> limits{};
};

struct PhyEditParams {
    std::string root_name;
    float total_mass = 0.0f;
    bool concave = false;
    std::vector<std::pair<std::string, std::string>> joint_merges; // Bone pairs merged into one solid.
};

// A "break" block: a gib model spawned when the prop breaks.
struct PhyBreak {
    std::string model;
    float health = 0.0f, fade_time = 0.0f;
    std::array<float, 3> offset{};
};

struct PhyFile {
    std::int32_t checksum = 0; // Equals the .mdl checksum when the file is current.
    std::vector<PhySolid> solids;
    KvDocument text; // The raw text block; the members below are read from it.
    std::vector<PhySolidInfo> solid_info;
    std::vector<PhyRagdollConstraint> constraints;
    PhyEditParams edit;
    std::vector<PhyBreak> breaks;
};

// Throws FormatError on malformed binary data.
PhyFile parse_phy(std::span<const std::byte> data);

} // namespace openbl::formats
