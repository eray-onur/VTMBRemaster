#pragma once

// Lip-sync data (.lip): the spoken text, word and phoneme timings, closed
// captions and playback options for one sound file. See docs/formats/lipsync.md.

#include "openbl/formats/text_diagnostic.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

struct LipPhoneme {
    char32_t code = 0;  // IPA code point, e.g. U+02A7 for "ch".
    std::string name;   // ARPAbet-style name as written ("ch", "eh"); occasionally disagrees with code.
    float start = 0, end = 0; // Seconds from the start of the sound.
    float weight = 1;
    int flags = 0;      // Last column; absent (0) in older files. Meaning unknown.
};

struct LipWord {
    std::string text;
    float start = 0, end = 0;
    std::vector<LipPhoneme> phonemes;
};

struct LipCaption {
    std::string language; // "english" in all shipped files.
    std::string text;     // UTF-8 (stored as UTF-16LE in the file).
    float start = 0, end = 0;
};

struct LipFile {
    std::string version;   // "1.0", "1.1" or "1.2".
    std::string plaintext; // Windows-1252, as written.
    std::vector<LipWord> words;
    std::vector<std::string> emphasis; // Raw lines; empty in all shipped files.
    std::vector<LipCaption> captions;
    std::vector<std::pair<std::string, std::string>> options; // e.g. {"voice_duck", "1"}, {"speaker_name", "Barabus"}.
    std::vector<TextDiagnostic> diagnostics;
};

// Never throws; unknown sections and malformed lines are skipped with a diagnostic.
LipFile parse_lip(std::string_view data);

} // namespace openbl::formats
