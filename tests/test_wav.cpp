#include "openbl/formats/error.hpp"
#include "openbl/formats/mp3.hpp"
#include "openbl/formats/wav.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <map>
#include <span>
#include <string>

using namespace openbl;
using namespace openbl::formats;

namespace {

struct Riff {
    std::vector<std::byte> d;
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        d.insert(d.end(), b, b + n);
    }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
};

std::vector<std::byte> wav(std::uint16_t format, std::uint16_t channels, std::uint16_t bits, std::uint16_t align,
                           const std::vector<std::byte>& fmt_extra, const std::vector<std::byte>& payload,
                           std::uint32_t fact) {
    Riff body;
    body.raw("WAVE", 4);
    body.raw("fmt ", 4);
    body.u32(static_cast<std::uint32_t>(16 + fmt_extra.size()));
    body.u16(format);
    body.u16(channels);
    body.u32(22050);
    body.u32(22050u * align);
    body.u16(align);
    body.u16(bits);
    body.raw(fmt_extra.data(), fmt_extra.size());
    if (fact) {
        body.raw("fact", 4);
        body.u32(4);
        body.u32(fact);
    }
    body.raw("data", 4);
    body.u32(static_cast<std::uint32_t>(payload.size()));
    body.raw(payload.data(), payload.size());
    Riff out;
    out.raw("RIFF", 4);
    out.u32(static_cast<std::uint32_t>(body.d.size()));
    out.raw(body.d.data(), body.d.size());
    return out.d;
}

} // namespace

TEST_CASE("WAV PCM") {
    const std::int16_t samples[4] = {0, 1000, -1000, 32767};
    std::vector<std::byte> payload(8);
    std::memcpy(payload.data(), samples, 8);
    const auto clip = decode_wav(wav(1, 1, 16, 2, {}, payload, 0));
    CHECK(clip.sample_rate == 22050);
    REQUIRE(clip.samples.size() == 4);
    CHECK(clip.samples[2] == -1000);
}

TEST_CASE("WAV Microsoft ADPCM") {
    // One mono block: header {predictor 0, delta 16, sample1 100, sample2 50},
    // then codes 1 and 0. Coefficient pair 0 = (256, 0): predict the last sample.
    Riff extra;
    extra.u16(4 + 4); // cbSize
    extra.u16(4);     // samples per block
    extra.u16(1);     // one coefficient pair
    extra.u16(256);
    extra.u16(0);
    Riff block;
    block.raw("\x00", 1);
    block.u16(16);
    block.u16(100);
    block.u16(50);
    block.raw("\x10", 1); // nibbles 1 then 0
    const auto clip = decode_wav(wav(2, 1, 4, 8, extra.d, block.d, 4));
    REQUIRE(clip.samples.size() == 4);
    CHECK(clip.samples[0] == 50);
    CHECK(clip.samples[1] == 100);
    CHECK(clip.samples[2] == 116); // 100 + 1 * 16
    CHECK(clip.samples[3] == 116); // + 0 * delta
    CHECK_THROWS_AS(decode_wav(std::vector<std::byte>(12)), FormatError);
}

TEST_CASE("WAV integration: every sound in the real install decodes") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, empty = 0, adpcm_checked = 0, length_mismatch = 0;
    double seconds = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".wav")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        if (data->empty()) {
            ++empty; // 15 zero-byte placeholders in the release.
            continue;
        }
        AudioClip clip;
        REQUIRE_NOTHROW(clip = decode_wav(*data));
        ++files;
        seconds += static_cast<double>(clip.frames()) / clip.sample_rate;
        if (clip.declared_frames) {
            ++adpcm_checked;
            // 145 files declare one frame more than their final partial block
            // holds (an encoder quirk); anything else is a decoding error.
            length_mismatch += clip.frames() != clip.declared_frames && clip.frames() + 1 != clip.declared_frames;
        }
    }
    MESSAGE(files << " sounds (" << std::lround(seconds / 60) << " minutes), " << empty << " empty files, "
                  << adpcm_checked << " with a declared length, " << length_mismatch << " not matching it");
    CHECK(files > 3800);
    CHECK(length_mismatch == 0);
}

TEST_CASE("MP3 rejects data without frames") {
    const std::string junk(4096, 'x');
    CHECK_THROWS_AS(decode_mp3(std::as_bytes(std::span(junk.data(), junk.size()))), FormatError);
}

TEST_CASE("MP3 integration: every voice line in the real install decodes") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, empty = 0, failed = 0, mono = 0, rough = 0;
    double seconds = 0;
    std::map<std::uint32_t, std::size_t> rates;
    std::string first_failure;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".mp3")) {
            continue;
        }
        const auto data = gfs.read(path);
        if (data->empty()) {
            ++empty;
            continue;
        }
        AudioClip clip;
        try {
            clip = decode_mp3(*data);
        } catch (const FormatError& e) {
            if (first_failure.empty()) {
                first_failure = path + ": " + e.what();
            }
            ++failed;
            continue;
        }
        ++files;
        ++rates[clip.sample_rate];
        mono += clip.channels == 1;
        seconds += static_cast<double>(clip.frames()) / clip.sample_rate;
        // Decoded speech is smooth: neighbouring samples differ far less than
        // the signal level (white noise would give about 1.4).
        double diff = 0, level = 0;
        for (std::size_t i = clip.channels; i < clip.samples.size(); ++i) {
            diff += std::abs(double(clip.samples[i]) - clip.samples[i - clip.channels]);
            level += std::abs(double(clip.samples[i]));
        }
        if (level > 0 && diff / level > 0.8) {
            ++rough;
            MESSAGE("noise-like: " << path << " ratio " << diff / level);
        }
    }
    std::string rate_list;
    for (const auto& [r, n] : rates) {
        rate_list += std::to_string(r) + " Hz x" + std::to_string(n) + " ";
    }
    MESSAGE(files << " MP3s (" << std::lround(seconds / 60) << " minutes, " << mono << " mono), " << empty
                  << " empty, " << failed << " failed " << first_failure << "; " << rate_list << "; " << rough
                  << " noise-like");
    CHECK(files > 5000);
    CHECK(failed == 0);
    CHECK(rough <= 1); // Skelter's whispered line is genuinely noise-like.
}
