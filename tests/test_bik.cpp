#include "openbl/formats/bik.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#ifdef OPENBL_WITH_FFMPEG
#include "openbl/media/video.hpp"
#endif

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("BIK header and frame index") {
    // 2 frames, 1 audio track; frame 0 is a keyframe.
    std::vector<std::byte> d(44 + 4 + 4 + 4 + 12);
    auto u32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(d.data() + at, &v, 4); };
    std::memcpy(d.data(), "BIKi", 4);
    u32(4, static_cast<std::uint32_t>(d.size() + 10 - 8));
    u32(8, 2);
    u32(12, 6);
    u32(20, 320);
    u32(24, 240);
    u32(28, 30);
    u32(32, 1);
    u32(40, 1);
    u32(44, 1000);                                          // Max decoded audio size.
    u32(48, 22050 | (0x2000u << 16));                       // Rate, flags.
    u32(52, 0);                                             // Track id.
    const auto first = static_cast<std::uint32_t>(d.size());
    u32(56, first | 1);
    u32(60, first + 4);
    d.resize(d.size() + 10);
    u32(64, static_cast<std::uint32_t>(d.size()));
    const auto bik = parse_bik(d);
    CHECK(bik.revision == 'i');
    CHECK(bik.width == 320);
    CHECK(bik.fps() == 30.0);
    REQUIRE(bik.audio.size() == 1);
    CHECK(bik.audio[0].sample_rate == 22050);
    REQUIRE(bik.frames.size() == 2);
    CHECK(bik.frames[0].keyframe);
    CHECK(bik.frames[0].size == 4);
    CHECK(bik.frames[1].size == 6);

    auto bad = d;
    bad[0] = std::byte{'X'};
    CHECK_THROWS_AS(parse_bik(bad), FormatError);
}

TEST_CASE("BIK integration: the intro movies") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t movies = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".bik")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        BinkFile bik;
        REQUIRE_NOTHROW(bik = parse_bik(*data));
        ++movies;
#ifdef OPENBL_WITH_FFMPEG
        media::VideoDecoder dec(*data);
        CHECK(dec.width() == bik.width);
        CHECK(dec.height() == bik.height);
        media::VideoFrame frame;
        std::size_t frames = 0, lit = 0;
        while (dec.next(frame)) {
            ++frames;
            double sum = 0;
            for (std::size_t i = 0; i < frame.rgba.size(); i += 4 * 97) {
                sum += frame.rgba[i] + frame.rgba[i + 1] + frame.rgba[i + 2];
            }
            lit += sum > 0;
        }
        const auto audio = dec.take_audio();
        const double seconds = static_cast<double>(bik.frames.size()) / bik.fps();
        const double audio_seconds = dec.audio_channels()
                                         ? static_cast<double>(audio.size()) / dec.audio_channels() / dec.audio_sample_rate()
                                         : 0.0;
        MESSAGE(path << ": " << dec.codec_name() << " " << bik.width << "x" << bik.height << ", " << frames << "/"
                     << bik.frames.size() << " frames decoded, " << lit << " not black, audio " << audio_seconds
                     << " s of " << seconds << " s at " << dec.audio_sample_rate() << " Hz x" << dec.audio_channels());
        CHECK(frames == bik.frames.size());
        CHECK(lit > frames / 2);
        CHECK(std::abs(audio_seconds - seconds) < 0.5);
#endif
    }
    CHECK(movies == 4);
}
