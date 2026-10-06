#pragma once

// Facial expression and phoneme sets: the source tables
// (expressions/<name>.txt) and their compiled form (.vfe). Each set names a
// list of flex keys and defines expressions as a value and weight per key.
// See docs/formats/expressions.md.

#include "openbl/formats/text_diagnostic.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::formats {

struct Expression {
    std::string name;
    char32_t code = '_';            // '_' for expressions; the phoneme's code point for phoneme sets.
    std::vector<float> values;      // One per key.
    std::vector<float> weights;     // One per key.
    std::string description;        // Source tables only; not compiled into .vfe.
};

struct ExpressionSet {
    std::vector<std::string> keys;  // Flex key names.
    std::vector<Expression> expressions;
    std::vector<TextDiagnostic> diagnostics; // Source tables only.
};

// Source table (.txt). Never throws.
ExpressionSet parse_expression_table(std::string_view text);

// Compiled set (.vfe). Throws FormatError on malformed input.
ExpressionSet parse_vfe(std::span<const std::byte> data);

} // namespace openbl::formats
