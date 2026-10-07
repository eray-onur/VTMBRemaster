#pragma once

// Bitmap fonts (materials/fonts/<face>_<size>_<weight>_<style>.fnt). Glyph
// images live in page textures <font>-page0, -page1, ..., which are ordinary
// .vmt/.tth materials in the same folder. See docs/formats/fonts.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace openbl::formats {

struct FntGlyph {
    std::array<std::uint8_t, 18> unknown{}; // Small per-glyph values (perhaps per-row extents).
    std::uint16_t unknown2 = 0;              // 0-255.
    std::int16_t y_offset = 0;               // Top of the glyph relative to the baseline (negative = above).
    std::int16_t x_offset = 0;               // Probably the left bearing.
    std::int16_t page = 0;                   // Page texture index.
    std::int16_t width = 0;                  // Pixel width; matches the texture rectangle.
    std::array<float, 4> uv{};               // u0, v0, u1, v1 on the page texture.
};

struct FntPage {
    std::int32_t width = 0, height = 0;
    std::int32_t unknown = 0; // 16, 32, 48, ...
};

struct FntFile {
    std::array<std::int32_t, 5> header{}; // Size-related values; header[1] is the point size.
    std::array<std::uint8_t, 256> char_map{}; // Character code -> glyph index.
    std::vector<FntGlyph> glyphs;
    std::vector<FntPage> pages; // Page textures are named <font file name>-page<N>.

    const FntGlyph* glyph_for(unsigned char c) const {
        const std::size_t g = char_map[c];
        return g < glyphs.size() ? &glyphs[g] : nullptr;
    }
};

// Throws FormatError.
FntFile parse_fnt(std::span<const std::byte> data);

} // namespace openbl::formats
