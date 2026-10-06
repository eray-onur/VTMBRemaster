#include "openbl/formats/error.hpp"
#include "openbl/formats/image_decode.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

using namespace openbl::formats;

namespace {

std::vector<std::byte> raw(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

std::vector<std::uint8_t> pixel(const std::vector<std::uint8_t>& rgba, std::size_t i) {
    return {rgba.begin() + static_cast<std::ptrdiff_t>(i * 4), rgba.begin() + static_cast<std::ptrdiff_t>(i * 4 + 4)};
}

using Px = std::vector<std::uint8_t>;

} // namespace

TEST_CASE("DXT1 four-color block") {
    // c0 = red (0xF800), c1 = blue (0x001F); row 0 uses indices 0,1,2,3.
    const auto block = raw({0x00, 0xF8, 0x1F, 0x00, 0b11100100, 0, 0, 0});
    const auto rgba = decode_to_rgba8(VtfFormat::DXT1, block, 4, 4);
    CHECK(pixel(rgba, 0) == Px{255, 0, 0, 255});
    CHECK(pixel(rgba, 1) == Px{0, 0, 255, 255});
    CHECK(pixel(rgba, 2) == Px{170, 0, 85, 255});
    CHECK(pixel(rgba, 3) == Px{85, 0, 170, 255});
    CHECK(pixel(rgba, 15) == Px{255, 0, 0, 255});
}

TEST_CASE("DXT1 three-color block with transparency") {
    // c0 <= c1 selects 3-color mode; index 3 is transparent black.
    const auto block = raw({0x1F, 0x00, 0x00, 0xF8, 0b11100100, 0, 0, 0});
    const auto rgba = decode_to_rgba8(VtfFormat::DXT1, block, 4, 4);
    CHECK(pixel(rgba, 2) == Px{127, 0, 127, 255});
    CHECK(pixel(rgba, 3) == Px{0, 0, 0, 0});
}

TEST_CASE("DXT5 interpolated alpha") {
    // a0 = 255, a1 = 0 (8-alpha mode); pixel 0 uses index 0, pixel 1 index 1, pixel 2 index 2.
    const auto block = raw({255, 0, 0b10001000, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0});
    const auto rgba = decode_to_rgba8(VtfFormat::DXT5, block, 4, 4);
    CHECK(pixel(rgba, 0) == Px{255, 255, 255, 255});
    CHECK(pixel(rgba, 1)[3] == 0);
    CHECK(pixel(rgba, 2)[3] == 218);
}

TEST_CASE("DXT images smaller than a block") {
    const auto block = raw({0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0});
    const auto rgba = decode_to_rgba8(VtfFormat::DXT1, block, 2, 1);
    CHECK(rgba.size() == 8);
    CHECK(pixel(rgba, 1) == Px{255, 0, 0, 255});
}

TEST_CASE("Uncompressed formats") {
    CHECK(decode_to_rgba8(VtfFormat::BGR888, raw({1, 2, 3}), 1, 1) == Px{3, 2, 1, 255});
    CHECK(decode_to_rgba8(VtfFormat::BGRA8888, raw({1, 2, 3, 4}), 1, 1) == Px{3, 2, 1, 4});
    CHECK(decode_to_rgba8(VtfFormat::RGBA8888, raw({1, 2, 3, 4}), 1, 1) == Px{1, 2, 3, 4});
    CHECK(decode_to_rgba8(VtfFormat::ABGR8888, raw({1, 2, 3, 4}), 1, 1) == Px{4, 3, 2, 1});
    CHECK(decode_to_rgba8(VtfFormat::RGB565, raw({0x00, 0xF8}), 1, 1) == Px{255, 0, 0, 255});
    CHECK(decode_to_rgba8(VtfFormat::IA88, raw({9, 7}), 1, 1) == Px{9, 9, 9, 7});
    CHECK(decode_to_rgba8(VtfFormat::BGR888_BlueScreen, raw({255, 0, 0}), 1, 1) == Px{0, 0, 255, 0});
}

TEST_CASE("Decode rejects short input and unsupported formats") {
    CHECK_THROWS_AS(decode_to_rgba8(VtfFormat::DXT1, raw({0, 0, 0}), 4, 4), FormatError);
    CHECK_THROWS_AS(decode_to_rgba8(VtfFormat::RGBA8888, raw({0, 0, 0, 0}), 2, 1), FormatError);
    CHECK_THROWS_AS(decode_to_rgba8(VtfFormat::P8, raw({0}), 1, 1), FormatError);
}
