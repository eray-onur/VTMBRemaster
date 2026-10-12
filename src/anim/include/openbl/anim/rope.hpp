#pragma once

// Ropes and cables between two points (keyframe_rope, move_rope;
// docs/RENDERING.md#ropes-and-cables): a chain of `subdiv` + 2 nodes whose
// links share the rope's length (the distance plus its slack), hanging at
// rest under gravity, drawn as a smooth curve through the nodes.

#include <array>
#include <vector>

namespace openbl::anim {

using Vec3 = std::array<float, 3>;

// The chain's nodes at rest, from `a` to `b` (both included), gravity along
// -z. `slack`: length beyond the straight distance, in units; `subdiv`:
// nodes between the ends (clamped to 0-8).
std::vector<Vec3> rope_rest_nodes(const Vec3& a, const Vec3& b, float slack, int subdiv);

// Points on a Catmull-Rom curve through `nodes`, `steps` per link, ends
// included (the end links are extended by mirroring their neighbours).
std::vector<Vec3> rope_curve(const std::vector<Vec3>& nodes, int steps);

} // namespace openbl::anim
