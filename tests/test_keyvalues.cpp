#include "openbl/formats/keyvalues.hpp"

#include <doctest/doctest.h>

using namespace openbl::formats;

TEST_CASE("KeyValues basic structure") {
    const auto doc = parse_keyvalues("// header comment\r\n"
                                     "\"Root\"\r\n"
                                     "{\r\n"
                                     "\t\"$basetexture\" \"brick\\wall\"  // trailing comment\r\n"
                                     "\t$model 1\r\n"
                                     "\t\"Proxies\" { \"Sine\" { \"sinemin\" .2 } }\r\n"
                                     "}\r\n");
    CHECK(doc.diagnostics.empty());
    REQUIRE(doc.roots.size() == 1);
    const KvNode& root = doc.roots[0];
    CHECK(root.key == "Root");
    CHECK(root.is_block);
    CHECK(root.line == 2);
    REQUIRE(root.children.size() == 3);
    CHECK(root.find("$BASETEXTURE")->value == "brick\\wall"); // No escape processing.
    CHECK(root.find("$model")->value == "1");
    CHECK(root.find("$model")->line == 5);
    const KvNode* sine = root.find("proxies")->find("sine");
    REQUIRE(sine);
    CHECK(sine->find("sinemin")->value == ".2");
    CHECK(root.find("missing") == nullptr);
}

TEST_CASE("KeyValues tokens") {
    SUBCASE("quoted strings may span lines and contain braces") {
        const auto doc = parse_keyvalues("a { \"$fogcolor\" \"{22 20 10}\" \"multi\nline\" x }");
        CHECK(doc.diagnostics.empty());
        CHECK(doc.roots[0].find("$fogcolor")->value == "{22 20 10}");
        CHECK(doc.roots[0].find("multi\nline")->value == "x");
    }
    SUBCASE("unquoted tokens stop at quotes and braces") {
        const auto doc = parse_keyvalues("a{b\"c\"}");
        CHECK(doc.diagnostics.empty());
        CHECK(doc.roots[0].find("b")->value == "c");
    }
    SUBCASE("a backslash pair is an ordinary token, not a comment") {
        const auto doc = parse_keyvalues("a { \\\\\"$selfillum\" \"1\" \"$x\" \"2\" }");
        const auto& kids = doc.roots[0].children;
        REQUIRE(kids.size() == 3);
        CHECK(kids[0].key == "\\\\");
        CHECK(kids[0].value == "$selfillum");
        CHECK(kids[1].key == "1");
        CHECK(kids[1].value == "$x");
        // "2" is left as a key with no value before the closing brace.
        CHECK(kids[2].key == "2");
        CHECK(doc.diagnostics.size() == 1);
    }
    SUBCASE("smart quotes are ordinary characters") {
        const auto doc = parse_keyvalues("a { \"$surfaceprop\" \x93metal\x94 }");
        CHECK(doc.roots[0].find("$surfaceprop")->value == "\x93metal\x94");
    }
}

TEST_CASE("KeyValues recovery") {
    SUBCASE("unclosed block is closed at end of input") {
        const auto doc = parse_keyvalues("a { b c\n d { e f }");
        CHECK(doc.roots[0].find("d")->find("e")->value == "f");
        REQUIRE(doc.diagnostics.size() == 1);
        CHECK(doc.diagnostics[0].message.find("not closed") != std::string::npos);
    }
    SUBCASE("extra closing braces are ignored") {
        const auto doc = parse_keyvalues("a { b c }\n}\n}");
        CHECK(doc.roots.size() == 1);
        CHECK(doc.diagnostics.size() == 2);
    }
    SUBCASE("block opened with the wrong brace") {
        const auto doc = parse_keyvalues("\"VertexLitGeneric\"\n}\n\"$basetexture\" \"x\"\n}");
        REQUIRE(doc.roots.size() == 2);
        CHECK_FALSE(doc.roots[0].is_block);
        CHECK(doc.roots[1].key == "$basetexture");
        CHECK(doc.diagnostics.size() == 2);
    }
    SUBCASE("unterminated quote") {
        const auto doc = parse_keyvalues("a { b \"oops");
        CHECK(doc.roots[0].find("b")->value == "oops");
        CHECK(doc.diagnostics.size() == 2);
    }
    SUBCASE("key without value at end of input") {
        const auto doc = parse_keyvalues("a { b");
        CHECK(doc.roots[0].children.size() == 1);
        CHECK(doc.diagnostics.size() == 2);
    }
    SUBCASE("deep nesting stops instead of overflowing the stack") {
        std::string deep;
        for (int i = 0; i < 10000; ++i) {
            deep += "k {";
        }
        const auto doc = parse_keyvalues(deep);
        CHECK_FALSE(doc.diagnostics.empty());
    }
    SUBCASE("empty input") {
        const auto doc = parse_keyvalues("  // nothing\n");
        CHECK(doc.roots.empty());
        CHECK(doc.diagnostics.empty());
    }
}
