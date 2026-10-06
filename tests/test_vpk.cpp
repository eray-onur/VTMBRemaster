#include "openbl/formats/error.hpp"
#include "openbl/formats/vpk.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace openbl::formats;
using namespace openbl::test;

TEST_CASE("VPK round trip") {
    TempDir tmp;
    const auto file = tmp.path() / "pack000.vpk";
    const std::vector<VpkWriteEntry> in = {
        {"Sound\\Surfaces\\Scrape1.wav", bytes("RIFF....WAVE")},
        {"dlg/hollywood/isaac.dlg", bytes("{1}{hello}")},
        {"empty.txt", {}},
    };
    write_vpk(file, in);

    const auto archive = VpkArchive::open(file);
    CHECK(archive->kind() == VpkKind::Data);
    REQUIRE(archive->entries().size() == 3);
    CHECK(archive->entries()[0].path == "sound/surfaces/scrape1.wav");

    const VpkEntry* dlg = archive->find("DLG\\Hollywood\\Isaac.dlg");
    REQUIRE(dlg != nullptr);
    CHECK(archive->read(*dlg) == bytes("{1}{hello}"));
    CHECK(archive->read(*archive->find("empty.txt")).empty());
    CHECK(archive->find("missing") == nullptr);

    // Writing what we read must reproduce the file byte for byte.
    std::vector<VpkWriteEntry> again;
    for (const auto& e : archive->entries()) {
        again.push_back({e.path, archive->read(e)});
    }
    const auto copy = tmp.path() / "copy.vpk";
    write_vpk(copy, again);
    CHECK(read_raw(copy) == read_raw(file));
}

TEST_CASE("VPK empty archive is a bare 9-byte footer") {
    TempDir tmp;
    const auto file = tmp.path() / "pack100.vpk";
    write_vpk(file, {});
    CHECK(std::filesystem::file_size(file) == 9);
    CHECK(VpkArchive::open(file)->entries().empty());
}

TEST_CASE("VPK directory index") {
    TempDir tmp;
    const auto file = tmp.path() / "pack010.vpk";
    const std::vector<VpkWriteEntry> dirs = {{"cfg", {}}, {"dlg/chinatown", {}}};
    write_vpk(file, dirs, VpkKind::DirectoryIndex);
    const auto archive = VpkArchive::open(file);
    CHECK(archive->kind() == VpkKind::DirectoryIndex);
    CHECK(archive->entries().size() == 2);
    CHECK(archive->entries()[1].offset == 0);
}

TEST_CASE("VPK rejects malformed archives") {
    TempDir tmp;
    const auto file = tmp.path() / "bad.vpk";

    SUBCASE("too small") {
        write_raw(file, bytes("abc"));
        CHECK_THROWS_AS(VpkArchive::open(file), FormatError);
    }
    SUBCASE("directory offset past end") {
        write_raw(file, bytes(std::string("\x00\x00\x00\x00\xff\x00\x00\x00\x00", 9)));
        CHECK_THROWS_AS(VpkArchive::open(file), FormatError);
    }
    SUBCASE("entry count larger than directory") {
        write_raw(file, bytes(std::string("\x05\x00\x00\x00\x00\x00\x00\x00\x00", 9)));
        CHECK_THROWS_AS(VpkArchive::open(file), FormatError);
    }
    SUBCASE("unknown kind byte") {
        write_raw(file, bytes(std::string("\x00\x00\x00\x00\x00\x00\x00\x00\x07", 9)));
        CHECK_THROWS_AS(VpkArchive::open(file), FormatError);
    }
    SUBCASE("entry data overlaps directory") {
        // One entry "a" claiming 100 bytes at offset 0, but the directory starts at 0.
        std::string raw("\x01\x00\x00\x00"
                        "a"
                        "\x00\x00\x00\x00"
                        "\x64\x00\x00\x00"
                        "\x01\x00\x00\x00\x00\x00\x00\x00\x00",
                        22);
        write_raw(file, bytes(raw));
        CHECK_THROWS_AS(VpkArchive::open(file), FormatError);
    }
}

TEST_CASE("VPK integration: every archive in the real install parses") {
    auto game = game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    auto content = *game / "Vampire";
    if (!std::filesystem::exists(content)) {
        content = *game;
    }
    std::size_t archives = 0;
    for (const auto& item : std::filesystem::directory_iterator(content)) {
        if (item.path().extension() == ".vpk") {
            INFO(item.path().string());
            CHECK_NOTHROW(VpkArchive::open(item.path()));
            ++archives;
        }
    }
    CHECK(archives > 0);
}
