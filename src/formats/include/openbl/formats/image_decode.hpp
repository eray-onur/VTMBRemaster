#pragma once

#include "openbl/formats/vtf.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

bool can_decode_to_rgba8(VtfFormat format);

// Decodes one image into tightly packed 8-bit RGBA, row-major, top row first.
// Used by tools and tests; the renderer uploads block-compressed data as is.
// Throws FormatError for unsupported formats or too little input.
std::vector<std::uint8_t> decode_to_rgba8(VtfFormat format, std::span<const std::byte> data, std::uint32_t width,
                                          std::uint32_t height);

} // namespace openbl::formats
