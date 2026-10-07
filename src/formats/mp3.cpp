#include "openbl/formats/mp3.hpp"

#include "openbl/formats/error.hpp"

#include <algorithm>

#define MINIMP3_IMPLEMENTATION
#include <minimp3.h>

namespace openbl::formats {

AudioClip decode_mp3(std::span<const std::byte> data) {
    mp3dec_t dec;
    mp3dec_init(&dec);
    AudioClip clip;
    const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t left = data.size();
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (left > 0) {
        mp3dec_frame_info_t info{};
        const int frames = mp3dec_decode_frame(&dec, p, static_cast<int>(std::min<std::size_t>(left, 1 << 30)), pcm, &info);
        if (info.frame_bytes == 0) {
            break; // No further frame in the remaining data.
        }
        p += info.frame_bytes;
        left -= static_cast<std::size_t>(info.frame_bytes);
        if (frames == 0) {
            continue; // Skipped data (tags, junk) or a frame without output.
        }
        if (clip.channels == 0) {
            clip.sample_rate = static_cast<std::uint32_t>(info.hz);
            clip.channels = static_cast<std::uint16_t>(info.channels);
        } else if (clip.sample_rate != static_cast<std::uint32_t>(info.hz) ||
                   clip.channels != static_cast<std::uint16_t>(info.channels)) {
            throw FormatError("MP3: sample rate or channel count changes mid-stream");
        }
        clip.samples.insert(clip.samples.end(), pcm, pcm + frames * info.channels);
    }
    if (clip.channels == 0) {
        throw FormatError("MP3: no audio frames");
    }
    return clip;
}

} // namespace openbl::formats
