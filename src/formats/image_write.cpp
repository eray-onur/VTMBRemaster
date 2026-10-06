#include "openbl/formats/image_write.hpp"

#include <zlib.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace openbl::formats {

namespace {

void put_u32_be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
    }
}

void chunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
    put_u32_be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong crc = crc32(0, out.data() + start, static_cast<uInt>(out.size() - start));
    put_u32_be(out, static_cast<std::uint32_t>(crc));
}

} // namespace

void write_png(const std::filesystem::path& file, std::span<const std::uint8_t> rgba, std::uint32_t width,
               std::uint32_t height) {
    const std::size_t row = std::size_t{width} * 4;
    if (rgba.size() < row * height) {
        throw std::runtime_error("write_png: not enough pixel data");
    }
    std::vector<std::uint8_t> raw;
    raw.reserve((row + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                   rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * row));
    }
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> packed(packed_size);
    if (compress2(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        throw std::runtime_error("write_png: compression failed");
    }
    packed.resize(packed_size);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    put_u32_be(header, width);
    put_u32_be(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
    chunk(out, "IHDR", header);
    chunk(out, "IDAT", packed);
    chunk(out, "IEND", {});

    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        throw std::runtime_error("write_png: cannot write " + file.string());
    }
}

} // namespace openbl::formats
