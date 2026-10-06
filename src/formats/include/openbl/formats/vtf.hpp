#pragma once

// VTF textures as embedded in Bloodlines .tth/.ttz pairs (VTF version 7.1).
// See docs/formats/textures.md.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openbl::formats {

enum class VtfFormat : std::int32_t {
    None = -1,
    RGBA8888 = 0,
    ABGR8888 = 1,
    RGB888 = 2,
    BGR888 = 3,
    RGB565 = 4,
    I8 = 5,
    IA88 = 6,
    P8 = 7,
    A8 = 8,
    RGB888_BlueScreen = 9,
    BGR888_BlueScreen = 10,
    ARGB8888 = 11,
    BGRA8888 = 12,
    DXT1 = 13,
    DXT3 = 14,
    DXT5 = 15,
    BGRX8888 = 16,
    BGR565 = 17,
    BGRX5551 = 18,
    BGRA4444 = 19,
    DXT1_OneBitAlpha = 20,
    BGRA5551 = 21,
    UV88 = 22,
    UVWQ8888 = 23,
    RGBA16161616F = 24,
    RGBA16161616 = 25,
    UVLX8888 = 26,
};

std::string_view vtf_format_name(VtfFormat format);
bool vtf_format_is_compressed(VtfFormat format);

// Byte size of one image of the given format and dimensions. Throws FormatError for unknown formats.
std::size_t vtf_image_size(VtfFormat format, std::uint32_t width, std::uint32_t height);

namespace vtf_flags {
constexpr std::uint32_t EnvMap = 0x4000;
}

struct VtfHeader {
    std::uint32_t version_major = 0;
    std::uint32_t version_minor = 0;
    std::uint32_t header_size = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t flags = 0;
    std::uint16_t frame_count = 0;
    std::uint16_t first_frame = 0;
    float reflectivity[3] = {};
    float bump_scale = 0;
    VtfFormat format = VtfFormat::None;
    std::uint8_t mip_count = 0;
    VtfFormat thumbnail_format = VtfFormat::None;
    std::uint8_t thumbnail_width = 0;
    std::uint8_t thumbnail_height = 0;
    std::uint16_t depth = 1; // Present from 7.2; always 1 in 7.0/7.1.
};

class VtfTexture {
public:
    // Parses a VTF file. Truncated image data is accepted: mips that are not
    // fully present are reported as unavailable. Throws FormatError otherwise.
    static VtfTexture parse(std::vector<std::byte> data);

    const VtfHeader& header() const { return header_; }

    // Cubemaps in VTF 7.0-7.4 store 7 faces: 6 cube faces plus a sphere map.
    std::uint32_t face_count() const { return face_count_; }

    std::uint32_t mip_width(std::uint32_t mip) const;
    std::uint32_t mip_height(std::uint32_t mip) const;

    // Mip 0 is the largest. Mips are stored smallest first, so missing data
    // always removes the largest mips. Returns mip_count when nothing is present.
    std::uint32_t largest_available_mip() const { return largest_available_mip_; }
    bool has_mip(std::uint32_t mip) const { return mip < header_.mip_count && mip >= largest_available_mip_; }

    // Throws FormatError if the image is out of range or not present.
    std::span<const std::byte> image(std::uint32_t mip, std::uint32_t frame = 0, std::uint32_t face = 0) const;
    std::span<const std::byte> thumbnail() const;

    // Size of a complete file with this header.
    std::size_t expected_size() const;

private:
    std::size_t image_offset(std::uint32_t mip, std::uint32_t frame, std::uint32_t face) const;

    std::vector<std::byte> data_;
    VtfHeader header_;
    std::uint32_t face_count_ = 1;
    std::uint32_t largest_available_mip_ = 0;
    std::size_t thumbnail_size_ = 0;
};

} // namespace openbl::formats
