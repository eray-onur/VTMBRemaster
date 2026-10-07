#include "openbl/formats/wav.hpp"

#include "binary_reader.hpp"

#include <algorithm>
#include <array>

namespace openbl::formats {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatMsAdpcm = 2;

// Step-size adaptation per 4-bit code, from the Microsoft ADPCM specification.
constexpr std::array<int, 16> kAdaptation = {230, 230, 230, 230, 307, 409, 512, 614,
                                             768, 614, 512, 409, 307, 230, 230, 230};

std::int16_t clamp16(int v) {
    return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
}

struct AdpcmChannel {
    int coef1 = 0, coef2 = 0;
    int delta = 0;
    int sample1 = 0, sample2 = 0; // Most recent first.

    std::int16_t decode(int nibble) {
        const int signed_nibble = nibble >= 8 ? nibble - 16 : nibble;
        const int predicted = (sample1 * coef1 + sample2 * coef2) / 256;
        const std::int16_t out = clamp16(predicted + signed_nibble * delta);
        sample2 = sample1;
        sample1 = out;
        delta = std::max(16, kAdaptation[static_cast<std::size_t>(nibble)] * delta / 256);
        return out;
    }
};

void decode_ms_adpcm(std::span<const std::byte> data, std::uint16_t channels, std::uint16_t block_align,
                     std::uint16_t samples_per_block, const std::vector<std::array<int, 2>>& coefs,
                     detail::BinaryReader& r, std::vector<std::int16_t>& out) {
    const std::size_t header = 7u * channels;
    if (block_align < header || samples_per_block < 2) {
        r.fail("bad ADPCM block layout");
    }
    for (std::size_t block = 0; block < data.size(); block += block_align) {
        const auto b = data.subspan(block, std::min<std::size_t>(block_align, data.size() - block));
        if (b.size() < header) {
            break; // Trailing partial header: no samples.
        }
        std::array<AdpcmChannel, 2> ch{};
        auto byte = [&](std::size_t i) { return std::to_integer<std::uint8_t>(b[i]); };
        auto i16 = [&](std::size_t i) { return static_cast<std::int16_t>(detail::load_u16(b.data() + i)); };
        for (std::size_t c = 0; c < channels; ++c) {
            const std::size_t predictor = byte(c);
            if (predictor >= coefs.size()) {
                r.fail("ADPCM predictor out of range");
            }
            ch[c].coef1 = coefs[predictor][0];
            ch[c].coef2 = coefs[predictor][1];
            ch[c].delta = i16(channels + 2 * c);
            ch[c].sample1 = i16(3 * channels + 2 * c);
            ch[c].sample2 = i16(5 * channels + 2 * c);
        }
        // The two seed samples come first, oldest first.
        for (std::size_t c = 0; c < channels; ++c) {
            out.push_back(static_cast<std::int16_t>(ch[c].sample2));
        }
        for (std::size_t c = 0; c < channels; ++c) {
            out.push_back(static_cast<std::int16_t>(ch[c].sample1));
        }
        // Then one nibble per sample, high nibble first, channels interleaved.
        std::size_t produced = 2;
        std::size_t channel = 0;
        for (std::size_t i = header; i < b.size() && produced < samples_per_block; ++i) {
            for (const int nibble : {byte(i) >> 4, byte(i) & 0x0F}) {
                out.push_back(ch[channel].decode(nibble));
                if (++channel == channels) {
                    channel = 0;
                    ++produced;
                }
            }
        }
    }
}

} // namespace

AudioClip decode_wav(std::span<const std::byte> data) {
    detail::BinaryReader r(data, "WAV");
    const auto riff = r.bytes(4);
    r.u32();
    const auto wave = r.bytes(4);
    if (riff[0] != std::byte{'R'} || riff[1] != std::byte{'I'} || riff[2] != std::byte{'F'} ||
        riff[3] != std::byte{'F'} || wave[0] != std::byte{'W'} || wave[1] != std::byte{'A'} ||
        wave[2] != std::byte{'V'} || wave[3] != std::byte{'E'}) {
        r.fail("not a RIFF WAVE file");
    }
    AudioClip clip;
    std::uint16_t format = 0, block_align = 0, bits = 0, samples_per_block = 0;
    std::vector<std::array<int, 2>> coefs;
    std::span<const std::byte> pcm;
    bool have_format = false, have_data = false;
    while (r.remaining() >= 8) {
        const auto id = r.bytes(4);
        const std::uint32_t size = r.u32();
        const std::size_t start = r.pos();
        const std::size_t length = std::min<std::size_t>(size, r.remaining());
        auto is = [&](const char* name) {
            return id[0] == std::byte(name[0]) && id[1] == std::byte(name[1]) && id[2] == std::byte(name[2]) &&
                   id[3] == std::byte(name[3]);
        };
        if (is("fmt ")) {
            format = r.u16();
            clip.channels = r.u16();
            clip.sample_rate = r.u32();
            r.u32(); // Bytes per second.
            block_align = r.u16();
            bits = r.u16();
            if (format == kFormatMsAdpcm && length >= 20) {
                r.u16(); // Extra size.
                samples_per_block = r.u16();
                const std::uint16_t count = r.u16();
                for (std::uint16_t i = 0; i < count; ++i) {
                    const int c1 = static_cast<std::int16_t>(r.u16());
                    const int c2 = static_cast<std::int16_t>(r.u16());
                    coefs.push_back({c1, c2});
                }
            }
            have_format = true;
        } else if (is("fact") && length >= 4) {
            clip.declared_frames = r.u32();
        } else if (is("data")) {
            pcm = data.subspan(start, length);
            have_data = true;
        }
        r.seek(start + length + (length & 1 && start + length < data.size() ? 1 : 0));
    }
    if (!have_format || !have_data || clip.channels == 0 || clip.channels > 2 || clip.sample_rate == 0) {
        r.fail("missing or unsupported format or data");
    }

    if (format == kFormatPcm && (bits == 16 || bits == 8)) {
        if (bits == 16) {
            clip.samples.resize(pcm.size() / 2);
            for (std::size_t i = 0; i < clip.samples.size(); ++i) {
                clip.samples[i] = static_cast<std::int16_t>(detail::load_u16(pcm.data() + 2 * i));
            }
        } else {
            for (const std::byte b : pcm) {
                clip.samples.push_back(static_cast<std::int16_t>((std::to_integer<int>(b) - 128) * 256));
            }
        }
    } else if (format == kFormatMsAdpcm && bits == 4 && !coefs.empty()) {
        decode_ms_adpcm(pcm, clip.channels, block_align, samples_per_block, coefs, r, clip.samples);
        // The last block may be padded: trim to the declared length.
        if (clip.declared_frames && clip.frames() > clip.declared_frames) {
            clip.samples.resize(std::size_t{clip.declared_frames} * clip.channels);
        }
    } else {
        r.fail("unsupported WAV encoding (format " + std::to_string(format) + ", " + std::to_string(bits) + " bits)");
    }
    return clip;
}

} // namespace openbl::formats
