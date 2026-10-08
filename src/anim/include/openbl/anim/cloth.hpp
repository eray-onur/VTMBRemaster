#pragma once

// Particle cloth for hair, skirts and coats (docs/ANIMATION.md): the model's
// cloth pieces (formats::MdlCloth) simulated with Verlet integration and the
// model's own distance constraints, relaxed Jakobsen-style with their stored
// weights, colliding with the model's capsules. The first particles of a piece
// are attached: they follow the skeleton exactly.

#include "openbl/formats/mdl.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::anim {

using Vec3 = std::array<float, 3>;

struct ClothSettings {
    float gravity = 386.0f;          // Units (inches) per second squared, along -z in model space.
    float damping = 0.02f;           // Fraction of velocity lost per step.
    std::uint32_t iterations = 4;    // Constraint relaxation passes per step.
    float step = 1.0f / 60.0f;       // Fixed time step, seconds.
    std::uint32_t max_steps = 6;     // Per update; longer gaps are not caught up.
    // Pull of each free particle towards its skinned (authored) position, per
    // step: keeps the authored silhouette while the cloth swings. 0 = none.
    float shape_stiffness = 0.02f;
    // If a particle ends a step farther than this from its skinned position
    // (a teleport, or a bad frame), the piece restarts from the skinned shape.
    float max_stray = 64.0f;
    // Free particles whose skinned position lies within this distance of a
    // capsule's surface are clothing on the body: they may not sink deeper
    // than `body_margin` behind their skinned position, measured away from
    // the capsule's axis. The capsules alone are too thin to keep a skirt
    // out of the hips. 0 = off.
    float body_reach = 8.0f;
    float body_margin = 0.25f;
};

// A collision capsule in model space (start, end, radius).
struct ClothCapsule {
    Vec3 start{}, end{};
    float radius = 0.0f;
};

class ClothSimulation {
public:
    // `skinned`: every particle's position with the current skinning (the
    // authored shape); the cloth starts there.
    ClothSimulation(const formats::MdlCloth& cloth, std::span<const Vec3> skinned, ClothSettings settings = {});

    // Advances by `dt` seconds (in fixed steps). `skinned` gives every
    // particle's skinned position this frame: attached particles snap to it,
    // free ones are pulled towards it by shape_stiffness.
    void update(float dt, std::span<const Vec3> skinned, std::span<const ClothCapsule> capsules);

    const std::vector<Vec3>& positions() const { return positions_; }
    // Per-particle normals from the piece's triangles (unnormalised sums, then
    // normalised; zero for particles in no triangle).
    std::vector<Vec3> normals() const;

    std::size_t particle_count() const { return positions_.size(); }

private:
    void step(std::span<const Vec3> skinned, std::span<const ClothCapsule> capsules);

    std::uint32_t attached_ = 0;
    // The first group keeps its pairs at their rest length; the second only
    // keeps them from coming closer than theirs (docs/ANIMATION.md).
    std::vector<formats::MdlClothConstraint> constraints_;
    std::size_t primary_ = 0; // Constraints in the first group.
    std::vector<std::array<std::uint16_t, 3>> triangles_;
    ClothSettings settings_;
    std::vector<Vec3> positions_;
    std::vector<Vec3> previous_;
    std::vector<Vec3> body_out_; // Per particle, this step: out from the nearest capsule, or zero.
    float pending_ = 0.0f; // Time not yet simulated.
};

} // namespace openbl::anim
