#pragma once

// Dialogue trees (.dlg). One line per record, each record a run of 13
// brace-delimited fields. See docs/formats/dialogue.md.

#include "openbl/formats/text_diagnostic.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::formats {

struct DlgLine {
    std::size_t source_line = 0; // 1-based line in the file where the record starts.
    std::string index;           // Usually equals the record's position (1-based); kept as written.
    std::string male_text;       // NPC lines: spoken to a male player. Player lines: the male player's reply.
    std::string female_text;
    // "#" marks an NPC line. On a player reply: the index of the NPC line it
    // leads to, "0" to end the conversation, or a script expression such as
    // "RandomLine([571, 561])". Empty on placeholder rows.
    std::string next;
    std::string condition; // Script expression; on NPC lines mostly a statement run when the line plays (unverified).
    std::string action;    // Script statements run when the line is chosen or spoken.
    std::array<std::string, 6> unused; // Columns 6-11, empty in shipped files apart from a note.
    std::string malkavian_text;        // Alternative text for Malkavian players; empty means use the normal text.

    bool is_npc() const { return next == "#"; }
    bool is_placeholder() const;            // No text, link, condition or action.
    std::optional<int> number() const;      // index as an integer, if numeric.
    std::optional<int> next_number() const; // next as an integer, if numeric.
};

struct DlgFile {
    std::vector<DlgLine> lines; // In file order, including placeholder rows.
    std::vector<TextDiagnostic> diagnostics;
};

// Text is treated as bytes (Windows-1252). Never throws.
DlgFile parse_dlg(std::string_view text);

} // namespace openbl::formats
