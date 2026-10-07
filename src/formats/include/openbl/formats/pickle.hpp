#pragma once

// A restricted reader for Python pickles (protocols 0 and 1), as stored in the
// Python block of save files. It builds plain values and never executes
// anything: GLOBAL, INST, REDUCE, BUILD and OBJ produce inert Object values
// naming the class and holding the arguments and state. See
// docs/formats/saves.md.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

struct PickleValue {
    enum class Kind { None, Bool, Int, Float, String, Unicode, Tuple, List, Dict, Global, Object, Mark };
    Kind kind = Kind::None;
    bool boolean = false;
    std::int64_t integer = 0; // Int; Python longs beyond 64 bits are rejected.
    double real = 0.0;
    std::string text; // String (bytes), Unicode (UTF-8), Global/Object ("module.name").
    std::vector<PickleValue> items;                         // Tuple, List; Object: constructor arguments.
    std::vector<std::pair<PickleValue, PickleValue>> dict;  // Dict; Object: state from BUILD.

    // Dict lookup by string key; null if absent or not a dict.
    const PickleValue* get(std::string_view key) const;
};

// Throws FormatError on malformed or unsupported data, or if limits (nesting,
// item counts, memo size) are exceeded.
PickleValue parse_pickle(std::span<const std::byte> data);

} // namespace openbl::formats
