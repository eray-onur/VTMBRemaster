#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace openbl::formats {

// Writes 8-bit RGBA pixels (top row first) as a PNG file. Throws std::runtime_error.
void write_png(const std::filesystem::path& file, std::span<const std::uint8_t> rgba, std::uint32_t width,
               std::uint32_t height);

} // namespace openbl::formats
