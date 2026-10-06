#include "openbl/formats/text_encoding.hpp"

#include <array>
#include <cstdint>

namespace openbl::formats {
namespace {

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Windows-1252 0x80-0x9F; the five undefined bytes map to their C1 controls.
constexpr std::array<char16_t, 32> kCp1252High = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

} // namespace

std::string utf16le_to_utf8(std::span<const std::byte> data) {
    std::string out;
    out.reserve(data.size() / 2);
    const std::size_t units = data.size() / 2;
    auto unit = [&](std::size_t i) {
        return static_cast<char16_t>(std::to_integer<std::uint16_t>(data[2 * i]) |
                                     (std::to_integer<std::uint16_t>(data[2 * i + 1]) << 8));
    };
    for (std::size_t i = 0; i < units; ++i) {
        const char16_t u = unit(i);
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < units) {
            const char16_t low = unit(i + 1);
            if (low >= 0xDC00 && low < 0xE000) {
                append_utf8(out, 0x10000 + ((char32_t{u} - 0xD800) << 10) + (low - 0xDC00));
                ++i;
                continue;
            }
        }
        append_utf8(out, (u >= 0xD800 && u < 0xE000) ? char32_t{0xFFFD} : char32_t{u});
    }
    return out;
}

std::string cp1252_to_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const auto b = static_cast<unsigned char>(ch);
        append_utf8(out, b >= 0x80 && b < 0xA0 ? char32_t{kCp1252High[b - 0x80]} : char32_t{b});
    }
    return out;
}

} // namespace openbl::formats
