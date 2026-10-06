#include "openbl/formats/error.hpp"
#include "openbl/formats/vmt.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("VMT parse") {
    const auto mat = parse_vmt("\"VertexLitGeneric\"\r\n"
                               "{\r\n"
                               "\t\"$baseTexture\" \"models\\Scenery\\misc\\fwheel\\whllghts\"\r\n"
                               "\t\"$surfaceprop\" \"concrete\"\r\n"
                               "\t\"$surfaceprop\" \"brick\"\r\n"
                               "\t\"$envmapmask\" \"/models/scenery/ref.vtf\"\r\n"
                               "\t\"Proxies\" { \"AnimatedTexture\" { \"animatedTextureFrameRate\" \"10\" } }\r\n"
                               "\t\"LightmappedGeneric_DX6\" { \"$basetexture\" \"old\" }\r\n"
                               "}\r\n");
    CHECK(mat.shader == "vertexlitgeneric");
    CHECK(mat.param("$BaseTexture") == "models\\Scenery\\misc\\fwheel\\whllghts");
    CHECK(mat.param("$surfaceprop") == "brick");
    CHECK_FALSE(mat.param("$envmap"));
    CHECK(mat.texture_path("$basetexture") == "materials/models/scenery/misc/fwheel/whllghts");
    CHECK(mat.texture_path("$envmapmask") == "materials/models/scenery/ref");
    REQUIRE(mat.proxies.size() == 1);
    CHECK(mat.proxies[0].key == "AnimatedTexture");
    REQUIRE(mat.sub_blocks.size() == 1);
    CHECK(mat.sub_blocks[0].key == "LightmappedGeneric_DX6");
    CHECK(mat.diagnostics.empty());
}

TEST_CASE("VMT patch materials") {
    const auto base = parse_vmt("\"LightmappedGeneric\" { \"$basetexture\" \"glass/glass01\" \"$envmap\" \"env_cubemap\" }");
    const auto patch = parse_vmt("\"patch\" { \"include\" \"GLASS/GLASS01\" "
                                 "\"insert\" { \"$envmap\" \"maps/x/c1_2_3\" \"$translucent\" \"1\" } }");
    CHECK_FALSE(is_vmt_patch(base));
    REQUIRE(is_vmt_patch(patch));
    CHECK(vmt_patch_include(patch) == "materials/glass/glass01.vmt");
    const auto merged = apply_vmt_patch(base, patch);
    CHECK(merged.shader == "lightmappedgeneric");
    CHECK(merged.param("$basetexture") == "glass/glass01");
    CHECK(merged.param("$envmap") == "maps/x/c1_2_3");
    CHECK(merged.param("$translucent") == "1");
    CHECK_THROWS_AS(vmt_patch_include(parse_vmt("\"patch\" { }")), FormatError);
}

TEST_CASE("VMT without a shader block") {
    CHECK_THROWS_AS(parse_vmt(""), FormatError);
    CHECK_THROWS_AS(parse_vmt("\"VertexLitGeneric\"\n}\n\"$basetexture\" \"x\"\n}"), FormatError);
}

TEST_CASE("VMT keeps diagnostics from recovered errors") {
    const auto mat = parse_vmt("\"UnlitGeneric\" { \"$basetexture\" \"a\"");
    CHECK(mat.param("$basetexture") == "a");
    CHECK(mat.diagnostics.size() == 1);
}

TEST_CASE("VMT integration: every material in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t materials = 0, failed = 0, with_diagnostics = 0, base_textures = 0, missing_textures = 0;
    std::map<std::string, std::size_t> shaders;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".vmt")) {
            continue;
        }
        ++materials;
        const auto data = gfs.read(path);
        REQUIRE(data);
        const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
        try {
            const auto mat = parse_vmt(text);
            ++shaders[mat.shader];
            with_diagnostics += !mat.diagnostics.empty();
            if (auto tex = mat.texture_path("$basetexture")) {
                ++base_textures;
                missing_textures += !gfs.exists(*tex + ".tth") && !gfs.exists(*tex + ".vtf");
            }
        } catch (const FormatError&) {
            ++failed;
        }
    }
    MESSAGE(materials << " materials, " << failed << " without a shader block, " << with_diagnostics
                      << " with diagnostics, " << missing_textures << " of " << base_textures
                      << " $basetexture references missing, " << shaders.size() << " shaders");
    CHECK(materials > 11000);
    CHECK(failed <= 1);         // materials/models/gibs/glass-gib.vmt
    CHECK(shaders["vertexlitgeneric"] > 5000);
}
