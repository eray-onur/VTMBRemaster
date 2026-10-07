#pragma once

// Bink video container (media/*.bik): header, audio tracks and frame index.
// Decoding the video and audio is done by libavcodec (openbl/media). See
// docs/formats/video.md.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openbl::formats {

struct BinkAudioTrack {
    std::uint32_t id = 0;
    std::uint16_t sample_rate = 0;
    std::uint16_t flags = 0;   // 0x2000 stereo, 0x1000 DCT (else RDFT), as read; see the spec.
    std::uint32_t max_size = 0; // Largest decoded packet, as declared.
};

struct BinkFrame {
    std::uint32_t offset = 0, size = 0;
    bool keyframe = false;
};

struct BinkFile {
    char revision = 0; // 'i' for all four Bloodlines movies.
    std::uint32_t width = 0, height = 0;
    std::uint32_t fps_num = 0, fps_den = 1;
    std::uint32_t flags = 0;
    std::uint32_t largest_frame = 0;
    std::vector<BinkAudioTrack> audio;
    std::vector<BinkFrame> frames;

    double fps() const { return fps_den ? static_cast<double>(fps_num) / fps_den : 0.0; }
};

// Throws FormatError on malformed data.
BinkFile parse_bik(std::span<const std::byte> data);

} // namespace openbl::formats
