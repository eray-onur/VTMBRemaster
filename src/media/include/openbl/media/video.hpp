#pragma once

// Movie decoding (the Bink intros in media/*.bik) through FFmpeg's libavcodec,
// which is LGPL and linked dynamically. See docs/formats/video.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openbl::media {

struct VideoFrame {
    std::uint32_t width = 0, height = 0;
    double time = 0.0;              // Seconds from the start.
    std::vector<std::uint8_t> rgba; // width * height * 4.
};

class VideoDecoder {
public:
    // Decodes from a copy of `data` (e.g. read from the game's archives).
    // Throws std::runtime_error if FFmpeg cannot open the stream.
    explicit VideoDecoder(std::span<const std::byte> data);
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    std::uint32_t width() const;
    std::uint32_t height() const;
    double fps() const;
    std::string codec_name() const;
    bool has_audio() const;
    std::uint32_t audio_sample_rate() const;
    std::uint16_t audio_channels() const;

    // Decodes the next video frame; false at the end of the stream. Audio met
    // along the way is appended to the audio buffer.
    bool next(VideoFrame& out);
    // Interleaved 16-bit samples decoded so far, cleared by the call.
    std::vector<std::int16_t> take_audio();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openbl::media
