#include "openbl/formats/error.hpp"
#include "openbl/formats/pickle.hpp"

#include <doctest/doctest.h>

#include <span>
#include <string>

using namespace openbl::formats;
using Kind = PickleValue::Kind;

namespace {

PickleValue parse(const std::string& s) {
    return parse_pickle(std::as_bytes(std::span(s.data(), s.size())));
}

} // namespace

TEST_CASE("Pickle protocol 0") {
    const auto v = parse(R"((dp0
S'Story_State'
p1
I3
sS'flag'
p2
I01
sS'f'
p3
F1.5
sS'l'
p4
(lp5
I1
aNag1
asS'esc'
p6
S'it\'s\n\x41'
p7
s.)");
    REQUIRE(v.kind == Kind::Dict);
    CHECK(v.get("Story_State")->integer == 3);
    CHECK(v.get("flag")->kind == Kind::Bool);
    CHECK(v.get("flag")->boolean);
    CHECK(v.get("f")->real == 1.5);
    const auto* l = v.get("l");
    REQUIRE(l->items.size() == 3);
    CHECK(l->items[1].kind == Kind::None);
    CHECK(l->items[2].text == "Story_State"); // From the memo.
    CHECK(v.get("esc")->text == "it's\nA");
    CHECK(v.get("missing") == nullptr);

    // raw-unicode-escape: \uXXXX becomes UTF-8. (Escaped backslash, since a
    // \u inside a raw literal is translated by the compiler.)
    const auto u = parse("Vcaf\\u00e9\n.");
    CHECK(u.kind == Kind::Unicode);
    CHECK(u.text == "caf\xC3\xA9");
}

TEST_CASE("Pickle protocol 1") {
    const std::string s("}q\x00(U\x01" "aq\x01K\x05U\x01" "bq\x02]q\x03(J\x00\x01\x00\x00G?\xf8\x00\x00\x00\x00\x00\x00" "eu.", 40);
    const auto v = parse(s);
    REQUIRE(v.kind == Kind::Dict);
    CHECK(v.get("a")->integer == 5);
    const auto* b = v.get("b");
    REQUIRE(b->items.size() == 2);
    CHECK(b->items[0].integer == 256);
    CHECK(b->items[1].real == 1.5);
}

TEST_CASE("Pickle objects are never executed") {
    const auto v = parse("cos\nsystem\n(S'echo hi'\ntR.");
    CHECK(v.kind == Kind::Object);
    CHECK(v.text == "os.system");
    REQUIRE(v.items.size() == 1);
    CHECK(v.items[0].text == "echo hi");
}

TEST_CASE("Pickle rejects malformed data") {
    CHECK_THROWS_AS(parse("(dp0\nS'a'\n"), FormatError);   // No STOP.
    CHECK_THROWS_AS(parse("I1\nI2\n."), FormatError);       // Two values left.
    CHECK_THROWS_AS(parse("g7\n."), FormatError);           // Unknown memo key.
    CHECK_THROWS_AS(parse("S'unterminated\n."), FormatError);
    CHECK_THROWS_AS(parse("\x80\x02}."), FormatError);      // Protocol 2 is not supported.
    CHECK_THROWS_AS(parse("I99999999999999999999999\n."), FormatError);
}
