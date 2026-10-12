#pragma once

#include "openbl/formats/vtf.hpp"
#include "openbl/rhi/rhi.hpp"

#include <memory>

namespace openbl::render {

// Uploads every mip that is present in the VTF, starting at the largest
// available one. Block-compressed formats are uploaded as-is; other formats
// are decoded to RGBA8 on the CPU. Texels stay as stored (UNORM): shaders
// decode colour themselves (shaders/color.hlsli). `frame` selects one frame of
// an animated texture.
// Throws formats::FormatError if no image data is present.
std::unique_ptr<rhi::Texture> upload_vtf(rhi::Device& device, const formats::VtfTexture& vtf, std::uint32_t frame = 0);

// A small two-colour checkerboard, used when a texture is missing.
std::unique_ptr<rhi::Texture> create_checker_texture(rhi::Device& device);

} // namespace openbl::render
