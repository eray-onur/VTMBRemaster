#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/localization.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>
#include <span>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("KeyValues integration: text data files in the real install") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    struct Group {
        std::size_t files = 0, clean = 0, diagnostics = 0;
        std::map<std::string, std::string> roots; // root key -> example file
        std::vector<std::string> failing;
    };
    std::map<std::string, Group> groups;
    for (const auto& path : gfs.list_files()) {
        const bool txt = path.ends_with(".txt");
        std::string group;
        if (txt && path.starts_with("vdata/")) {
            group = path.substr(0, path.find('/', 6));
        } else if (txt && (path.starts_with("particles/") || path.starts_with("sound/schemes/") ||
                           path.starts_with("scripts/") || path.starts_with("resource/"))) {
            group = path.substr(0, path.find('/'));
        } else if (path.ends_with(".res")) {
            group = "*.res";
        } else if (path.starts_with("scripts/dialog_") || path.starts_with("scripts/kb_") ||
                   path == "scripts/liblist.gam") {
            group = "scripts (UI dialogs, key bindings, game info)";
        } else {
            continue;
        }
        const auto data = gfs.read(path);
        const KvOptions options{.escaped_quotes = path.starts_with("vdata/")};
        const auto doc = parse_keyvalues({reinterpret_cast<const char*>(data->data()), data->size()}, options);
        auto& g = groups[group];
        ++g.files;
        g.clean += doc.diagnostics.empty();
        g.diagnostics += doc.diagnostics.size();
        if (!doc.diagnostics.empty() && g.failing.size() < 12) {
            g.failing.push_back(path + ": line " + std::to_string(doc.diagnostics[0].line) + " " +
                                doc.diagnostics[0].message);
        }
        for (const auto& r : doc.roots) {
            g.roots.emplace(r.key, path);
        }
    }
    for (const auto& [name, g] : groups) {
        std::string roots;
        for (const auto& [k, example] : g.roots) {
            if (roots.size() < 200) {
                roots += k + " ";
            }
        }
        MESSAGE(name << ": " << g.clean << "/" << g.files << " clean, " << g.diagnostics << " diagnostics; roots: "
                     << roots);
        for (const auto& f : g.failing) {
            MESSAGE("  " << f);
        }
    }
}

TEST_CASE("Localisation tokens") {
    std::string u16 = "\xFF\xFE";
    for (char c : std::string_view("\"lang\"\r\n{\r\n\"Language\" \"English\"\r\n\"Tokens\"\r\n{\r\n"
                                   "\"A\"\t\"One\nTwo\"\r\n\"B\" \"x\"\r\n\"B\" \"y\"\r\n}\r\n}\r\n")) {
        u16 += c;
        u16 += '\0';
    }
    const auto loc = parse_localization(std::as_bytes(std::span(u16.data(), u16.size())));
    CHECK(loc.language == "English");
    REQUIRE(loc.tokens.size() == 2);
    CHECK(loc.tokens.at("A") == "One\nTwo");
    CHECK(loc.tokens.at("B") == "x");
    CHECK(loc.diagnostics.size() == 1); // Duplicate B.

    const std::string cp1252 = "\"lang\" { \"Tokens\" { \"Q\" \"\x93quoted\x94\" } }";
    const auto legacy = parse_localization(std::as_bytes(std::span(cp1252.data(), cp1252.size())));
    CHECK(legacy.tokens.at("Q") == "\xE2\x80\x9Cquoted\xE2\x80\x9D");
}

TEST_CASE("Localisation integration: the game's UI strings load") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    const auto data = gfs.read("resource/gameui_english.txt");
    REQUIRE(data);
    const auto loc = parse_localization(*data);
    MESSAGE(loc.tokens.size() << " UI tokens, " << loc.diagnostics.size() << " diagnostics");
    CHECK(loc.language == "English");
    CHECK(loc.tokens.size() == 192);
    CHECK(loc.diagnostics.size() == 1); // GameUI_Advanced is defined twice.
    CHECK(loc.tokens.at("GameUI_MouseLook") == "Mouse look");
}
