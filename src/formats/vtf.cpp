#include "openbl/formats/vtf.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"

#include <algorithm>
#include <string>

namespace openbl::formats {

namespace {

constexpr std::uint32_t kMaxMips = 16;
constexpr std::uint32_t kCubemapFaces = 7; // 6 faces + sphere map, verified for every Bloodlines cubemap.

struct FormatInfo {
    std::string_view name;
    std::uint32_t bits_per_pixel; // For block formats: bytes per 4x4 block.
    bool block_compressed;
};

const FormatInfo* format_info(VtfFormat format) {
    static constexpr FormatInfo kFormats[] = {
        {"RGBA8888", 32, false},       {"ABGR8888", 32, false},      {"RGB888", 24, false},
        {"BGR888", 24, false},         {"RGB565", 16, false},        {"I8", 8, false},
        {"IA88", 16, false},           {"P8", 8, false},             {"A8", 8, false},
        {"RGB888_BlueScreen", 24, false}, {"BGR888_BlueScreen", 24, false}, {"ARGB8888", 32, false},
        {"BGRA8888", 32, false},       {"DXT1", 8, true},            {"DXT3", 16, true},
        {"DXT5", 16, true},            {"BGRX8888", 32, false},      {"BGR565", 16, false},
        {"BGRX5551", 16, false},       {"BGRA4444", 16, false},      {"DXT1_OneBitAlpha", 8, true},
        {"BGRA5551", 16, false},       {"UV88", 16, false},          {"UVWQ8888", 32, false},
        {"RGBA16161616F", 64, false},  {"RGBA16161616", 64, false},  {"UVLX8888", 32, false},
    };
    const auto index = static_cast<std::int32_t>(format);
    if (index < 0 || index >= static_cast<std::int32_t>(std::size(kFormats))) {
        return nullptr;
    }
    return &kFormats[index];
}

} // namespace

std::string_view vtf_format_name(VtfFormat format) {
    if (format == VtfFormat::None) {
        return "None";
    }
    const FormatInfo* info = format_info(format);
    return info ? info->name : "Unknown";
}

bool vtf_format_is_compressed(VtfFormat format) {
    const FormatInfo* info = format_info(format);
    return info && info->block_compressed;
}

std::size_t vtf_image_size(VtfFormat format, std::uint32_t width, std::uint32_t height) {
    const FormatInfo* info = format_info(format);
    if (!info) {
        throw FormatError("unknown VTF image format " + std::to_string(static_cast<std::int32_t>(format)));
    }
    if (info->block_compressed) {
        const std::size_t blocks_x = std::max<std::size_t>(1, (width + 3) / 4);
        const std::size_t blocks_y = std::max<std::size_t>(1, (height + 3) / 4);
        return blocks_x * blocks_y * info->bits_per_pixel;
    }
    return static_cast<std::size_t>(width) * height * info->bits_per_pixel / 8;
}

VtfTexture VtfTexture::parse(std::vector<std::byte> data) {
    VtfTexture tex;
    tex.data_ = std::move(data);
    detail::BinaryReader in(tex.data_, "VTF");
    VtfHeader& h = tex.header_;

    const auto magic = in.bytes(4);
    if (magic[0] != std::byte{'V'} || magic[1] != std::byte{'T'} || magic[2] != std::byte{'F'} ||
        magic[3] != std::byte{0}) {
        in.fail("missing VTF signature");
    }
    h.version_major = in.u32();
    h.version_minor = in.u32();
    if (h.version_major != 7 || h.version_minor > 2) {
        in.fail("unsupported version " + std::to_string(h.version_major) + "." + std::to_string(h.version_minor));
    }
    h.header_size = in.u32();
    h.width = in.u16();
    h.height = in.u16();
    h.flags = in.u32();
    h.frame_count = in.u16();
    h.first_frame = in.u16();
    in.bytes(4);
    for (float& r : h.reflectivity) {
        r = in.f32();
    }
    in.bytes(4);
    h.bump_scale = in.f32();
    h.format = static_cast<VtfFormat>(in.i32());
    h.mip_count = in.u8();
    h.thumbnail_format = static_cast<VtfFormat>(in.i32());
    h.thumbnail_width = in.u8();
    h.thumbnail_height = in.u8();
    if (h.version_minor >= 2) {
        h.depth = in.u16();
    }

    if (h.header_size < in.pos() || h.header_size > tex.data_.size()) {
        in.fail("bad header size " + std::to_string(h.header_size));
    }
    if (h.width == 0 || h.height == 0 || h.frame_count == 0 || h.depth == 0) {
        in.fail("zero dimension, frame count or depth");
    }
    if (h.mip_count == 0 || h.mip_count > kMaxMips) {
        in.fail("bad mip count " + std::to_string(h.mip_count));
    }
    if (!format_info(h.format)) {
        in.fail("unknown image format " + std::to_string(static_cast<std::int32_t>(h.format)));
    }
    if (h.thumbnail_format != VtfFormat::None) {
        if (!format_info(h.thumbnail_format)) {
            in.fail("unknown thumbnail format");
        }
        tex.thumbnail_size_ = vtf_image_size(h.thumbnail_format, h.thumbnail_width, h.thumbnail_height);
    }
    tex.face_count_ = (h.flags & vtf_flags::EnvMap) ? kCubemapFaces : 1;

    // Mips are stored smallest first; find how many are completely present.
    std::size_t end = h.header_size + tex.thumbnail_size_;
    if (end > tex.data_.size()) {
        in.fail("truncated before image data");
    }
    tex.largest_available_mip_ = h.mip_count;
    for (std::uint32_t mip = h.mip_count; mip-- > 0;) {
        const std::size_t depth = std::max<std::uint32_t>(1, h.depth >> mip);
        end += vtf_image_size(h.format, tex.mip_width(mip), tex.mip_height(mip)) * h.frame_count *
               tex.face_count_ * depth;
        if (end > tex.data_.size()) {
            break;
        }
        tex.largest_available_mip_ = mip;
    }
    return tex;
}

std::uint32_t VtfTexture::mip_width(std::uint32_t mip) const {
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(header_.width) >> mip);
}

std::uint32_t VtfTexture::mip_height(std::uint32_t mip) const {
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(header_.height) >> mip);
}

std::size_t VtfTexture::image_offset(std::uint32_t mip, std::uint32_t frame, std::uint32_t face) const {
    std::size_t offset = header_.header_size + thumbnail_size_;
    for (std::uint32_t m = header_.mip_count - 1; m > mip; --m) {
        const std::size_t depth = std::max<std::uint32_t>(1, header_.depth >> m);
        offset += vtf_image_size(header_.format, mip_width(m), mip_height(m)) * header_.frame_count * face_count_ *
                  depth;
    }
    const std::size_t depth = std::max<std::uint32_t>(1, header_.depth >> mip);
    const std::size_t image_size = vtf_image_size(header_.format, mip_width(mip), mip_height(mip)) * depth;
    return offset + (static_cast<std::size_t>(frame) * face_count_ + face) * image_size;
}

std::span<const std::byte> VtfTexture::image(std::uint32_t mip, std::uint32_t frame, std::uint32_t face) const {
    if (mip >= header_.mip_count || frame >= header_.frame_count || face >= face_count_) {
        throw FormatError("VTF: image index out of range");
    }
    if (!has_mip(mip)) {
        throw FormatError("VTF: mip " + std::to_string(mip) + " is not present");
    }
    const std::size_t depth = std::max<std::uint32_t>(1, header_.depth >> mip);
    const std::size_t size = vtf_image_size(header_.format, mip_width(mip), mip_height(mip)) * depth;
    return std::span(data_).subspan(image_offset(mip, frame, face), size);
}

std::span<const std::byte> VtfTexture::thumbnail() const {
    return std::span(data_).subspan(header_.header_size, thumbnail_size_);
}

std::size_t VtfTexture::expected_size() const {
    return image_offset(0, 0, 0) +
           vtf_image_size(header_.format, header_.width, header_.height) * header_.depth * header_.frame_count *
               face_count_;
}

} // namespace openbl::formats
