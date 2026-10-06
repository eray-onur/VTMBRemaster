#include "openbl/formats/zip.hpp"

#include "binary_reader.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

#include <zlib.h>

#include <limits>

namespace openbl::formats {

namespace {

constexpr std::uint32_t kEndOfCentralDir = 0x06054b50;
constexpr std::uint32_t kCentralHeader = 0x02014b50;
constexpr std::uint32_t kLocalHeader = 0x04034b50;
constexpr std::size_t kEndRecordSize = 22;

} // namespace

ZipArchive::ZipArchive(std::span<const std::byte> data) : data_(data) {
    if (data.size() < kEndRecordSize) {
        throw FormatError("zip: too small");
    }
    // The end record is followed by a comment of up to 64 KiB.
    std::size_t end = std::string::npos;
    const std::size_t lowest = data.size() > 0xFFFF + kEndRecordSize ? data.size() - 0xFFFF - kEndRecordSize : 0;
    for (std::size_t pos = data.size() - kEndRecordSize + 1; pos-- > lowest;) {
        if (detail::load_u32(data.data() + pos) == kEndOfCentralDir) {
            end = pos;
            break;
        }
    }
    if (end == std::string::npos) {
        throw FormatError("zip: end of central directory not found");
    }
    detail::BinaryReader in(data, "zip");
    in.seek(end + 10);
    const std::uint16_t count = in.u16();
    in.u32(); // directory size
    const std::uint32_t dir_offset = in.u32();

    in.seek(dir_offset);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (in.u32() != kCentralHeader) {
            in.fail("bad central directory entry");
        }
        in.bytes(6); // versions, flags
        Entry e;
        e.method = in.u16();
        in.bytes(8); // time, date, crc
        e.compressed_size = in.u32();
        e.size = in.u32();
        const std::uint16_t name_length = in.u16();
        const std::uint16_t extra_length = in.u16();
        const std::uint16_t comment_length = in.u16();
        in.bytes(8); // disk, attributes
        e.local_header = in.u32();
        const auto name = in.bytes(name_length);
        e.path = normalize_game_path(std::string_view(reinterpret_cast<const char*>(name.data()), name.size()));
        in.bytes(std::size_t{extra_length} + comment_length);
        index_[e.path] = entries_.size();
        entries_.push_back(std::move(e));
    }
}

const ZipArchive::Entry* ZipArchive::find(std::string_view path) const {
    auto it = index_.find(normalize_game_path(path));
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::vector<std::byte> ZipArchive::read(const Entry& entry) const {
    detail::BinaryReader in(data_, "zip entry " + entry.path);
    in.seek(entry.local_header);
    if (in.u32() != kLocalHeader) {
        in.fail("bad local header");
    }
    in.seek(entry.local_header + 26);
    const std::uint16_t name_length = in.u16();
    const std::uint16_t extra_length = in.u16();
    in.bytes(std::size_t{name_length} + extra_length);
    const auto packed = in.bytes(entry.compressed_size);

    if (entry.method == 0) {
        if (entry.compressed_size != entry.size) {
            in.fail("stored entry sizes differ");
        }
        return {packed.begin(), packed.end()};
    }
    if (entry.method != 8) {
        in.fail("unsupported compression method " + std::to_string(entry.method));
    }
    std::vector<std::byte> out(entry.size);
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        in.fail("inflate init failed");
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(packed.data()));
    zs.avail_in = static_cast<uInt>(packed.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    const int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (ret != Z_STREAM_END || zs.avail_out != 0) {
        in.fail("corrupt deflate data");
    }
    return out;
}

} // namespace openbl::formats
