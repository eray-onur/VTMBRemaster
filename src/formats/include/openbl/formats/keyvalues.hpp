#pragma once

// Text KeyValues, the nested "key" "value" / "key" { ... } syntax used by
// .vmt materials and many other text files. See docs/formats/materials.md.

#include "openbl/formats/text_diagnostic.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::formats {

struct KvNode {
    std::string key;
    std::string value;           // Leaf nodes only.
    std::vector<KvNode> children; // Block nodes only.
    bool is_block = false;
    std::size_t line = 0; // 1-based line of the key.

    // Case-insensitive lookup among direct children; first match.
    const KvNode* find(std::string_view child_key) const;
};

using KvDiagnostic = TextDiagnostic;

struct KvDocument {
    std::vector<KvNode> roots;
    std::vector<KvDiagnostic> diagnostics;
};

// Never throws on malformed input: problems are recovered from and reported
// as diagnostics. Text is treated as bytes (the game files are Windows-1252).
//   - "//" starts a comment to end of line; there are no escape sequences.
//   - Quoted strings may span lines. Unquoted tokens end at whitespace, '"', '{' or '}'.
//   - Unclosed blocks are closed at end of input; stray '}' at top level are ignored.
KvDocument parse_keyvalues(std::string_view text);

bool iequals(std::string_view a, std::string_view b);

} // namespace openbl::formats
