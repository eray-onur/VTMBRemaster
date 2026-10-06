#include "openbl/formats/error.hpp"
#include "openbl/formats/image_read.hpp"
#include "openbl/formats/image_write.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

using namespace openbl::formats;

TEST_CASE("PNG round trip") {
    openbl::test::TempDir tmp;
    std::vector<std::uint8_t> rgba(5 * 3 * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        rgba[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    const auto file = tmp.path() / "a.png";
    write_png(file, rgba, 5, 3);
    const Image img = read_image(file);
    CHECK(img.width == 5);
    CHECK(img.height == 3);
    CHECK(img.rgba == rgba);
}

TEST_CASE("TGA decoding") {
    // 2x2, 24-bit, bottom-left origin, RLE: a run of two red pixels, then two raw pixels.
    const std::vector<std::uint8_t> tga = {0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0,
                                           0x81, 0, 0, 255,                     // run: 2x red (BGR)
                                           0x01, 255, 0, 0, 0, 255, 0};         // raw: blue, green
    const auto bytes = std::as_bytes(std::span(tga));
    const Image img = decode_tga(bytes);
    REQUIRE(img.rgba.size() == 16);
    // Bottom-left origin: the file's first row is the image's bottom row.
    CHECK(img.rgba[8] == 255);  // bottom-left: red
    CHECK(img.rgba[2] == 255);  // top-left: blue
    CHECK(img.rgba[5] == 255);  // top-right: green
    CHECK(img.rgba[3] == 255);  // alpha
    CHECK_THROWS_AS(decode_tga(bytes.first(10)), FormatError);
}

TEST_CASE("Image readers reject bad data") {
    const std::vector<std::byte> junk(16, std::byte{0});
    CHECK_THROWS_AS(decode_png(junk), FormatError);
}
