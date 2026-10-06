#include "openbl/formats/error.hpp"
#include "openbl/formats/vtf.hpp"
#include "texture_builder.hpp"

#include <doctest/doctest.h>

using namespace openbl::formats;
using namespace openbl::test;

TEST_CASE("vtf_image_size") {
    CHECK(vtf_image_size(VtfFormat::DXT1, 1, 1) == 8);
    CHECK(vtf_image_size(VtfFormat::DXT1, 256, 256) == 32768);
    CHECK(vtf_image_size(VtfFormat::DXT5, 4, 4) == 16);
    CHECK(vtf_image_size(VtfFormat::DXT5, 5, 5) == 64);
    CHECK(vtf_image_size(VtfFormat::BGR888, 2, 2) == 12);
    CHECK(vtf_image_size(VtfFormat::RGBA8888, 3, 1) == 12);
    CHECK_THROWS_AS(vtf_image_size(static_cast<VtfFormat>(99), 1, 1), FormatError);
}

TEST_CASE("VTF parse and image layout") {
    VtfSpec spec;
    spec.width = 32;
    spec.height = 16;
    spec.mip_count = 6;
    spec.frames = 2;
    const auto built = build_vtf(spec);
    const auto tex = VtfTexture::parse(built.data);

    CHECK(tex.header().width == 32);
    CHECK(tex.header().height == 16);
    CHECK(tex.header().format == VtfFormat::DXT1);
    CHECK(tex.face_count() == 1);
    CHECK(tex.largest_available_mip() == 0);
    CHECK(tex.expected_size() == built.data.size());
    CHECK(tex.mip_width(5) == 1);
    CHECK(tex.mip_height(4) == 1);
    CHECK(tex.thumbnail().size() == 8);

    for (std::uint32_t mip = 0; mip < 6; ++mip) {
        for (std::uint32_t frame = 0; frame < 2; ++frame) {
            const auto img = tex.image(mip, frame);
            CHECK(img.size() == vtf_image_size(VtfFormat::DXT1, tex.mip_width(mip), tex.mip_height(mip)));
            CHECK(img.front() == fill_value(mip, frame, 0));
            CHECK(img.back() == fill_value(mip, frame, 0));
        }
    }
    CHECK_THROWS_AS(tex.image(6), FormatError);
    CHECK_THROWS_AS(tex.image(0, 2), FormatError);
}

TEST_CASE("VTF cubemaps have 7 faces") {
    VtfSpec spec;
    spec.flags = vtf_flags::EnvMap;
    spec.format = VtfFormat::BGR888;
    spec.mip_count = 3;
    spec.width = spec.height = 4;
    const auto built = build_vtf(spec);
    const auto tex = VtfTexture::parse(built.data);
    CHECK(tex.face_count() == 7);
    CHECK(tex.image(1, 0, 6).front() == fill_value(1, 0, 6));
    CHECK(tex.image(0, 0, 3).front() == fill_value(0, 0, 3));
}

TEST_CASE("VTF truncated data drops the largest mips") {
    VtfSpec spec;
    spec.width = spec.height = 64;
    spec.mip_count = 7;
    auto built = build_vtf(spec);
    // Keep everything up to the end of mip 2 (third largest) plus a partial mip 1.
    built.data.resize(built.boundaries[5] + 3);
    const auto tex = VtfTexture::parse(built.data);
    CHECK(tex.largest_available_mip() == 2);
    CHECK(tex.has_mip(2));
    CHECK_FALSE(tex.has_mip(1));
    CHECK(tex.image(2).front() == fill_value(2, 0, 0));
    CHECK_THROWS_AS(tex.image(0), FormatError);
}

TEST_CASE("VTF rejects malformed headers") {
    auto built = build_vtf({});
    SUBCASE("signature") {
        built.data[0] = std::byte{'X'};
    }
    SUBCASE("version") {
        put_u32(built.data, 8, 5);
    }
    SUBCASE("zero width") {
        put_u16(built.data, 16, 0);
    }
    SUBCASE("unknown format") {
        put_u32(built.data, 52, 77);
    }
    SUBCASE("too many mips") {
        built.data[56] = std::byte{40};
    }
    SUBCASE("truncated header") {
        built.data.resize(40);
    }
    CHECK_THROWS_AS(VtfTexture::parse(built.data), FormatError);
}
