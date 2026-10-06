#include "openbl/formats/image_read.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace openbl::formats {

namespace {

std::uint32_t load_be32(const std::byte* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    return static_cast<std::uint8_t>(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
}

} // namespace

Image decode_png(std::span<const std::byte> data) {
    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (data.size() < 8 || std::memcmp(data.data(), kSignature, 8) != 0) {
        throw FormatError("PNG: bad signature");
    }
    Image img;
    std::uint8_t depth = 0, color = 0, interlace = 0;
    std::vector<std::byte> compressed;
    std::size_t pos = 8;
    while (pos + 12 <= data.size()) {
        const std::uint32_t length = load_be32(data.data() + pos);
        const std::string type(reinterpret_cast<const char*>(data.data() + pos + 4), 4);
        if (pos + 12 + length > data.size()) {
            throw FormatError("PNG: truncated chunk");
        }
        const std::byte* body = data.data() + pos + 8;
        if (type == "IHDR" && length >= 13) {
            img.width = load_be32(body);
            img.height = load_be32(body + 4);
            depth = static_cast<std::uint8_t>(body[8]);
            color = static_cast<std::uint8_t>(body[9]);
            interlace = static_cast<std::uint8_t>(body[12]);
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), body, body + length);
        } else if (type == "IEND") {
            break;
        }
        pos += 12 + length;
    }
    if (depth != 8 || interlace != 0 || (color != 0 && color != 2 && color != 6)) {
        throw FormatError("PNG: only 8-bit non-interlaced greyscale, RGB or RGBA is supported");
    }
    if (img.width == 0 || img.height == 0 || img.width > 32768 || img.height > 32768) {
        throw FormatError("PNG: bad dimensions");
    }
    const std::size_t channels = color == 0 ? 1 : (color == 2 ? 3 : 4);
    const std::size_t stride = img.width * channels;
    std::vector<std::uint8_t> raw((stride + 1) * img.height);
    uLongf raw_size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(compressed.data()),
                   static_cast<uLong>(compressed.size())) != Z_OK ||
        raw_size != raw.size()) {
        throw FormatError("PNG: corrupt image data");
    }

    std::vector<std::uint8_t> pixels(stride * img.height);
    for (std::size_t y = 0; y < img.height; ++y) {
        const std::uint8_t filter = raw[y * (stride + 1)];
        const std::uint8_t* in = &raw[y * (stride + 1) + 1];
        std::uint8_t* out = &pixels[y * stride];
        const std::uint8_t* prev = y > 0 ? &pixels[(y - 1) * stride] : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= channels ? out[x - channels] : 0;
            const int b = prev ? prev[x] : 0;
            const int c = (prev && x >= channels) ? prev[x - channels] : 0;
            int v = in[x];
            switch (filter) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: throw FormatError("PNG: bad filter type");
            }
            out[x] = static_cast<std::uint8_t>(v);
        }
    }
    img.rgba.resize(std::size_t{img.width} * img.height * 4);
    for (std::size_t i = 0; i < std::size_t{img.width} * img.height; ++i) {
        const std::uint8_t* p = &pixels[i * channels];
        img.rgba[i * 4 + 0] = p[0];
        img.rgba[i * 4 + 1] = channels >= 3 ? p[1] : p[0];
        img.rgba[i * 4 + 2] = channels >= 3 ? p[2] : p[0];
        img.rgba[i * 4 + 3] = channels == 4 ? p[3] : 255;
    }
    return img;
}

Image decode_tga(std::span<const std::byte> data) {
    detail::BinaryReader in(data, "TGA");
    const std::uint8_t id_length = in.u8();
    const std::uint8_t colormap = in.u8();
    const std::uint8_t type = in.u8();
    in.bytes(9); // colour map spec and origin
    Image img;
    img.width = in.u16();
    img.height = in.u16();
    const std::uint8_t bpp = in.u8();
    const std::uint8_t descriptor = in.u8();
    if (colormap != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32)) {
        in.fail("only 24/32-bit true-colour TGA is supported");
    }
    if (img.width == 0 || img.height == 0) {
        in.fail("bad dimensions");
    }
    in.bytes(id_length);
    const std::size_t pixel_size = bpp / 8;
    const std::size_t count = std::size_t{img.width} * img.height;
    std::vector<std::uint8_t> bgra(count * 4, 255);
    auto read_pixel = [&](std::size_t i) {
        const auto p = in.bytes(pixel_size);
        for (std::size_t c = 0; c < pixel_size; ++c) {
            bgra[i * 4 + c] = static_cast<std::uint8_t>(p[c]);
        }
    };
    if (type == 2) {
        for (std::size_t i = 0; i < count; ++i) {
            read_pixel(i);
        }
    } else {
        std::size_t i = 0;
        while (i < count) {
            const std::uint8_t header = in.u8();
            const std::size_t run = (header & 0x7F) + 1u;
            if (i + run > count) {
                in.fail("RLE run past the end of the image");
            }
            if (header & 0x80) {
                read_pixel(i);
                for (std::size_t k = 1; k < run; ++k) {
                    std::memcpy(&bgra[(i + k) * 4], &bgra[i * 4], 4);
                }
            } else {
                for (std::size_t k = 0; k < run; ++k) {
                    read_pixel(i + k);
                }
            }
            i += run;
        }
    }
    const bool top_left = (descriptor & 0x20) != 0;
    img.rgba.resize(count * 4);
    for (std::size_t y = 0; y < img.height; ++y) {
        const std::size_t src_row = top_left ? y : img.height - 1 - y;
        for (std::size_t x = 0; x < img.width; ++x) {
            const std::uint8_t* s = &bgra[(src_row * img.width + x) * 4];
            std::uint8_t* d = &img.rgba[(y * img.width + x) * 4];
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
        }
    }
    return img;
}

Image read_image(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("cannot open " + file.string());
    }
    std::vector<std::byte> data(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (data.size() >= 8 && data[1] == std::byte{'P'} && data[2] == std::byte{'N'} && data[3] == std::byte{'G'}) {
        return decode_png(data);
    }
    std::string ext = file.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    if (ext == ".tga") {
        return decode_tga(data);
    }
    throw FormatError(file.string() + ": unsupported image format (use PNG or TGA)");
}

} // namespace openbl::formats
