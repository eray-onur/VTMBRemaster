#pragma once

#include <string>
#include <string_view>

namespace openbl::formats {

// Canonical form for game-relative paths: lowercase ASCII, '/' separators,
// no leading "./" or '/', no duplicate separators. All lookups use this form.
std::string normalize_game_path(std::string_view path);

// True if the (normalized) path is relative and contains no ".." components,
// i.e. it cannot escape a directory it is joined onto.
bool is_safe_relative_path(std::string_view normalized_path);

} // namespace openbl::formats
