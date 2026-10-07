#include "openbl/formats/error.hpp"
#include "openbl/formats/fnt.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>

using namespace openbl;
using namespace openbl::formats;

namespace {

std::vector<std::byte> build_font() {
    std::vector<std::byte> d;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        d.insert(d.end(), b, b + n);
    };
    auto i32 = [&](std::int32_t v) { put(&v, 4); };
    auto i16 = [&](std::int16_t v) { put(&v, 2); };
    const std::int32_t footer = 292 + 2 * 44;
    i32(1);
    i32(footer);
    i32(1);
    for (int v : {2, 6, 1, 7, 7}) {
        i32(v);
    }
    i32(292);
    std::uint8_t map[256] = {};
    map[static_cast<unsigned char>('A')] = 1;
    put(map, 256);
    for (int g = 0; g < 2; ++g) {
        std::uint8_t unknown[18] = {};
        put(unknown, 18);
        i16(0);
        i16(-5);
        i16(1);
        i16(0); // page
        i16(static_cast<std::int16_t>(4 + g));
        const float uv[4] = {0.25f, 0.5f, 0.25f + (4 + g) / 128.0f, 0.5f + 7 / 128.0f};
        put(uv, 16);
    }
    i32(128);
    i32(128);
    i32(16);
    i32(0);
    put("Test-page0", 11); // Stored names are not used.
    return d;
}

} // namespace

TEST_CASE("FNT bitmap fonts") {
    const auto font = parse_fnt(build_font());
    REQUIRE(font.glyphs.size() == 2);
    const auto* a = font.glyph_for('A');
    REQUIRE(a);
    CHECK(a->width == 5);
    CHECK(a->y_offset == -5);
    CHECK(a->page == 0);
    REQUIRE(font.pages.size() == 1);
    CHECK(font.pages[0].width == 128);

    auto bad = build_font();
    bad[32] = std::byte{0}; // Glyph offset.
    CHECK_THROWS_AS(parse_fnt(bad), FormatError);
}

TEST_CASE("FNT integration: every font in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t fonts = 0, glyphs = 0, width_matches = 0, missing_pages = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".fnt")) {
            continue;
        }
        INFO(path);
        FntFile font;
        REQUIRE_NOTHROW(font = parse_fnt(*gfs.read(path)));
        ++fonts;
        glyphs += font.glyphs.size();
        for (const auto& g : font.glyphs) {
            const auto& page = font.pages[static_cast<std::size_t>(g.page)];
            width_matches += std::lround((g.uv[2] - g.uv[0]) * static_cast<float>(page.width)) == g.width;
        }
        const std::string base = path.substr(0, path.size() - 4);
        for (std::size_t p = 0; p < font.pages.size(); ++p) {
            missing_pages += !gfs.exists(base + "-page" + std::to_string(p) + ".vmt");
        }
    }
    MESSAGE(fonts << " fonts, " << glyphs << " glyphs, " << width_matches << " widths matching their texture rectangle, "
                  << missing_pages << " missing pages");
    CHECK(fonts == 141);
    CHECK(missing_pages == 0);
    CHECK(width_matches * 100 >= glyphs * 99);
}
