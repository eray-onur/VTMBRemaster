#pragma once

// Localisation token files (resource/gameui_<language>.txt): UTF-16LE
// KeyValues of the form "lang" { "Language" "English" "Tokens" { ... } }.

#include "openbl/formats/text_diagnostic.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace openbl::formats {

struct Localization {
    std::string language;
    std::map<std::string, std::string> tokens; // Token name -> UTF-8 text, "\n" sequences expanded.
    std::vector<TextDiagnostic> diagnostics;
};

// Accepts UTF-16LE with a byte-order mark, UTF-8 with or without one, or
// Windows-1252. Never throws.
Localization parse_localization(std::span<const std::byte> data);

} // namespace openbl::formats
