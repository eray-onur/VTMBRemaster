#pragma once

// Builds synthetic VTF files and .tth/.ttz pairs for tests.

#include "openbl/formats/vtf.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace openbl::test {

struct VtfSpec {
    std::uint16_t width = 16;
    std::uint16_t height = 16;
    formats::VtfFormat format = formats::VtfFormat::DXT1;
    std::uint8_t mip_count = 5;
    std::uint16_t frames = 1;
    std::uint32_t flags = 0;
    formats::VtfFormat thumbnail_format = formats::VtfFormat::DXT1;
    std::uint8_t thumbnail_size = 4;
};

inline void put_u16(std::vector<std::byte>& out, std::size_t at, std::uint16_t v) {
    out[at] = static_cast<std::byte>(v & 0xFF);
    out[at + 1] = static_cast<std::byte>(v >> 8);
}

inline void put_u32(std::vector<std::byte>& out, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out[at + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
    }
}

// Byte value every image is filled with, so tests can check layout.
inline std::byte fill_value(std::uint32_t mip, std::uint32_t frame, std::uint32_t face) {
    return static_cast<std::byte>(0x10 * (mip + 1) + 4 * frame + face);
}

struct BuiltVtf {
    std::vector<std::byte> data;
    // VTF end offset after the header + thumbnail and after each mip, smallest first.
    std::vector<std::uint32_t> boundaries;
};

inline BuiltVtf build_vtf(const VtfSpec& s) {
    using formats::vtf_image_size;
    BuiltVtf out;
    std::vector<std::byte>& d = out.data;
    d.resize(64);
    const char magic[4] = {'V', 'T', 'F', 0};
    std::memcpy(d.data(), magic, 4);
    put_u32(d, 4, 7);
    put_u32(d, 8, 1);
    put_u32(d, 12, 64);
    put_u16(d, 16, s.width);
    put_u16(d, 18, s.height);
    put_u32(d, 20, s.flags);
    put_u16(d, 24, s.frames);
    put_u32(d, 48, 0x3F800000); // bump scale 1.0
    put_u32(d, 52, static_cast<std::uint32_t>(s.format));
    d[56] = static_cast<std::byte>(s.mip_count);
    put_u32(d, 57, static_cast<std::uint32_t>(s.thumbnail_format));
    d[61] = static_cast<std::byte>(s.thumbnail_size);
    d[62] = static_cast<std::byte>(s.thumbnail_size);
    if (s.thumbnail_format != formats::VtfFormat::None) {
        d.resize(d.size() + vtf_image_size(s.thumbnail_format, s.thumbnail_size, s.thumbnail_size), std::byte{0xAA});
    }
    out.boundaries.push_back(static_cast<std::uint32_t>(d.size()));

    const std::uint32_t faces = (s.flags & formats::vtf_flags::EnvMap) ? 7 : 1;
    for (std::uint32_t mip = s.mip_count; mip-- > 0;) {
        const auto w = std::max<std::uint32_t>(1, s.width >> mip);
        const auto h = std::max<std::uint32_t>(1, s.height >> mip);
        for (std::uint32_t frame = 0; frame < s.frames; ++frame) {
            for (std::uint32_t face = 0; face < faces; ++face) {
                d.resize(d.size() + vtf_image_size(s.format, w, h), fill_value(mip, frame, face));
            }
        }
        out.boundaries.push_back(static_cast<std::uint32_t>(d.size()));
    }
    return out;
}

struct BuiltTth {
    std::vector<std::byte> tth;
    std::vector<std::byte> ttz;
};

// Splits a VTF into .tth/.ttz the way the original tools do: the header,
// thumbnail and `resident` smallest mips stay in the .tth, the rest is one
// zlib stream with a sync flush after each mip.
inline BuiltTth build_tth(const BuiltVtf& vtf, std::uint8_t resident) {
    const auto mips = static_cast<std::uint8_t>(vtf.boundaries.size() - 1);
    const std::uint32_t prefix = vtf.boundaries[resident];

    BuiltTth out;
    std::vector<std::uint32_t> ttz_ends(vtf.boundaries.size(), 0);
    if (prefix < vtf.data.size()) {
        z_stream zs{};
        deflateInit(&zs, Z_BEST_COMPRESSION);
        out.ttz.resize(deflateBound(&zs, static_cast<uLong>(vtf.data.size())) + 64 * vtf.boundaries.size());
        zs.next_out = reinterpret_cast<Bytef*>(out.ttz.data());
        zs.avail_out = static_cast<uInt>(out.ttz.size());
        for (std::size_t i = resident + 1u; i < vtf.boundaries.size(); ++i) {
            zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(vtf.data.data() + vtf.boundaries[i - 1]));
            zs.avail_in = vtf.boundaries[i] - vtf.boundaries[i - 1];
            deflate(&zs, i + 1 == vtf.boundaries.size() ? Z_FINISH : Z_SYNC_FLUSH);
            ttz_ends[i] = static_cast<std::uint32_t>(zs.total_out);
        }
        out.ttz.resize(zs.total_out);
        deflateEnd(&zs);
    }

    std::vector<std::byte>& t = out.tth;
    t.resize(12 + 8 * vtf.boundaries.size());
    const char magic[4] = {'T', 'T', 'H', 0};
    std::memcpy(t.data(), magic, 4);
    t[4] = std::byte{1};
    t[5] = std::byte{0};
    t[6] = static_cast<std::byte>(mips);
    t[7] = static_cast<std::byte>(resident);
    put_u32(t, 8, prefix);
    for (std::size_t i = 0; i < vtf.boundaries.size(); ++i) {
        put_u32(t, 12 + 8 * i, vtf.boundaries[i]);
        put_u32(t, 16 + 8 * i, ttz_ends[i]);
    }
    t.insert(t.end(), vtf.data.begin(), vtf.data.begin() + prefix);
    return out;
}

} // namespace openbl::test
