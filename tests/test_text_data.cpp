#include "openbl/formats/console_script.hpp"
#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/localization.hpp"
#include "openbl/formats/script_text.hpp"
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

TEST_CASE("Script text formats") {
    const auto xp = parse_experience_table("> comment\nQuest1 | Find the thing | 50\nbad line\n");
    REQUIRE(xp.rows.size() == 1);
    CHECK(xp.rows[0].experience == 50);
    CHECK(xp.rows[0].description == "Find the thing");
    CHECK(xp.diagnostics.size() == 1);

    const auto credits = parse_credits("# comment\n@MainMenu\n$175 10 5 255\nVampire\n");
    REQUIRE(credits.size() == 3);
    CHECK(credits[0].kind == CreditsLine::Kind::Font);
    CHECK(credits[0].value == "MainMenu");
    CHECK(credits[2].value == "Vampire");

    const auto sentences = parse_sentences("// c\nSPI_DIES0 character/a.wav {Len 1.50}\n");
    REQUIRE(sentences.sentences.size() == 1);
    CHECK(sentences.sentences[0].sound == "character/a.wav");
    CHECK(sentences.sentences[0].options == "Len 1.50");

    const auto titles = parse_titles("$fadein 1.5\nCR1\n{\nHello\n}\n$fadein 2\nCR2\n{\nBye\n}\n");
    REQUIRE(titles.titles.size() == 2);
    CHECK(titles.titles[0].text == "Hello");
    CHECK(titles.titles[1].settings[0].second == "2");

    const auto dsp = parse_dsp_presets("{ 0 LINEAR 0.2 0.8 { RVA 80.0 30.0 } { 0 0 } }\n");
    REQUIRE(dsp.presets.size() == 1);
    CHECK(dsp.presets[0].header[1] == "LINEAR");
    REQUIRE(dsp.presets[0].processors.size() == 2);
    CHECK(dsp.presets[0].processors[0].tokens[0] == "RVA");
}

TEST_CASE("Script text integration: the shipped files parse") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    auto text = [&](std::string_view path) {
        const auto d = gfs.read(path);
        REQUIRE(d);
        return std::string(reinterpret_cast<const char*>(d->data()), d->size());
    };
    const auto xp = parse_experience_table(text("vdata/system/experience_table.txt"));
    const auto credits = parse_credits(text("vdata/system/credits.txt"));
    const auto sentences = parse_sentences(text("scripts/sentences.txt"));
    const auto titles = parse_titles(text("scripts/titles.txt"));
    const auto dsp = parse_dsp_presets(text("scripts/dsp_presets.txt"));
    std::size_t missing_sounds = 0;
    for (const auto& s : sentences.sentences) {
        const std::string wav = "sound/" + s.sound;
        missing_sounds += !gfs.exists(wav) && !gfs.exists(wav.substr(0, wav.size() - 4) + ".mp3");
    }
    MESSAGE(xp.rows.size() << " experience rows, " << credits.size() << " credit lines, " << sentences.sentences.size()
                           << " sentences (" << missing_sounds << " missing sounds), " << titles.titles.size()
                           << " titles, " << dsp.presets.size() << " DSP presets");
    CHECK(xp.diagnostics.empty());
    CHECK(sentences.diagnostics.empty());
    CHECK(titles.diagnostics.empty());
    CHECK(dsp.diagnostics.size() == 1); // A column header at line 765 lacks its "//".
    CHECK(xp.rows.size() == 167);
}

TEST_CASE("Console scripts") {
    const auto s = parse_console_script(R"(// comment
bind "]" "invprev"
sv_cheats 1; noclip // fly
bind x "open
)");
    REQUIRE(s.commands.size() == 4);
    CHECK(s.commands[0].tokens == std::vector<std::string>{"bind", "]", "invprev"});
    CHECK(s.commands[1].tokens == std::vector<std::string>{"sv_cheats", "1"});
    CHECK(s.commands[2].tokens == std::vector<std::string>{"noclip"});
    CHECK(s.commands[2].line == 3);
    CHECK(s.commands[3].tokens.back() == "open");
    CHECK(s.diagnostics.size() == 1); // The unterminated quote.

    const auto settings = parse_settings_script(R"(VERSION 1.0
DESCRIPTION SERVER_OPTIONS
{
    "mp_fraglimit" { "Frag Limit" { NUMBER 0.000000 -1.000000 } { "0.000000" } }
    "mp_falldamage" { "Falling Damage" { LIST "Normal" "0" "Realistic" "1" } { "0.000000" } }
    "mp_teamplay" { "Teamplay" { BOOL } { "0" } }
})");
    CHECK(settings.diagnostics.empty());
    CHECK(settings.version == "1.0");
    REQUIRE(settings.options.size() == 3);
    CHECK(settings.options[0].info == std::vector<std::string>{"0.000000", "-1.000000"});
    REQUIRE(settings.options[1].list.size() == 2);
    CHECK(settings.options[1].list[1].first == "Realistic");
    CHECK(settings.options[2].type == "BOOL");
    CHECK(settings.options[2].default_value == "0");
}

TEST_CASE("Console scripts integration: the shipped configs parse") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    auto text = [&](std::string_view path) {
        const auto d = gfs.read(path);
        REQUIRE(d);
        return std::string(reinterpret_cast<const char*>(d->data()), d->size());
    };
    std::size_t commands = 0, diagnostics = 0;
    std::map<std::string, std::size_t> per_file;
    for (const char* f : {"cfg/default.cfg", "cfg/skill1.cfg", "cfg/language.cfg", "cfg/valve.rc"}) {
        const auto s = parse_console_script(text(f));
        per_file[f] = s.commands.size();
        commands += s.commands.size();
        diagnostics += s.diagnostics.size();
    }
    const auto rc = parse_console_script(text("cfg/valve.rc"));
    std::vector<std::string> execs;
    for (const auto& c : rc.commands) {
        if (c.tokens[0] == "exec" && c.tokens.size() > 1) {
            execs.push_back(c.tokens[1]);
        }
    }
    const auto settings = parse_settings_script(text("scripts/settings.scr"));
    MESSAGE(commands << " commands (default.cfg " << per_file["cfg/default.cfg"] << ", skill1.cfg "
                     << per_file["cfg/skill1.cfg"] << "), " << diagnostics << " diagnostics; settings.scr: "
                     << settings.options.size() << " options");
    CHECK(diagnostics == 0);
    CHECK(execs == std::vector<std::string>{"language.cfg", "joystick.cfg", "autoexec.cfg"});
    CHECK(settings.diagnostics.empty());
    CHECK(settings.options.size() == 12);
}
