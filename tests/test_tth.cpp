#include "openbl/formats/error.hpp"
#include "openbl/formats/image_decode.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"
#include "texture_builder.hpp"

#include <doctest/doctest.h>

using namespace openbl;
using namespace openbl::formats;
using namespace openbl::test;

namespace {

BuiltVtf sample_vtf() {
    VtfSpec spec;
    spec.width = spec.height = 64;
    spec.mip_count = 7;
    return build_vtf(spec);
}

std::optional<std::span<const std::byte>> as_span(const std::vector<std::byte>& v) {
    return std::span<const std::byte>(v);
}

} // namespace

TEST_CASE("TTH header") {
    const auto vtf = sample_vtf();
    const auto pair = build_tth(vtf, 3);
    const auto h = parse_tth_header(pair.tth);
    CHECK(h.version_major == 1);
    CHECK(h.mip_count == 7);
    CHECK(h.resident_mip_count == 3);
    CHECK(h.ranges.size() == 8);
    CHECK(h.vtf_prefix_size == vtf.boundaries[3]);
    CHECK(h.vtf_size() == vtf.data.size());
    CHECK(h.vtf_offset == 12 + 8 * 8);
    CHECK(h.needs_ttz());
    CHECK(h.ranges.back().ttz_end == pair.ttz.size());
}

TEST_CASE("TTH/TTZ assemble the original VTF") {
    const auto vtf = sample_vtf();
    for (std::uint8_t resident : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{7}}) {
        CAPTURE(resident);
        const auto pair = build_tth(vtf, resident);
        const auto out = assemble_vtf(pair.tth, resident == 7 ? std::nullopt : as_span(pair.ttz));
        CHECK(out.damage == TextureDamage::None);
        CHECK(out.data == vtf.data);
    }
}

TEST_CASE("TTH ignores padding after the VTF prefix") {
    const auto vtf = sample_vtf();
    auto pair = build_tth(vtf, 3);
    pair.tth.resize(pair.tth.size() + 16, std::byte{0xCD});
    const auto out = assemble_vtf(pair.tth, as_span(pair.ttz));
    CHECK(out.damage == TextureDamage::None);
    CHECK(out.data == vtf.data);
}

TEST_CASE("TTH damaged or missing TTZ") {
    const auto vtf = sample_vtf();
    auto pair = build_tth(vtf, 3);

    SUBCASE("missing ttz keeps the resident mips") {
        const auto out = assemble_vtf(pair.tth, std::nullopt);
        CHECK(out.damage == TextureDamage::MissingTtz);
        const auto tex = VtfTexture::parse(out.data);
        CHECK(tex.largest_available_mip() == 4);
    }
    SUBCASE("bad checksum keeps all data") {
        pair.ttz.back() ^= std::byte{1};
        const auto out = assemble_vtf(pair.tth, as_span(pair.ttz));
        CHECK(out.damage == TextureDamage::CorruptTtz);
        CHECK(out.data == vtf.data);
    }
    SUBCASE("truncated stream keeps the mips that decompressed") {
        const auto h = parse_tth_header(pair.tth);
        pair.ttz.resize(h.ranges[5].ttz_end);
        const auto out = assemble_vtf(pair.tth, as_span(pair.ttz));
        CHECK(out.damage == TextureDamage::CorruptTtz);
        CHECK(VtfTexture::parse(out.data).largest_available_mip() == 2);
    }
    SUBCASE("not zlib at all") {
        pair.ttz.assign(10, std::byte{0});
        const auto out = assemble_vtf(pair.tth, as_span(pair.ttz));
        CHECK(out.damage == TextureDamage::CorruptTtz);
        CHECK(out.data.size() == vtf.boundaries[3]);
    }
}

TEST_CASE("TTH rejects malformed headers") {
    auto pair = build_tth(sample_vtf(), 3);
    SUBCASE("signature") {
        pair.tth[0] = std::byte{'X'};
    }
    SUBCASE("version") {
        pair.tth[4] = std::byte{2};
    }
    SUBCASE("resident count above mip count") {
        pair.tth[7] = std::byte{9};
    }
    SUBCASE("prefix size mismatch") {
        put_u32(pair.tth, 8, 1);
    }
    SUBCASE("descending ranges") {
        put_u32(pair.tth, 12 + 8 * 2, 0);
    }
    SUBCASE("truncated prefix") {
        pair.tth.resize(pair.tth.size() - 1);
    }
    CHECK_THROWS_AS(parse_tth_header(pair.tth), FormatError);
}

TEST_CASE("TTH integration: every texture in the real install loads") {
    auto game = game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t textures = 0, missing = 0, corrupt = 0, decoded = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".tth")) {
            continue;
        }
        INFO(path);
        const auto tth = gfs.read(path);
        REQUIRE(tth);
        const auto ttz = gfs.read(path.substr(0, path.size() - 4) + ".ttz");
        AssembledVtf out;
        REQUIRE_NOTHROW(out = assemble_vtf(*tth, ttz ? as_span(*ttz) : std::nullopt));
        missing += out.damage == TextureDamage::MissingTtz;
        corrupt += out.damage == TextureDamage::CorruptTtz;

        std::optional<VtfTexture> tex;
        REQUIRE_NOTHROW(tex = VtfTexture::parse(std::move(out.data)));
        if (out.damage == TextureDamage::None) {
            CHECK(tex->largest_available_mip() == 0);
        }
        // Decoding everything is slow in debug builds; sample a subset.
        if (textures % 97 == 0) {
            const std::uint32_t mip = tex->largest_available_mip();
            CHECK_NOTHROW(decode_to_rgba8(tex->header().format, tex->image(mip), tex->mip_width(mip),
                                          tex->mip_height(mip)));
            ++decoded;
        }
        ++textures;
    }
    MESSAGE(textures << " textures, " << missing << " missing ttz, " << corrupt << " corrupt ttz, " << decoded
                     << " decoded");
    CHECK(textures > 10000);
    // Known damage in the Steam release; see docs/formats/textures.md.
    CHECK(missing <= 2);
    CHECK(corrupt <= 3);
}
