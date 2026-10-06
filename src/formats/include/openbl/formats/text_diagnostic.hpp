#pragma once

#include <cstddef>
#include <string>

namespace openbl::formats {

// A recoverable problem found while parsing a text format. Text parsers never
// throw on malformed input; they recover and report what they skipped.
struct TextDiagnostic {
    std::size_t line = 0; // 1-based.
    std::string message;
};

} // namespace openbl::formats
