#include "openbl/formats/error.hpp"
#include "openbl/formats/image_read.hpp"
#include "openbl/formats/image_write.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cstring>

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

TEST_CASE("BMP decoding") {
    // 2x2, 8-bit, bottom-up, two palette entries; rows padded to 4 bytes.
    std::vector<std::uint8_t> bmp(14 + 40 + 8 + 8, 0);
    auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(bmp.data() + at, &v, 4); };
    bmp[0] = 'B';
    bmp[1] = 'M';
    put32(10, 14 + 40 + 8);
    put32(14, 40);
    put32(18, 2);
    put32(22, 2);
    bmp[26] = 1;
    bmp[28] = 8;
    put32(46, 2);
    const std::uint8_t palette[8] = {0, 0, 255, 0, 255, 0, 0, 0}; // BGRX: red, blue
    std::memcpy(bmp.data() + 54, palette, 8);
    bmp[62] = 1; // bottom row: blue, red
    bmp[63] = 0;
    bmp[66] = 0; // top row: red, red
    bmp[67] = 0;
    const auto bytes = std::as_bytes(std::span(bmp));
    const Image img = decode_bmp(bytes);
    REQUIRE(img.rgba.size() == 16);
    CHECK(img.rgba[0] == 255); // top-left red
    CHECK(img.rgba[8 + 2] == 255); // bottom-left blue
    CHECK(img.rgba[12] == 255); // bottom-right red
    CHECK_THROWS_AS(decode_bmp(bytes.first(20)), FormatError);
}
