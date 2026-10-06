#pragma once

// Troika texture pairs: .tth (header + embedded VTF prefix) and .ttz
// (zlib-compressed remainder of the VTF). See docs/formats/textures.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openbl::formats {

struct TthMipRange {
    std::uint32_t vtf_end = 0; // End offset in the assembled VTF.
    std::uint32_t ttz_end = 0; // End offset in the .ttz stream; 0 for data held in the .tth.
};

struct TthHeader {
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;
    std::uint8_t mip_count = 0;
    std::uint8_t resident_mip_count = 0; // Smallest mips stored uncompressed in the .tth.
    std::uint32_t vtf_prefix_size = 0;   // Bytes of VTF stored in the .tth.
    // mip_count + 1 entries. [0] ends the VTF header and thumbnail; [i] ends
    // the (i-1)-th smallest mip, i.e. VTF mip (mip_count - i).
    std::vector<TthMipRange> ranges;
    std::size_t vtf_offset = 0; // Where the VTF prefix starts inside the .tth.

    std::uint32_t vtf_size() const { return ranges.back().vtf_end; }
    bool needs_ttz() const { return vtf_prefix_size < vtf_size(); }
};

// Throws FormatError.
TthHeader parse_tth_header(std::span<const std::byte> tth);

enum class TextureDamage {
    None,
    MissingTtz, // Large mips needed but no .ttz given.
    CorruptTtz, // .ttz did not decompress to exactly the expected bytes.
};

struct AssembledVtf {
    std::vector<std::byte> data; // Complete VTF, or a prefix of one if damaged.
    TextureDamage damage = TextureDamage::None;
};

// Rebuilds the VTF file from a .tth and its optional .ttz. Damaged .ttz data
// is tolerated and reported; only a malformed .tth throws FormatError.
AssembledVtf assemble_vtf(std::span<const std::byte> tth, std::optional<std::span<const std::byte>> ttz);

} // namespace openbl::formats
