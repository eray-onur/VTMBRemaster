#include "openbl/formats/error.hpp"
#include "openbl/formats/expressions.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>

using namespace openbl;
using namespace openbl::formats;

namespace {

void put_i32(std::vector<std::byte>& out, std::size_t at, std::int32_t v) {
    std::memcpy(out.data() + at, &v, 4);
}

// Two keys, two settings ("Joy" sets key 1; "r" with a code point sets none).
std::vector<std::byte> build_vfe(std::size_t name_field) {
    const std::size_t h = 8 + name_field;
    const std::size_t settings = h + 36;
    const std::size_t values = settings + 48;
    const std::size_t key_table = values + 12;
    const std::size_t strings = key_table + 8;
    std::vector<std::byte> out(strings + 32);
    std::memcpy(out.data(), "EFV\0", 4);
    const std::size_t joy = strings, r = strings + 4, brow = strings + 6, jaw = strings + 11;
    std::memcpy(out.data() + joy, "Joy\0r\0brow\0jaw\0", 15);
    const std::int32_t header[9] = {static_cast<std::int32_t>(out.size()), 2, static_cast<std::int32_t>(settings), 0, 0, 0, 2,
                                    static_cast<std::int32_t>(key_table), 0};
    std::memcpy(out.data() + h, header, sizeof header);
    const std::int32_t rec0[6] = {static_cast<std::int32_t>(joy - settings), 0, 1, '_', 0,
                                  static_cast<std::int32_t>(values - settings)};
    const std::int32_t rec1[6] = {static_cast<std::int32_t>(r - settings - 24), 0, 0, 0x0279, 0, 0};
    std::memcpy(out.data() + settings, rec0, sizeof rec0);
    std::memcpy(out.data() + settings + 24, rec1, sizeof rec1);
    put_i32(out, values, 1);
    const float vw[2] = {0.5f, 0.75f};
    std::memcpy(out.data() + values + 4, vw, 8);
    put_i32(out, key_table, static_cast<std::int32_t>(brow));
    put_i32(out, key_table + 4, static_cast<std::int32_t>(jaw));
    return out;
}

} // namespace

TEST_CASE("Expression tables") {
    const auto set = parse_expression_table("$keys brow jaw\r\n$hasweighting\r\n"
                                            "\"Joy\" \"_\" 0.000 0.000 0.500 0.750 \"Happy\"\r\n"
                                            "\"r\" \"0x0279\" 1 1 0 1 \"Approximant\"\r\n"
                                            "\"bad\" \"_\" 1\r\n");
    CHECK(set.keys == std::vector<std::string>{"brow", "jaw"});
    REQUIRE(set.expressions.size() == 2);
    CHECK(set.expressions[0].code == U'_');
    CHECK(set.expressions[0].values[1] == 0.5f);
    CHECK(set.expressions[0].weights[1] == 0.75f);
    CHECK(set.expressions[0].description == "Happy");
    CHECK(set.expressions[1].code == U'ɹ');
    CHECK(set.diagnostics.size() == 1);
}

TEST_CASE("VFE compiled sets") {
    for (const std::size_t name_field : {std::size_t{128}, std::size_t{64}}) {
        CAPTURE(name_field);
        const auto set = parse_vfe(build_vfe(name_field));
        CHECK(set.keys == std::vector<std::string>{"brow", "jaw"});
        REQUIRE(set.expressions.size() == 2);
        CHECK(set.expressions[0].name == "Joy");
        CHECK(set.expressions[0].values == std::vector<float>{0.0f, 0.5f});
        CHECK(set.expressions[0].weights == std::vector<float>{0.0f, 0.75f});
        CHECK(set.expressions[1].code == U'ɹ');
    }
    auto bad = build_vfe(128);
    SUBCASE("signature") {
        bad[0] = std::byte{'X'};
    }
    SUBCASE("length") {
        bad.push_back(std::byte{0});
    }
    SUBCASE("key index") {
        bad[8 + 128 + 36 + 48] = std::byte{7};
    }
    CHECK_THROWS_AS(parse_vfe(bad), FormatError);
}

TEST_CASE("Expressions integration: compiled sets match their source tables") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t tables = 0, compiled = 0, compared = 0, mismatched = 0, rows = 0;
    std::vector<std::string> mismatches;
    for (const auto& path : gfs.list_files()) {
        if (!path.starts_with("expressions/")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        if (path.ends_with(".txt")) {
            const auto set = parse_expression_table({reinterpret_cast<const char*>(data->data()), data->size()});
            CHECK(set.diagnostics.empty());
            ++tables;
            rows += set.expressions.size();
            continue;
        }
        if (!path.ends_with(".vfe")) {
            continue;
        }
        ExpressionSet vfe;
        REQUIRE_NOTHROW(vfe = parse_vfe(*data));
        ++compiled;
        const auto txt_data = gfs.read(path.substr(0, path.size() - 4) + ".txt");
        if (!txt_data) {
            continue;
        }
        const auto txt = parse_expression_table({reinterpret_cast<const char*>(txt_data->data()), txt_data->size()});
        ++compared;
        bool same = txt.keys == vfe.keys && txt.expressions.size() == vfe.expressions.size();
        for (std::size_t e = 0; same && e < txt.expressions.size(); ++e) {
            const auto& a = txt.expressions[e];
            const auto& b = vfe.expressions[e];
            same = a.name == b.name && a.code == b.code;
            for (std::size_t k = 0; same && k < a.values.size(); ++k) {
                // Tables round to three decimals; keys whose value is zero are not compiled.
                constexpr float kRounding = 0.0005f + 1e-6f;
                same = std::abs(a.values[k] - b.values[k]) <= kRounding &&
                       (a.values[k] == 0.0f || std::abs(a.weights[k] - b.weights[k]) <= kRounding);
            }
        }
        if (!same) {
            ++mismatched;
            mismatches.push_back(path);
        }
    }
    MESSAGE(tables << " source tables (" << rows << " rows), " << compiled << " compiled sets, " << compared
                   << " compared, " << mismatched << " out of date");
    for (const auto& m : mismatches) {
        MESSAGE("  out of date: " << m);
    }
    CHECK(tables == 249);
    CHECK(compiled == 249);
    // Steam release: 7 tables were edited after their last compile.
    CHECK(mismatched <= 7);
}
