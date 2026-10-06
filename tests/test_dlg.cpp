#include "openbl/formats/dlg.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace openbl;
using namespace openbl::formats;

namespace {

std::string_view as_text(const std::vector<std::byte>& data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

} // namespace

TEST_CASE("DLG records") {
    const auto dlg = parse_dlg(
        "{\t1\t}{\t[Angry]Get out!\t}{\t[Angry]Get out!\t}{\t#\t}{\tnpc.SetDisposition(\"Anger\", 3)\t}{\t\t}"
        "{\t}{\t}{\t}{\t}{\t}{\t}{\tOut, out, damned spot!\t}\r\n"
        "{\t2\t}{\tMake me.\t}{\tMake me, please.\t}{\t11\t}{\tG.Brave == 1\t}{\tG.Fight = 1\t}"
        "{\t}{\t}{\t}{\t}{\t}{\t}{\t\t}\r\n"
        "{\t3\t}{\t\t}{\t\t}{\t\t}{\t\t}{\t\t}{\t}{\t}{\t}{\t}{\t}{\t}\r\n");
    CHECK(dlg.diagnostics.empty());
    REQUIRE(dlg.lines.size() == 3);

    const auto& npc = dlg.lines[0];
    CHECK(npc.is_npc());
    CHECK(npc.number() == 1);
    CHECK(npc.male_text == "[Angry]Get out!");
    CHECK(npc.condition == "npc.SetDisposition(\"Anger\", 3)");
    CHECK(npc.malkavian_text == "Out, out, damned spot!");
    CHECK(npc.source_line == 1);

    const auto& reply = dlg.lines[1];
    CHECK_FALSE(reply.is_npc());
    CHECK(reply.female_text == "Make me, please.");
    CHECK(reply.next_number() == 11);
    CHECK(reply.condition == "G.Brave == 1");
    CHECK(reply.action == "G.Fight = 1");
    CHECK(reply.source_line == 2);

    // 12 fields: Malkavian column missing.
    CHECK(dlg.lines[2].is_placeholder());
    CHECK_FALSE(dlg.lines[2].next_number());
}

TEST_CASE("DLG spreadsheet quoting and damage") {
    SUBCASE("quoted field with doubled quotes and a line break") {
        const auto dlg = parse_dlg("{1}{Hi}{Hi}{0}{}{\t\"\r\nnpc.Say(\"\"hello\"\")\"\t}{}{}{}{}{}{}{}\r\n");
        REQUIRE(dlg.lines.size() == 1);
        CHECK(dlg.lines[0].action == "npc.Say(\"hello\")");
    }
    SUBCASE("ordinary quoted text is kept") {
        const auto dlg = parse_dlg("{1}{\"Yes.\"}{\"Yes.\"}{#}{}{}{}{}{}{}{}{}{}\r\n");
        CHECK(dlg.lines[0].male_text == "\"Yes.\"");
    }
    SUBCASE("non-numeric next") {
        const auto dlg = parse_dlg("{1}{Hm}{Hm}{RandomLine([5, 7])}{}{}{}{}{}{}{}{}{}\r\n");
        CHECK(dlg.lines[0].next == "RandomLine([5, 7])");
        CHECK_FALSE(dlg.lines[0].next_number());
    }
    SUBCASE("missing closing braces") {
        // Field closed by the next '{' on the same line; record closed by a line break.
        const auto dlg = parse_dlg("{1\t{a}{a}{#}{}{}{}{}{}{}{}{}{\t\r\n"
                                   "{2}{b}{b}{0}{}{}{}{}{}{}{}{}{}\r\n");
        REQUIRE(dlg.lines.size() == 2);
        CHECK(dlg.lines[0].index == "1");
        CHECK(dlg.lines[0].male_text == "a");
        CHECK(dlg.lines[1].index == "2");
        CHECK(dlg.diagnostics.size() == 2);
    }
    SUBCASE("stray and truncated records") {
        const auto dlg = parse_dlg("{1}{a}{a}{#}{}{}{}{}{}{}{}{}{}\r\njunk\r\n{\t2");
        CHECK(dlg.lines.size() == 1);
        CHECK(dlg.diagnostics.size() == 2);
        CHECK(dlg.diagnostics[0].line == 2);
    }
}

TEST_CASE("DLG integration: every dialogue in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, lines = 0, npc = 0, replies = 0, links = 0, dangling = 0, diagnostics = 0;
    std::map<std::string, std::size_t> damaged;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".dlg")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        const auto dlg = parse_dlg(as_text(*data));
        ++files;
        lines += dlg.lines.size();
        CHECK(dlg.lines.size() > 0);
        std::map<std::string, const DlgLine*> by_index;
        for (const auto& l : dlg.lines) {
            by_index[l.index] = &l;
        }
        for (const auto& l : dlg.lines) {
            npc += l.is_npc();
            if (!l.is_npc() && !l.is_placeholder()) {
                ++replies;
            }
            if (const auto n = l.next_number(); n && *n > 0) {
                ++links;
                const auto it = by_index.find(l.next);
                dangling += it == by_index.end() || !it->second->is_npc();
            }
        }
        for (const auto& d : dlg.diagnostics) {
            if (d.message.find("does not match") == std::string::npos) {
                ++damaged[path];
            }
        }
        diagnostics += dlg.diagnostics.size();
    }
    MESSAGE(files << " dialogues, " << lines << " records, " << npc << " NPC lines, " << replies << " player replies, "
                  << links << " links (" << dangling << " dangling), " << diagnostics << " diagnostics");
    for (const auto& [path, n] : damaged) {
        MESSAGE("damaged: " << path << " (" << n << ")");
    }
    CHECK(files == 138);
    // Steam release: arthur.dlg and skelter.dlg each miss one '}'.
    CHECK(damaged.size() == 2);
    CHECK(dangling <= 3);
}
