#pragma once

// Entity I/O connections, as map entities store them (docs/formats/maps.md).
//
// An output is a key/value on the entity that fires it: the key is the output's
// name ("OnTrigger") and the value lists, comma-separated, the target, the
// input to call on it, a parameter, a delay in seconds, how many times the
// connection may fire (-1: every time) and, in Bloodlines, a Python statement
// run when it fires: "target,Input,param,delay,times,python,". Every output in
// the shipped maps has those seven fields, the last one empty.

#include <optional>
#include <string>
#include <string_view>

namespace openbl::formats {

struct EntityOutput {
    std::string target;    // Targetname, classname or "!activator"-style keyword; may end in '*'.
    std::string input;     // Empty when only the Python statement runs.
    std::string parameter; // Empty: the input's default (or the value the output passes).
    float delay = 0.0f;    // Seconds.
    int times = -1;        // Times it may still fire; -1: unlimited.
    std::string python;    // Bloodlines: a statement for the script host; empty: none.
};

// Parses an output value; empty if it has fewer than five fields or a delay or
// count that is not a number.
std::optional<EntityOutput> parse_entity_output(std::string_view value);

} // namespace openbl::formats
