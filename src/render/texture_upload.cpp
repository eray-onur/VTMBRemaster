#include "openbl/render/texture_upload.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/image_decode.hpp"

#include <optional>
#include <vector>

namespace openbl::render {

namespace {

std::optional<rhi::Format> native_format(formats::VtfFormat f, bool srgb) {
    using formats::VtfFormat;
    switch (f) {
    case VtfFormat::DXT1:
    case VtfFormat::DXT1_OneBitAlpha:
        return srgb ? rhi::Format::BC1_sRGB : rhi::Format::BC1_UNorm;
    case VtfFormat::DXT3:
        return srgb ? rhi::Format::BC2_sRGB : rhi::Format::BC2_UNorm;
    case VtfFormat::DXT5:
        return srgb ? rhi::Format::BC3_sRGB : rhi::Format::BC3_UNorm;
    case VtfFormat::RGBA8888:
        return srgb ? rhi::Format::RGBA8_sRGB : rhi::Format::RGBA8_UNorm;
    case VtfFormat::BGRA8888:
        return srgb ? rhi::Format::BGRA8_sRGB : rhi::Format::BGRA8_UNorm;
    default:
        return std::nullopt;
    }
}

} // namespace

std::unique_ptr<rhi::Texture> upload_vtf(rhi::Device& device, const formats::VtfTexture& vtf, bool srgb, std::uint32_t frame) {
    const auto& h = vtf.header();
    const std::uint32_t first = vtf.largest_available_mip();
    if (first >= h.mip_count) {
        throw formats::FormatError("texture has no image data");
    }
    rhi::TextureDesc desc;
    desc.width = vtf.mip_width(first);
    desc.height = vtf.mip_height(first);
    desc.mip_levels = h.mip_count - first;
    // Cube maps: the first six faces (the seventh is a sphere map). VTF faces
    // are stored in the order +X, -X, +Y, -Y, +Z, -Z of Source's world axes,
    // which a cube lookup with a world-space direction uses directly.
    desc.cube = vtf.face_count() >= 6 && desc.width == desc.height;
    const std::uint32_t faces = desc.cube ? 6 : 1;

    std::vector<std::span<const std::byte>> mips;
    std::vector<std::vector<std::uint8_t>> decoded;
    const auto native = native_format(h.format, srgb);
    desc.format = native ? *native : srgb ? rhi::Format::RGBA8_sRGB : rhi::Format::RGBA8_UNorm;
    for (std::uint32_t m = first; m < h.mip_count; ++m) {
        if (native && faces == 1) {
            mips.push_back(vtf.image(m, frame));
            continue;
        }
        // Decoded, or faces gathered into one block per mip.
        std::vector<std::uint8_t> block;
        for (std::uint32_t f = 0; f < faces; ++f) {
            const auto image = vtf.image(m, frame, f);
            if (native) {
                const auto* p = reinterpret_cast<const std::uint8_t*>(image.data());
                block.insert(block.end(), p, p + image.size());
            } else {
                const auto rgba = formats::decode_to_rgba8(h.format, image, vtf.mip_width(m), vtf.mip_height(m));
                block.insert(block.end(), rgba.begin(), rgba.end());
            }
        }
        decoded.push_back(std::move(block));
    }
    for (const auto& block : decoded) {
        mips.push_back(std::as_bytes(std::span(block)));
    }
    return device.create_texture(desc, mips);
}

std::unique_ptr<rhi::Texture> create_checker_texture(rhi::Device& device) {
    constexpr std::uint32_t kSize = 64;
    std::vector<std::uint8_t> pixels(kSize * kSize * 4);
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const bool on = ((x / 8) + (y / 8)) % 2 == 0;
            const std::size_t i = (y * kSize + x) * 4;
            pixels[i + 0] = on ? 255 : 40;
            pixels[i + 1] = on ? 0 : 40;
            pixels[i + 2] = on ? 255 : 40;
            pixels[i + 3] = 255;
        }
    }
    const std::span<const std::byte> mip = std::as_bytes(std::span(pixels));
    return device.create_texture({kSize, kSize, 1, rhi::Format::RGBA8_sRGB}, std::span(&mip, 1));
}

} // namespace openbl::render
