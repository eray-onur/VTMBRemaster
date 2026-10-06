#pragma once

// Conversions from the encodings found in the game's text files to UTF-8.
// Most text files are Windows-1252; captions and localisation are UTF-16LE.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace openbl::formats {

// Unpaired surrogates become U+FFFD. A trailing odd byte is ignored.
std::string utf16le_to_utf8(std::span<const std::byte> data);
std::string cp1252_to_utf8(std::string_view text);

} // namespace openbl::formats
