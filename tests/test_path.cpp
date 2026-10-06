#include "openbl/formats/path.hpp"

#include <doctest/doctest.h>

using openbl::formats::is_safe_relative_path;
using openbl::formats::normalize_game_path;

TEST_CASE("normalize_game_path") {
    CHECK(normalize_game_path("Sound\\Surfaces\\Scrape1.WAV") == "sound/surfaces/scrape1.wav");
    CHECK(normalize_game_path("./dlg//main characters/regent.dlg") == "dlg/main characters/regent.dlg");
    CHECK(normalize_game_path("/materials/") == "materials");
    CHECK(normalize_game_path("") == "");
}

TEST_CASE("is_safe_relative_path") {
    CHECK(is_safe_relative_path("dlg/hollywood/isaac.dlg"));
    CHECK(is_safe_relative_path("a/..b/c"));
    CHECK_FALSE(is_safe_relative_path(""));
    CHECK_FALSE(is_safe_relative_path("/etc/passwd"));
    CHECK_FALSE(is_safe_relative_path("c:/windows"));
    CHECK_FALSE(is_safe_relative_path("../escape"));
    CHECK_FALSE(is_safe_relative_path("a/../../escape"));
    CHECK_FALSE(is_safe_relative_path("a/.."));
}
