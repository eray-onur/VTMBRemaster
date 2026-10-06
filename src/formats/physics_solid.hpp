#pragma once

// Internal: decoding of one physics solid, shared by .phy files and the BSP
// collision lump. See docs/formats/physics.md.

#include "binary_reader.hpp"
#include "openbl/formats/phy.hpp"

#include <cstddef>
#include <span>

namespace openbl::formats::detail {

// `solid` is the offset of the solid's data (after its i32 size); `size` its length.
PhySolid parse_physics_solid(std::span<const std::byte> data, std::size_t solid, std::size_t size, BinaryReader& r);

} // namespace openbl::formats::detail
