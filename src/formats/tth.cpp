#include "openbl/formats/tth.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"

#include <zlib.h>

#include <limits>
#include <string>

namespace openbl::formats {

namespace {

// Raw-inflates a zlib stream into `out` and verifies its Adler-32 trailer.
// Returns the number of bytes written and whether the stream matched `out` exactly.
struct InflateResult {
    std::size_t produced = 0;
    bool exact = false;
};

InflateResult inflate_zlib(std::span<const std::byte> in, std::span<std::byte> out) {
    InflateResult result;
    if (in.size() < 2 || in.size() > std::numeric_limits<uInt>::max() ||
        out.size() > std::numeric_limits<uInt>::max()) {
        return result;
    }
    const auto cmf = static_cast<unsigned>(in[0]);
    const auto flg = static_cast<unsigned>(in[1]);
    if ((cmf & 0x0F) != Z_DEFLATED || (cmf * 256 + flg) % 31 != 0 || (flg & 0x20) != 0) {
        return result;
    }

    // Inflate raw deflate data (windowBits < 0) so that a bad checksum in the
    // shipped data does not discard otherwise usable output.
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return result;
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data() + 2));
    zs.avail_in = static_cast<uInt>(in.size() - 2);
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    int ret = Z_OK;
    while (ret == Z_OK && zs.avail_out > 0 && zs.avail_in > 0) {
        ret = inflate(&zs, Z_NO_FLUSH);
    }
    if (ret == Z_OK && zs.avail_out == 0) {
        // Output is full; the stream must end exactly here.
        ret = inflate(&zs, Z_NO_FLUSH);
    }
    result.produced = out.size() - zs.avail_out;
    if (ret == Z_STREAM_END && result.produced == out.size() && zs.avail_in >= 4) {
        const auto* t = reinterpret_cast<const unsigned char*>(zs.next_in);
        const uLong stored = (uLong{t[0]} << 24) | (uLong{t[1]} << 16) | (uLong{t[2]} << 8) | uLong{t[3]};
        const uLong actual =
            adler32(adler32(0, nullptr, 0), reinterpret_cast<const Bytef*>(out.data()), static_cast<uInt>(out.size()));
        result.exact = stored == actual;
    }
    inflateEnd(&zs);
    return result;
}

} // namespace

TthHeader parse_tth_header(std::span<const std::byte> tth) {
    detail::BinaryReader in(tth, "TTH");
    TthHeader h;

    const auto magic = in.bytes(4);
    if (magic[0] != std::byte{'T'} || magic[1] != std::byte{'T'} || magic[2] != std::byte{'H'} ||
        magic[3] != std::byte{0}) {
        in.fail("missing TTH signature");
    }
    h.version_major = in.u8();
    h.version_minor = in.u8();
    if (h.version_major != 1) {
        in.fail("unsupported version " + std::to_string(h.version_major) + "." + std::to_string(h.version_minor));
    }
    h.mip_count = in.u8();
    h.resident_mip_count = in.u8();
    if (h.mip_count == 0 || h.resident_mip_count > h.mip_count) {
        in.fail("bad mip counts");
    }
    h.vtf_prefix_size = in.u32();

    h.ranges.resize(static_cast<std::size_t>(h.mip_count) + 1);
    for (std::size_t i = 0; i < h.ranges.size(); ++i) {
        h.ranges[i].vtf_end = in.u32();
        h.ranges[i].ttz_end = in.u32();
        if (i > 0 && (h.ranges[i].vtf_end < h.ranges[i - 1].vtf_end || h.ranges[i].ttz_end < h.ranges[i - 1].ttz_end)) {
            in.fail("mip ranges are not ascending");
        }
    }
    if (h.ranges[h.resident_mip_count].vtf_end != h.vtf_prefix_size) {
        in.fail("VTF prefix size does not match the resident mips");
    }
    h.vtf_offset = in.pos();
    if (in.remaining() < h.vtf_prefix_size) {
        in.fail("truncated VTF prefix");
    }
    // Any bytes after the prefix are uninitialised padding from the original tools.
    return h;
}

AssembledVtf assemble_vtf(std::span<const std::byte> tth, std::optional<std::span<const std::byte>> ttz) {
    const TthHeader h = parse_tth_header(tth);
    AssembledVtf out;
    const auto prefix = tth.subspan(h.vtf_offset, h.vtf_prefix_size);
    out.data.assign(prefix.begin(), prefix.end());
    if (!h.needs_ttz()) {
        return out;
    }
    if (!ttz) {
        out.damage = TextureDamage::MissingTtz;
        return out;
    }

    out.data.resize(h.vtf_size());
    const auto result = inflate_zlib(*ttz, std::span(out.data).subspan(h.vtf_prefix_size));
    out.data.resize(h.vtf_prefix_size + result.produced);
    if (!result.exact) {
        out.damage = TextureDamage::CorruptTtz;
    }
    return out;
}

} // namespace openbl::formats
