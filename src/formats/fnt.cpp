#include "openbl/formats/fnt.hpp"

#include "binary_reader.hpp"

namespace openbl::formats {

namespace {
constexpr std::size_t kHeaderSize = 36;
constexpr std::size_t kGlyphOffset = kHeaderSize + 256;
constexpr std::size_t kGlyphSize = 44;
constexpr std::size_t kPageSize = 16;
} // namespace

// Layout: i32 page count, i32 footer offset, i32 1, five i32 size values,
// i32 glyph table offset (292); a 256-byte character map; 44-byte glyphs up to
// the footer; footer: one {i32 width, i32 height, i32, i32 0} per page, then
// page names that the shipped files do not keep reliably (the engine is taken
// to derive them from the file name).
FntFile parse_fnt(std::span<const std::byte> data) {
    detail::BinaryReader r(data, "FNT");
    FntFile font;
    const std::int32_t page_count = r.i32();
    const std::int32_t footer = r.i32();
    r.i32();
    for (auto& v : font.header) {
        v = r.i32();
    }
    const std::int32_t glyph_offset = r.i32();
    if (glyph_offset != static_cast<std::int32_t>(kGlyphOffset) || footer < glyph_offset || page_count < 1 ||
        page_count > 64 || (footer - glyph_offset) % static_cast<std::int32_t>(kGlyphSize) != 0 ||
        static_cast<std::size_t>(footer) + kPageSize * static_cast<std::size_t>(page_count) > data.size()) {
        r.fail("unexpected layout");
    }
    const auto map = r.bytes(256);
    for (std::size_t i = 0; i < 256; ++i) {
        font.char_map[i] = std::to_integer<std::uint8_t>(map[i]);
    }
    const std::size_t count = static_cast<std::size_t>(footer - glyph_offset) / kGlyphSize;
    for (std::size_t g = 0; g < count; ++g) {
        FntGlyph glyph;
        for (auto& b : glyph.unknown) {
            b = r.u8();
        }
        glyph.unknown2 = r.u16();
        glyph.y_offset = static_cast<std::int16_t>(r.u16());
        glyph.x_offset = static_cast<std::int16_t>(r.u16());
        glyph.page = static_cast<std::int16_t>(r.u16());
        glyph.width = static_cast<std::int16_t>(r.u16());
        for (auto& f : glyph.uv) {
            f = r.f32();
        }
        if (glyph.page < 0 || glyph.page >= page_count) {
            r.fail("glyph on a missing page");
        }
        font.glyphs.push_back(glyph);
    }
    for (std::int32_t p = 0; p < page_count; ++p) {
        FntPage page;
        page.width = r.i32();
        page.height = r.i32();
        page.unknown = r.i32();
        r.i32();
        font.pages.push_back(page);
    }
    for (const auto g : font.char_map) {
        if (g >= font.glyphs.size()) {
            r.fail("character map points past the glyph table");
        }
    }
    return font;
}

} // namespace openbl::formats
