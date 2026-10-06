#include "openbl/formats/lip.hpp"
#include "openbl/formats/text_encoding.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace openbl;
using namespace openbl::formats;

namespace {

std::string utf16(std::string_view ascii_with_newline) {
    std::string out;
    for (char c : ascii_with_newline) {
        out += c;
        out += '\0';
    }
    return out;
}

} // namespace

TEST_CASE("Text encodings") {
    const std::string u16 = std::string("H\0i\0 \0\x19\x20", 8) + std::string("\x3d\xd8\x00\xde", 4); // "Hi ’" + U+1F600
    CHECK(utf16le_to_utf8(std::as_bytes(std::span(u16.data(), u16.size()))) == "Hi \xE2\x80\x99\xF0\x9F\x98\x80");
    const std::string lone = std::string("\x00\xd8", 2);
    CHECK(utf16le_to_utf8(std::as_bytes(std::span(lone.data(), lone.size()))) == "\xEF\xBF\xBD");
    CHECK(cp1252_to_utf8("caf\xe9 \x92") == "caf\xC3\xA9 \xE2\x80\x99");
}

TEST_CASE("LIP sections") {
    // The caption text contains a newline byte (0x0a as the low byte of U+010A)
    // to prove it is read by byte count, not by line.
    const std::string caption = utf16("\"Hi!\"") + std::string("\x0a\x01", 2);
    const std::string text = "VERSION 1.2\r\nPLAINTEXT\r\n{\r\nHi! How are you?\r\n}\r\nWORDS\r\n{\r\n"
                             "WORD Hi 0.064 0.272\r\n{\r\n104 hh 0.064 0.144 1.000 0\r\n618 ih 0.144 0.272 1.000 1\r\n}\r\n"
                             "WORD how 0.300 0.400\r\n{\r\n104 hh 0.300 0.400 1.000\r\n}\r\n"
                             "}\r\nEMPHASIS\r\n{\r\n}\r\nCLOSECAPTION\r\n{\r\nenglish\r\n{\r\nPHRASE unicode " +
                             std::to_string(caption.size()) + " " + caption + " 0.080 2.112\r\n}\r\n}\r\n" +
                             "OPTIONS\r\n{\r\nvoice_duck 1\r\nspeaker_name Tin Can Bill\r\n}\r\n";
    const auto lip = parse_lip(text);
    CHECK(lip.diagnostics.empty());
    CHECK(lip.version == "1.2");
    CHECK(lip.plaintext == "Hi! How are you?");
    REQUIRE(lip.words.size() == 2);
    CHECK(lip.words[0].text == "Hi");
    REQUIRE(lip.words[0].phonemes.size() == 2);
    CHECK(lip.words[0].phonemes[1].code == U'ɪ'); // 618 = U+026A, IPA "ɪ"
    CHECK(lip.words[0].phonemes[1].name == "ih");
    CHECK(lip.words[0].phonemes[1].flags == 1);
    CHECK(lip.words[1].phonemes[0].flags == 0); // Five-column form.
    REQUIRE(lip.captions.size() == 1);
    CHECK(lip.captions[0].language == "english");
    CHECK(lip.captions[0].text == "\"Hi!\"\xC4\x8A");
    CHECK(lip.captions[0].end == doctest::Approx(2.112f));
    REQUIRE(lip.options.size() == 2);
    CHECK(lip.options[1] == std::pair<std::string, std::string>("speaker_name", "Tin Can Bill"));
}

TEST_CASE("LIP damage is reported, not fatal") {
    const auto lip = parse_lip("VERSION 1.2\nWORDS\n{\nWORD a 0 x\n{\n}\nWORD b 0 1\n{\n1 2 3\n}\n}\nBOGUS\n{\nx\n}\n");
    CHECK(lip.words.size() == 1);
    CHECK(lip.diagnostics.size() == 3);
}

TEST_CASE("LIP integration: every lip-sync file in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, words = 0, phonemes = 0, captions = 0, diagnostics = 0, backwards = 0;
    std::map<std::string, std::size_t> versions;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".lip")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        const auto lip = parse_lip({reinterpret_cast<const char*>(data->data()), data->size()});
        ++files;
        ++versions[lip.version];
        CHECK(lip.diagnostics.empty());
        diagnostics += lip.diagnostics.size();
        words += lip.words.size();
        captions += lip.captions.size();
        for (const auto& w : lip.words) {
            phonemes += w.phonemes.size();
            backwards += w.end < w.start;
        }
    }
    MESSAGE(files << " lip files (" << versions["1.0"] << "/" << versions["1.1"] << "/" << versions["1.2"]
                  << " v1.0/1.1/1.2), " << words << " words, " << phonemes << " phonemes, " << captions
                  << " captions, " << backwards << " words ending before they start");
    CHECK(files == 5441);
    CHECK(captions == files);
    CHECK(diagnostics == 0);
}
