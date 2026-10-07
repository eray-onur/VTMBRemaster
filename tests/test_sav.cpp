#include "openbl/formats/error.hpp"
#include "openbl/formats/sav.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace openbl;
using namespace openbl::formats;

namespace {

struct Out {
    std::vector<std::byte> d;
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        d.insert(d.end(), b, b + n);
    }
    void i32(std::int32_t v) { raw(&v, 4); }
    void u16(std::uint16_t v) { raw(&v, 2); }
};

std::vector<std::byte> compress(const std::byte* data, std::size_t n) {
    uLongf size = compressBound(static_cast<uLong>(n));
    std::vector<std::byte> out(size);
    REQUIRE(compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(data),
                      static_cast<uLong>(n), 6) == Z_OK);
    out.resize(size);
    return out;
}

// A save with two header fields and two embedded files, the second large
// enough to need three 512 KiB chunks.
std::vector<std::byte> build_save(std::vector<std::byte>& big) {
    Out o;
    o.raw("JSAV", 4);
    o.i32(117);
    // Token table of 8 slots: "mapName" in slot 2, "mapCount" in slot 5.
    const char* slots[8] = {"", "", "mapName", "", "", "mapCount", "", ""};
    std::string table;
    for (const char* s : slots) {
        table += s;
        table.push_back('\0');
    }
    Out header;
    header.u16(4);
    header.u16(5);
    header.i32(3);
    header.u16(16);
    header.u16(2);
    char name[16] = "sp_tutorial_1";
    header.raw(name, 16);
    o.i32(static_cast<std::int32_t>(header.d.size()));
    o.i32(8);
    o.i32(static_cast<std::int32_t>(table.size()));
    o.raw(table.data(), table.size());
    o.raw(header.d.data(), header.d.size());

    auto add_file = [&](const std::string& file_name, const std::vector<std::byte>& data) {
        char n[260] = {};
        std::memcpy(n, file_name.c_str(), file_name.size());
        o.raw(n, 260);
        o.i32(static_cast<std::int32_t>(data.size()));
        for (std::size_t at = 0; at < data.size(); at += 512 * 1024) {
            const auto chunk = compress(data.data() + at, std::min<std::size_t>(512 * 1024, data.size() - at));
            o.i32(static_cast<std::int32_t>(chunk.size()));
            o.raw(chunk.data(), chunk.size());
        }
    };
    const std::vector<std::byte> small = {std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}};
    add_file("sp_tutorial_1.HL3", small);
    big.resize(1200 * 1024);
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<std::byte>((i * 7) ^ (i >> 9));
    }
    add_file("sp_tutorial_1.HL1", big);
    return o.d;
}

} // namespace

TEST_CASE("SAV container") {
    std::vector<std::byte> big;
    const auto data = build_save(big);
    const auto save = parse_sav(data);
    CHECK(save.version == 117);
    REQUIRE(save.header.size() == 2);
    CHECK(save.field("mapCount")->as_int() == 3);
    CHECK(save.field("mapName")->as_string() == "sp_tutorial_1");
    REQUIRE(save.files.size() == 2);
    CHECK(save.files[0].name == "sp_tutorial_1.HL3");
    CHECK(save.files[0].data.size() == 4);
    CHECK(save.files[1].data == big);

    auto bad = data;
    SUBCASE("signature") {
        bad[0] = std::byte{'X'};
    }
    SUBCASE("truncated") {
        bad.resize(bad.size() - 10);
    }
    SUBCASE("version") {
        bad[4] = std::byte{118};
    }
    CHECK_THROWS_AS(parse_sav(bad), FormatError);
}

// Reads the saves in OPENBL_SAVE_DIR, if set (e.g. <GOG>/Unofficial_Patch/save).
TEST_CASE("SAV integration: the user's saves parse") {
    const char* dir = std::getenv("OPENBL_SAVE_DIR");
    if (!dir || !*dir) {
        MESSAGE("OPENBL_SAVE_DIR not set; skipping");
        return;
    }
    std::size_t saves = 0, files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".sav" || entry.file_size() == 0) {
            continue;
        }
        INFO(entry.path().string());
        const auto data = test::read_raw(entry.path());
        SaveFile save;
        REQUIRE_NOTHROW(save = parse_sav(data));
        ++saves;
        files += save.files.size();
        CHECK(save.field("mapName"));
        for (const auto& f : save.files) {
            const bool state = f.name.ends_with(".HL1") || f.name.ends_with(".HL2");
            if (state && f.data.size() >= 4) {
                CHECK(std::memcmp(f.data.data(), "VALV", 4) == 0);
            }
        }
    }
    MESSAGE(saves << " saves, " << files << " embedded files");
}
