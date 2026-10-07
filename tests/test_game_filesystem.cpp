#include "openbl/formats/error.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

using namespace openbl;
using namespace openbl::test;
using formats::VpkKind;
using formats::VpkWriteEntry;
using Kind = vfs::GameFileSystem::Source::Kind;

TEST_CASE("GameFileSystem resolution order") {
    TempDir tmp;
    const auto content = tmp.path() / "Vampire";
    std::filesystem::create_directories(content);

    const std::vector<VpkWriteEntry> base = {
        {"vdata/system/strings.txt", bytes("base")},
        {"dlg/isaac.dlg", bytes("base-dlg")},
        {"sound/a.wav", bytes("wav")},
    };
    const std::vector<VpkWriteEntry> patch = {
        {"vdata/system/strings.txt", bytes("patched")},
        {"dlg/isaac.dlg", bytes("patched-dlg")},
    };
    const std::vector<VpkWriteEntry> dirs = {{"maps", {}}, {"python/somewhere", {}}};
    formats::write_vpk(content / "pack000.vpk", base);
    formats::write_vpk(content / "pack103.vpk", patch);
    formats::write_vpk(content / "pack010.vpk", dirs, VpkKind::DirectoryIndex);
    write_raw(content / "DLG" / "Isaac.dlg", bytes("loose-dlg"));

    // Install root resolves to its Vampire/ content directory.
    const vfs::GameFileSystem gfs(tmp.path());
    CHECK(gfs.content_dir() == content);

    CHECK(*gfs.read("sound/a.wav") == bytes("wav"));
    CHECK(*gfs.read("vdata/system/strings.txt") == bytes("patched"));
    CHECK(gfs.source_of("vdata/system/strings.txt")->file.filename() == "pack103.vpk");

    CHECK(*gfs.read("dlg/isaac.dlg") == bytes("loose-dlg"));
    CHECK(gfs.source_of("dlg/isaac.dlg")->kind == Kind::Loose);

    CHECK_FALSE(gfs.exists("missing.txt"));
    CHECK_FALSE(gfs.read("missing.txt").has_value());
    CHECK(gfs.shadowed_count() == 3);

    CHECK(gfs.is_directory("python"));
    CHECK(gfs.is_directory("python/somewhere"));
    CHECK(gfs.is_directory("vdata/system"));
    CHECK_FALSE(gfs.is_directory("sound/a.wav"));

    CHECK(gfs.list_files().size() == 3);
}

TEST_CASE("GameFileSystem integration: real install mounts") {
    auto game = game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    CHECK(gfs.list_files().size() > 60000);
    CHECK(gfs.is_directory("vdata/system"));
    CHECK(gfs.exists("vdata/system/strings.txt"));
    CHECK(gfs.read("vdata/system/strings.txt").has_value());
}

TEST_CASE("GameFileSystem mounts mod folders over the game") {
    TempDir tmp;
    const auto root = tmp.path();
    std::filesystem::create_directories(root / "Vampire");
    const std::vector<VpkWriteEntry> base = {{"scripts/a.txt", bytes("base")}, {"scripts/b.txt", bytes("base b")}};
    formats::write_vpk(root / "Vampire" / "pack000.vpk", base);
    write_raw(root / "Vampire" / "scripts" / "c.txt", bytes("base loose"));
    write_raw(root / "Mod" / "scripts" / "a.txt", bytes("mod"));
    write_raw(root / "Mod" / "scripts" / "c.txt", bytes("mod loose"));
    write_raw(root / "Mod" / "new.txt", bytes("added"));
    write_raw(root / "Mod2" / "new.txt", bytes("added later"));

    const vfs::GameFileSystem gfs(root, {"Mod", "Mod2"});
    REQUIRE(gfs.mod_dirs().size() == 2);
    auto text = [&](std::string_view path) {
        const auto d = gfs.read(path);
        return d ? std::string(reinterpret_cast<const char*>(d->data()), d->size()) : std::string("<missing>");
    };
    CHECK(text("scripts/a.txt") == "mod");
    CHECK(text("scripts/b.txt") == "base b");
    CHECK(text("scripts/c.txt") == "mod loose");
    CHECK(text("new.txt") == "added later");
    CHECK_THROWS_AS(vfs::GameFileSystem(root, {"Missing"}), formats::FormatError);
}
