#include "openbl/rhi/rhi.hpp"

#include <algorithm>

namespace openbl::rhi {

bool format_is_block_compressed(Format format) {
    switch (format) {
    case Format::BC1_UNorm:
    case Format::BC1_sRGB:
    case Format::BC2_UNorm:
    case Format::BC2_sRGB:
    case Format::BC3_UNorm:
    case Format::BC3_sRGB:
        return true;
    default:
        return false;
    }
}

std::size_t format_image_size(Format format, std::uint32_t width, std::uint32_t height) {
    const std::size_t blocks = std::size_t{std::max(1u, (width + 3) / 4)} * std::max(1u, (height + 3) / 4);
    const std::size_t pixels = std::size_t{width} * height;
    switch (format) {
    case Format::BC1_UNorm:
    case Format::BC1_sRGB:
        return blocks * 8;
    case Format::BC2_UNorm:
    case Format::BC2_sRGB:
    case Format::BC3_UNorm:
    case Format::BC3_sRGB:
        return blocks * 16;
    case Format::RGBA8_UNorm:
    case Format::RGBA8_sRGB:
    case Format::BGRA8_UNorm:
    case Format::BGRA8_sRGB:
    case Format::D32_Float:
        return pixels * 4;
    case Format::RG32_Float:
    case Format::RGBA16_Float:
        return pixels * 8;
    case Format::RGB32_Float:
        return pixels * 12;
    case Format::RGBA32_Float:
        return pixels * 16;
    case Format::Undefined:
        break;
    }
    return 0;
}

} // namespace openbl::rhi
