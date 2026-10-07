#pragma once

// Small line-based text formats from scripts/ and vdata/system/: the
// experience table, credits, sentences, on-screen titles and DSP presets.
// See docs/formats/text_data.md. All parsers treat text as Windows-1252 bytes
// and never throw; unparseable lines become diagnostics.

#include "openbl/formats/text_diagnostic.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

// vdata/system/experience_table.txt: "Title | Description | XP" rows; lines
// starting with '>' are comments. Each row awards experience for a quest step.
struct ExperienceRow {
    std::string title, description;
    int experience = 0;
};
struct ExperienceTable {
    std::vector<ExperienceRow> rows;
    std::vector<TextDiagnostic> diagnostics;
};
ExperienceTable parse_experience_table(std::string_view text);

// vdata/system/credits.txt and similar: '#' comments; '@' font, '$' colour,
// '%' drop-shadow colour and '^' box commands; every other line is text.
struct CreditsLine {
    enum class Kind { Text, Font, Color, Shadow, Box } kind = Kind::Text;
    std::string value; // Text, font name or command arguments.
};
std::vector<CreditsLine> parse_credits(std::string_view text);

// scripts/sentences.txt: "NAME sound/path {options}" lines. Names that end
// in a number form groups the engine picks from (SPI_DIES0, SPI_DIES1).
struct Sentence {
    std::string name, sound, options;
};
struct SentenceList {
    std::vector<Sentence> sentences;
    std::vector<TextDiagnostic> diagnostics;
};
SentenceList parse_sentences(std::string_view text);

// scripts/titles.txt: "$name args" settings, then "NAME { text }" blocks that
// use the settings in effect.
struct Title {
    std::string name, text;
    std::vector<std::pair<std::string, std::string>> settings; // In effect for this title.
};
struct TitleList {
    std::vector<Title> titles;
    std::vector<TextDiagnostic> diagnostics;
};
TitleList parse_titles(std::string_view text);

// scripts/dsp_presets.txt: "{ index type numbers... { processor } ... }".
struct DspProcessor {
    std::vector<std::string> tokens; // e.g. "RVA", "80.0", "30.0", ...
};
struct DspPreset {
    std::vector<std::string> header; // index, type (LINEAR, ...), then numbers.
    std::vector<DspProcessor> processors;
};
struct DspPresetList {
    std::vector<DspPreset> presets;
    std::vector<TextDiagnostic> diagnostics;
};
DspPresetList parse_dsp_presets(std::string_view text);

} // namespace openbl::formats
