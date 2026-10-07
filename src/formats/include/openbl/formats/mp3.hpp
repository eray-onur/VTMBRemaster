#pragma once

// MP3 audio (MPEG-1/2 Layer III), used by about 5,000 of the game's voice
// lines. Decoded with minimp3 (public domain). See docs/formats/audio.md.

#include "openbl/formats/wav.hpp"

#include <cstddef>
#include <span>

namespace openbl::formats {

// Decodes a whole file to interleaved 16-bit samples. Leading ID3v2 tags and
// junk between frames are skipped. Throws FormatError if no MP3 frame is found
// or the stream changes sample rate or channel count part way.
AudioClip decode_mp3(std::span<const std::byte> data);

} // namespace openbl::formats
