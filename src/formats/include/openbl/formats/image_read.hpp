#pragma once

// Reading screenshots for parity comparisons: PNG (8-bit greyscale, RGB or
// RGBA, non-interlaced) and TGA (uncompressed or RLE, 24 or 32 bits).

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace openbl::formats {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // Top row first.
};

// Throws FormatError for unsupported or corrupt data.
Image decode_png(std::span<const std::byte> data);
Image decode_tga(std::span<const std::byte> data);
// Uncompressed Windows bitmaps: 8-bit palettized, 24-bit or 32-bit.
Image decode_bmp(std::span<const std::byte> data);
// Chooses the decoder by file signature/extension. Throws FormatError or std::runtime_error.
Image read_image(const std::filesystem::path& file);

} // namespace openbl::formats
