#include "openbl/formats/ain.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

using namespace openbl;
using namespace openbl::formats;

namespace {

// Two hulls, two nodes, one link; wrapped the way the game's writer does it.
constexpr std::string_view kGraph = "Version\t30\r\r\n"
                                    "NumHulls:         2\r\r\n"
                                    "UsedHullBits:     1\r\r\n"
                                    "ZoneCount:        2\r\r\n"
                                    "0 2 \r\r\n"
                                    "NumNodes:         2\r\r\n\r\r\n"
                                    "Nodes:            1.5,-2.00,3.25 90.000000 -8.87 0.10 2 0 2 \r\r\n"
                                    "1 1 4.00,5.00,6.00 0.000000 0.10 0.10 2 0 -2147483647 \r\r\n"
                                    "1 1 \r\r\n"
                                    "TotalNumLinks      1\r\r\n"
                                    "0 1 0 1 2 \r\r\n"
                                    "WCLookup:         7 9 \r\r\n";

} // namespace

TEST_CASE("AIN graph") {
    const auto g = parse_ain(kGraph);
    CHECK(g.version == 30);
    CHECK(g.zones == std::vector<std::int32_t>{0, 2});
    REQUIRE(g.nodes.size() == 2);
    CHECK(g.nodes[0].origin == std::array<float, 3>{1.5f, -2.0f, 3.25f});
    CHECK(g.nodes[0].yaw == 90.0f);
    CHECK(g.nodes[0].hull_offsets[0] == doctest::Approx(-8.87f));
    CHECK(g.nodes[0].mask_bit(1));
    CHECK_FALSE(g.nodes[0].mask_bit(0));
    CHECK(g.nodes[0].zone == 1);
    CHECK(g.nodes[1].mask[0] == 0x80000001u);
    REQUIRE(g.links.size() == 1);
    CHECK(g.links[0].dest == 1);
    CHECK(g.links[0].hull_moves == std::vector<std::int32_t>{1, 2});
    CHECK(g.editor_ids == std::vector<std::int32_t>{7, 9});
    CHECK(parse_loc("18561954\r\r\n") == 18561954);
}

TEST_CASE("AIN rejects malformed graphs") {
    std::string s(kGraph);
    SUBCASE("version") {
        s.replace(s.find("30"), 2, "31");
    }
    SUBCASE("truncated") {
        s.resize(s.find("WCLookup"));
    }
    SUBCASE("link out of range") {
        s.replace(s.find("0 1 0 1 2"), 9, "0 5 0 1 2");
    }
    SUBCASE("bad origin") {
        s.replace(s.find("1.5,-2.00"), 9, "1.5;-2.00");
    }
    CHECK_THROWS_AS(parse_ain(s), FormatError);
}

TEST_CASE("AIN integration: every AI graph in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t graphs = 0, nodes = 0, links = 0, degree_mismatch = 0, locs = 0;
    for (const auto& path : gfs.list_files()) {
        if (path.ends_with(".loc")) {
            const auto data = gfs.read(path);
            CHECK_NOTHROW(parse_loc({reinterpret_cast<const char*>(data->data()), data->size()}));
            ++locs;
        }
        if (!path.ends_with(".ain")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        AinGraph g;
        REQUIRE_NOTHROW(g = parse_ain({reinterpret_cast<const char*>(data->data()), data->size()}));
        ++graphs;
        nodes += g.nodes.size();
        links += g.links.size();
        std::vector<std::int32_t> degree(g.nodes.size());
        for (const auto& l : g.links) {
            ++degree[static_cast<std::size_t>(l.source)];
            ++degree[static_cast<std::size_t>(l.dest)];
        }
        for (std::size_t n = 0; n < g.nodes.size(); ++n) {
            degree_mismatch += degree[n] != g.nodes[n].link_count;
        }
        // Each graph should have a matching map.
        const std::string map = "maps/" + path.substr(path.rfind('/') + 1, path.size() - path.rfind('/') - 5) + ".bsp";
        CHECK(gfs.exists(map));
    }
    MESSAGE(graphs << " graphs, " << nodes << " nodes, " << links << " links, " << locs << " .loc files");
    CHECK(graphs == 100);
    CHECK(locs == 100);
    CHECK(degree_mismatch == 0);
}
