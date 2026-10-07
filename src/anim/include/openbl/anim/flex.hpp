#pragma once

// Facial flexes: controller values -> flex weights (the model's flex rules)
// -> per-mesh morph scales (each mesh flex's ramp). See docs/ANIMATION.md.

#include "openbl/formats/mdl.hpp"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace openbl::anim {

// Controller values in MdlFile::flex_controllers order, all 0 (each clamped
// into its controller's range).
std::vector<float> default_flex_controllers(const formats::MdlFile& file);

// Sets a controller by name (case-insensitive), clamped into its range.
// Returns false if the model has no such controller.
bool set_flex_controller(const formats::MdlFile& file, std::vector<float>& controllers, std::string_view name, float value);

// Weight of every flex (MdlFile::flex_names order). Rules run in file order,
// so a rule can read flexes computed before it; flexes without a rule weigh 0.
// Division by zero gives 0.
std::vector<float> evaluate_flex_rules(const formats::MdlFile& file, std::span<const float> controllers);

// How much of a mesh flex applies at a flex weight: ramps up from targets[0]
// to targets[1], holds until targets[2], ramps down to targets[3].
float mesh_flex_scale(const std::array<float, 4>& targets, float weight);

} // namespace openbl::anim
