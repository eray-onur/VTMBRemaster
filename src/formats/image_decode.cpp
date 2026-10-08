#include "openbl/formats/image_decode.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"

#include <array>
#include <string>

namespace openbl::formats {

namespace {

using detail::load_u16;
using detail::load_u32;
using Rgba = std::array<std::uint8_t, 4>;

std::uint8_t expand(unsigned value, unsigned bits) {
    // Replicate high bits into the low bits so that the maximum maps to 255.
    const unsigned shifted = value << (8 - bits);
    return static_cast<std::uint8_t>(shifted | (shifted >> bits));
}

Rgba from_565(std::uint16_t v) {
    return {expand((v >> 11) & 31, 5), expand((v >> 5) & 63, 6), expand(v & 31, 5), 255};
}

std::uint8_t lerp(unsigned a, unsigned b, unsigned num, unsigned den) {
    return static_cast<std::uint8_t>((a * (den - num) + b * num) / den);
}

// Decodes the 8-byte color part of a DXT block into 16 pixels.
void decode_color_block(const std::byte* block, bool allow_transparent, Rgba out[16]) {
    const std::uint16_t c0 = load_u16(block);
    const std::uint16_t c1 = load_u16(block + 2);
    Rgba palette[4] = {from_565(c0), from_565(c1), {}, {}};
    if (c0 > c1 || !allow_transparent) {
        for (int ch = 0; ch < 3; ++ch) {
            palette[2][ch] = lerp(palette[0][ch], palette[1][ch], 1, 3);
            palette[3][ch] = lerp(palette[0][ch], palette[1][ch], 2, 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int ch = 0; ch < 3; ++ch) {
            palette[2][ch] = lerp(palette[0][ch], palette[1][ch], 1, 2);
        }
        palette[2][3] = 255;
        palette[3] = {0, 0, 0, 0};
    }
    const std::uint32_t indices = load_u32(block + 4);
    for (int i = 0; i < 16; ++i) {
        out[i] = palette[(indices >> (2 * i)) & 3];
    }
}

void decode_dxt5_alpha(const std::byte* block, Rgba px[16]) {
    const unsigned a0 = static_cast<unsigned>(block[0]);
    const unsigned a1 = static_cast<unsigned>(block[1]);
    std::uint8_t palette[8] = {static_cast<std::uint8_t>(a0), static_cast<std::uint8_t>(a1)};
    if (a0 > a1) {
        for (unsigned i = 1; i < 7; ++i) {
            palette[i + 1] = lerp(a0, a1, i, 7);
        }
    } else {
        for (unsigned i = 1; i < 5; ++i) {
            palette[i + 1] = lerp(a0, a1, i, 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        px[i][3] = palette[(bits >> (3 * i)) & 7];
    }
}

std::vector<std::uint8_t> decode_blocks(VtfFormat format, std::span<const std::byte> data, std::uint32_t width,
                                        std::uint32_t height) {
    const std::uint32_t bx = (width + 3) / 4;
    const std::uint32_t by = (height + 3) / 4;
    const std::size_t block_size = format == VtfFormat::DXT1 || format == VtfFormat::DXT1_OneBitAlpha ? 8 : 16;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4);

    for (std::uint32_t y = 0; y < by; ++y) {
        for (std::uint32_t x = 0; x < bx; ++x) {
            const std::byte* block = data.data() + (static_cast<std::size_t>(y) * bx + x) * block_size;
            Rgba px[16];
            switch (format) {
            case VtfFormat::DXT1:
            case VtfFormat::DXT1_OneBitAlpha:
                decode_color_block(block, true, px);
                break;
            case VtfFormat::DXT3:
                decode_color_block(block + 8, false, px);
                for (int i = 0; i < 16; ++i) {
                    const auto nibble = (static_cast<unsigned>(block[i / 2]) >> (4 * (i % 2))) & 15;
                    px[i][3] = expand(nibble, 4);
                }
                break;
            default: // DXT5
                decode_color_block(block + 8, false, px);
                decode_dxt5_alpha(block, px);
                break;
            }
            for (std::uint32_t py = 0; py < 4 && y * 4 + py < height; ++py) {
                for (std::uint32_t qx = 0; qx < 4 && x * 4 + qx < width; ++qx) {
                    const std::size_t dst = ((static_cast<std::size_t>(y) * 4 + py) * width + x * 4 + qx) * 4;
                    const Rgba& p = px[py * 4 + qx];
                    out[dst + 0] = p[0];
                    out[dst + 1] = p[1];
                    out[dst + 2] = p[2];
                    out[dst + 3] = p[3];
                }
            }
        }
    }
    return out;
}

Rgba decode_pixel(VtfFormat format, const std::byte* p) {
    auto b = [p](int i) { return static_cast<std::uint8_t>(p[i]); };
    switch (format) {
    case VtfFormat::RGBA8888:
    case VtfFormat::UVLX8888:
        return {b(0), b(1), b(2), b(3)};
    case VtfFormat::UVWQ8888:
        // Signed bytes, biased to unsigned (0 -> 128) like a normal map.
        return {static_cast<std::uint8_t>(b(0) ^ 0x80), static_cast<std::uint8_t>(b(1) ^ 0x80),
                static_cast<std::uint8_t>(b(2) ^ 0x80), static_cast<std::uint8_t>(b(3) ^ 0x80)};
    case VtfFormat::ABGR8888:
        return {b(3), b(2), b(1), b(0)};
    case VtfFormat::ARGB8888:
        return {b(1), b(2), b(3), b(0)};
    case VtfFormat::BGRA8888:
        return {b(2), b(1), b(0), b(3)};
    case VtfFormat::BGRX8888:
        return {b(2), b(1), b(0), 255};
    case VtfFormat::RGB888:
        return {b(0), b(1), b(2), 255};
    case VtfFormat::BGR888:
        return {b(2), b(1), b(0), 255};
    case VtfFormat::RGB888_BlueScreen: {
        const bool key = b(0) == 0 && b(1) == 0 && b(2) == 255;
        return {b(0), b(1), b(2), static_cast<std::uint8_t>(key ? 0 : 255)};
    }
    case VtfFormat::BGR888_BlueScreen: {
        const bool key = b(2) == 0 && b(1) == 0 && b(0) == 255;
        return {b(2), b(1), b(0), static_cast<std::uint8_t>(key ? 0 : 255)};
    }
    case VtfFormat::RGB565:
        return from_565(load_u16(p));
    case VtfFormat::BGR565: {
        const Rgba c = from_565(load_u16(p));
        return {c[2], c[1], c[0], 255};
    }
    case VtfFormat::BGRA4444: {
        const std::uint16_t v = load_u16(p);
        return {expand((v >> 8) & 15, 4), expand((v >> 4) & 15, 4), expand(v & 15, 4), expand((v >> 12) & 15, 4)};
    }
    case VtfFormat::BGRA5551:
    case VtfFormat::BGRX5551: {
        const std::uint16_t v = load_u16(p);
        const bool alpha = format == VtfFormat::BGRX5551 || (v & 0x8000) != 0;
        return {expand((v >> 10) & 31, 5), expand((v >> 5) & 31, 5), expand(v & 31, 5),
                static_cast<std::uint8_t>(alpha ? 255 : 0)};
    }
    case VtfFormat::I8:
        return {b(0), b(0), b(0), 255};
    case VtfFormat::IA88:
        return {b(0), b(0), b(0), b(1)};
    case VtfFormat::A8:
        return {0, 0, 0, b(0)};
    case VtfFormat::UV88:
        return {b(0), b(1), 0, 255};
    default:
        return {};
    }
}

} // namespace

bool can_decode_to_rgba8(VtfFormat format) {
    switch (format) {
    case VtfFormat::None:
    case VtfFormat::P8:
    case VtfFormat::RGBA16161616F:
    case VtfFormat::RGBA16161616:
        return false;
    default:
        return static_cast<std::int32_t>(format) >= 0 && static_cast<std::int32_t>(format) <= 26;
    }
}

std::vector<std::uint8_t> decode_to_rgba8(VtfFormat format, std::span<const std::byte> data, std::uint32_t width,
                                          std::uint32_t height) {
    if (!can_decode_to_rgba8(format)) {
        throw FormatError("cannot decode image format " + std::string(vtf_format_name(format)));
    }
    if (data.size() < vtf_image_size(format, width, height)) {
        throw FormatError("image data too short for " + std::to_string(width) + "x" + std::to_string(height) + " " +
                          std::string(vtf_format_name(format)));
    }
    if (vtf_format_is_compressed(format)) {
        return decode_blocks(format, data, width, height);
    }

    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t stride = vtf_image_size(format, 1, 1);
    std::vector<std::uint8_t> out(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        const Rgba p = decode_pixel(format, data.data() + i * stride);
        for (int c = 0; c < 4; ++c) {
            out[i * 4 + static_cast<std::size_t>(c)] = p[static_cast<std::size_t>(c)];
        }
    }
    return out;
}

} // namespace openbl::formats
