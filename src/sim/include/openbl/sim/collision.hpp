#pragma once

// Swept-box traces against a map's collision: its brushes (through the BSP
// tree, the compiler's bevel planes included, as the original traces boxes)
// and its displacement surfaces. Brushes plus displacements reproduce the
// compiled world collision (docs/formats/maps.md#brush-collision).
// Brush entities (doors) are traced in their own model space; World combines
// them with their current transforms.

#include "openbl/formats/bsp.hpp"
#include "openbl/sim/transform.hpp"

#include <cstdint>
#include <vector>

namespace openbl::sim {

namespace contents {
constexpr std::int32_t Solid = 0x1;
constexpr std::int32_t Window = 0x2;
constexpr std::int32_t Grate = 0x8;
constexpr std::int32_t Moveable = 0x4000;
constexpr std::int32_t PlayerClip = 0x10000;
constexpr std::int32_t MonsterClip = 0x20000;
// What stops the player (Source's MASK_PLAYERSOLID, without monsters).
constexpr std::int32_t PlayerSolid = Solid | Window | Grate | Moveable | PlayerClip;
} // namespace contents

struct Trace {
    float fraction = 1.0f;     // How far along start -> end the box got.
    formats::Vec3 end{};       // Where it stopped.
    formats::Vec3 normal{};    // The surface it hit (fraction < 1).
    bool start_solid = false;  // The box started inside something.
    bool all_solid = false;    // It never left it.
    std::int32_t contents = 0; // Of what it hit.
    std::int32_t entity = -1;  // Map entity index of a brush entity it hit; -1: the world.
};

class Collision {
public:
    explicit Collision(const formats::BspFile& bsp);

    // Sweeps the box [mins, maxs] (relative to the moving point) from start to
    // end through brush model `model` (0: the world, with its displacements),
    // in that model's own space, against brushes whose contents meet `mask`.
    Trace trace(std::size_t model, const formats::Vec3& start, const formats::Vec3& end, const formats::Vec3& mins,
                const formats::Vec3& maxs, std::int32_t mask) const;
    // The same through a moved model: `transform` takes its model space to
    // the world (map pose and motion). A rotated box is traced as the box
    // around it.
    Trace trace_moved(std::size_t model, const Transform& transform, const formats::Vec3& start,
                      const formats::Vec3& end, const formats::Vec3& mins, const formats::Vec3& maxs,
                      std::int32_t mask) const;

private:
    struct Sweep;
    void trace_node(Sweep& s, std::int32_t node) const;
    void trace_brush(Sweep& s, std::size_t brush) const;
    void trace_displacements(Sweep& s) const;

    struct Triangle {
        formats::Vec3 a, b, c;
    };
    struct Displacement {
        formats::Vec3 mins, maxs;
        std::vector<Triangle> triangles;
    };

    const formats::BspFile& bsp_;
    std::vector<Displacement> displacements_;
    mutable std::vector<std::uint32_t> brush_stamp_; // Each brush once per trace.
    mutable std::uint32_t stamp_ = 0;
};

} // namespace openbl::sim
