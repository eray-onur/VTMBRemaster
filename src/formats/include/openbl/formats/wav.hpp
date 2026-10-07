#pragma once

// WAV audio: uncompressed PCM (8/16-bit) and Microsoft ADPCM (format 2),
// which 98% of the game's .wav files use. See docs/formats/audio.md.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

struct AudioClip {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::vector<std::int16_t> samples; // Interleaved.
    // Sample count from the file's "fact" chunk, if present (compressed formats).
    std::uint32_t declared_frames = 0;

    std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};

// Throws FormatError on malformed or unsupported data.
AudioClip decode_wav(std::span<const std::byte> data);

} // namespace openbl::formats
