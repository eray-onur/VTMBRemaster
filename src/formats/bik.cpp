#include "openbl/formats/bik.hpp"

#include "binary_reader.hpp"

namespace openbl::formats {

// Layout: "BIK" + revision, u32 file size - 8, u32 frame count, u32 largest
// frame, u32 (unused), u32 width, u32 height, u32 fps numerator and
// denominator, u32 flags, u32 audio track count; per track u32 max decoded
// size; per track u16 sample rate, u16 flags; per track u32 id; then
// frame count + 1 u32 offsets (bit 0 marks keyframes; the last is the file size).
BinkFile parse_bik(std::span<const std::byte> data) {
    detail::BinaryReader r(data, "BIK");
    const auto sig = r.bytes(4);
    if (sig[0] != std::byte{'B'} || sig[1] != std::byte{'I'} || sig[2] != std::byte{'K'}) {
        r.fail("missing BIK signature");
    }
    BinkFile bik;
    bik.revision = static_cast<char>(sig[3]);
    const std::uint32_t size = r.u32();
    if (std::uint64_t{size} + 8 != data.size()) {
        r.fail("file size does not match the header");
    }
    const std::uint32_t frames = r.u32();
    bik.largest_frame = r.u32();
    r.u32();
    bik.width = r.u32();
    bik.height = r.u32();
    bik.fps_num = r.u32();
    bik.fps_den = r.u32();
    bik.flags = r.u32();
    const std::uint32_t tracks = r.u32();
    if (frames == 0 || frames > 1'000'000 || bik.width == 0 || bik.width > 8192 || bik.height == 0 ||
        bik.height > 8192 || bik.fps_den == 0 || tracks > 256) {
        r.fail("implausible header values");
    }
    bik.audio.resize(tracks);
    for (auto& t : bik.audio) {
        t.max_size = r.u32();
    }
    for (auto& t : bik.audio) {
        t.sample_rate = r.u16();
        t.flags = r.u16();
    }
    for (auto& t : bik.audio) {
        t.id = r.u32();
    }
    std::vector<std::uint32_t> offsets(frames + 1);
    for (auto& o : offsets) {
        o = r.u32();
    }
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::uint32_t start = offsets[i] & ~1u;
        const std::uint32_t end = offsets[i + 1] & ~1u;
        if (start < r.pos() || end < start || end > data.size()) {
            r.fail("frame index out of order");
        }
        bik.frames.push_back({start, end - start, (offsets[i] & 1u) != 0});
    }
    if ((offsets.back() & ~1u) != data.size()) {
        r.fail("frame index does not end at the file size");
    }
    return bik;
}

} // namespace openbl::formats
