#include "openbl/formats/error.hpp"
#include "openbl/formats/pickle.hpp"
#include "openbl/formats/sav.hpp"
#include "openbl/formats/save_state.hpp"
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

namespace {

// Minimal state file: tokens, one Entities block with one entity, a global
// "Save Header" struct, and the entity's data.
std::vector<std::byte> build_state() {
    const std::vector<std::string> slots = {"", "uv", "elems", "SaveRestoreBlockHeader_t", "szName", "locHeader",
                                            "ETABLE", "location", "size", "classname", "Save Header", "mapName",
                                            "CBaseEntity", "m_iHealth", "m_vecOrigin", "", ""};
    std::string table;
    for (const auto& s : slots) {
        table += s;
        table.push_back('\0');
    }
    auto token = [&](std::string_view name) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i] == name) {
                return static_cast<std::uint16_t>(i);
            }
        }
        FAIL("unknown token");
        return std::uint16_t{0};
    };
    auto rec = [&](Out& o, std::string_view name, const void* p, std::size_t n) {
        o.u16(static_cast<std::uint16_t>(n));
        o.u16(token(name));
        o.raw(p, n);
    };
    auto rec_i32 = [&](Out& o, std::string_view name, std::int32_t v) { rec(o, name, &v, 4); };

    // Body: global struct, then the entity at location = its offset.
    Out body;
    rec_i32(body, "Save Header", 1);
    char map[32] = "sp_test";
    rec(body, "mapName", map, 32);
    const auto location = static_cast<std::int32_t>(body.d.size());
    Out entity;
    rec_i32(entity, "CBaseEntity", 2);
    rec_i32(entity, "m_iHealth", 75);
    const float origin[3] = {1, 2, 3};
    rec(entity, "m_vecOrigin", origin, 12);
    body.raw(entity.d.data(), entity.d.size());

    // Header section: 3 ints, one block, then the entity table at locHeader.
    Out header;
    header.i32(0);
    header.i32(0);
    header.i32(1);
    Out inner;
    rec_i32(inner, "SaveRestoreBlockHeader_t", 2);
    char block[32] = "Entities";
    rec(inner, "szName", block, 32);
    const std::size_t loc_at = inner.d.size() + 4;
    rec_i32(inner, "locHeader", 0);
    rec_i32(header, "uv", 1);
    const std::size_t inner_at = header.d.size() + 4;
    rec(header, "elems", inner.d.data(), inner.d.size());
    const auto table_at = static_cast<std::int32_t>(header.d.size());
    std::memcpy(header.d.data() + inner_at + loc_at, &table_at, 4);
    header.i32(1);
    rec_i32(header, "ETABLE", 3);
    rec_i32(header, "location", location);
    rec_i32(header, "size", static_cast<std::int32_t>(entity.d.size()));
    char cls[16] = "player";
    rec(header, "classname", cls, 16);

    Out o;
    o.raw("VALV", 4);
    o.i32(117);
    o.i32(static_cast<std::int32_t>(table.size()));
    o.i32(static_cast<std::int32_t>(slots.size()));
    o.i32(static_cast<std::int32_t>(header.d.size()));
    o.i32(static_cast<std::int32_t>(body.d.size()));
    o.raw(table.data(), table.size());
    o.raw(header.d.data(), header.d.size());
    o.raw(body.d.data(), body.d.size());
    return o.d;
}

} // namespace

TEST_CASE("Save state files") {
    const auto data = build_state();
    const auto state = parse_save_state(data);
    REQUIRE(state.blocks.size() == 1);
    CHECK(state.blocks[0].name == "Entities");
    REQUIRE(state.globals.size() == 1);
    CHECK(state.globals[0].name == "Save Header");
    CHECK(state.globals[0].field("mapName")->as_string() == "sp_test");
    REQUIRE(state.entities.size() == 1);
    const auto& e = state.entities[0];
    CHECK(e.classname == "player");
    REQUIRE(e.data.size() == 1);
    CHECK(e.field("m_iHealth")->as_int() == 75);
    CHECK((*e.field("m_vecOrigin")->as_vec3())[2] == 3.0f);

    auto bad = data;
    SUBCASE("signature") {
        bad[0] = std::byte{'X'};
    }
    SUBCASE("sizes") {
        bad.push_back(std::byte{0});
    }
    CHECK_THROWS_AS(parse_save_state(bad), FormatError);
}

TEST_CASE("Save state integration: every map state in the user's saves parses") {
    const char* dir = std::getenv("OPENBL_SAVE_DIR");
    if (!dir || !*dir) {
        MESSAGE("OPENBL_SAVE_DIR not set; skipping");
        return;
    }
    std::size_t states = 0, entities = 0, fields = 0, pickles = 0, players = 0, trailing_entities = 0;
    std::size_t story_states = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".sav" || entry.file_size() == 0) {
            continue;
        }
        const auto save = parse_sav(test::read_raw(entry.path()));
        for (const auto& f : save.files) {
            if (!f.name.ends_with(".HL1")) {
                continue;
            }
            INFO(entry.path().string() << " " << f.name);
            SaveState state;
            REQUIRE_NOTHROW(state = parse_save_state(f.data));
            ++states;
            CHECK(!state.globals.empty());
            CHECK(state.globals.front().name == "Save Header");
            CHECK(!state.event_queue.empty());
            CHECK(!state.python_pickles.empty());
            for (const auto& p : state.python_pickles) {
                CHECK((!p.empty() && p.back() == std::byte{'.'})); // Protocol-0 pickles end with STOP.
                PickleValue value;
                REQUIRE_NOTHROW(value = parse_pickle(p)); // Values are private; only their shape is checked.
                CHECK(value.kind == PickleValue::Kind::Dict);
                story_states += value.get("Story_State") != nullptr;
            }
            pickles += state.python_pickles.size();
            for (const auto& e : state.entities) {
                if (e.classname.empty()) {
                    continue;
                }
                ++entities;
                CHECK(!e.data.empty());
                for (const auto& s : e.data) {
                    fields += s.fields.size();
                }
                trailing_entities += !e.trailing.empty();
                if (e.classname == "player") {
                    ++players;
                    CHECK(e.trailing.size() == 29);
                    CHECK(e.field("m_iVAttributesBase[ v_attribute_clan ]"));
                    CHECK(e.field("m_iHealth"));
                }
            }
        }
    }
    MESSAGE(states << " map states, " << entities << " entities, " << fields << " saved fields, " << pickles
                   << " Python pickles, " << players << " players, " << trailing_entities
                   << " entities with trailing class data, " << story_states << " pickles with Story_State");
    CHECK(players >= 1);
    CHECK(story_states >= 1); // The global-variables pickle of each map state.
}
