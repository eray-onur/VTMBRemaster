#include "openbl/formats/vpk.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

#include <cstring>
#include <limits>

namespace openbl::formats {

namespace {

// Footer: u32 entry_count, u32 directory_offset, u8 kind. Little-endian.
constexpr std::uint64_t kFooterSize = 9;
// Fixed part of a directory entry: u32 name_length + u32 offset + u32 size.
constexpr std::uint64_t kEntryFixedSize = 12;
constexpr std::uint32_t kMaxNameLength = 4096;

std::uint32_t load_u32(const std::byte* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void store_u32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
    }
}

[[noreturn]] void fail(const std::filesystem::path& file, const std::string& what) {
    throw FormatError(file.string() + ": " + what);
}

} // namespace

std::unique_ptr<VpkArchive> VpkArchive::open(const std::filesystem::path& file) {
    std::unique_ptr<VpkArchive> archive(new VpkArchive());
    archive->file_path_ = file;
    archive->stream_.open(file, std::ios::binary);
    if (!archive->stream_) {
        fail(file, "cannot open file");
    }

    std::error_code ec;
    const std::uint64_t file_size = std::filesystem::file_size(file, ec);
    if (ec) {
        fail(file, "cannot query file size");
    }
    if (file_size < kFooterSize) {
        fail(file, "too small to contain a VPK footer");
    }
    if (file_size > std::numeric_limits<std::uint32_t>::max()) {
        fail(file, "larger than 4 GiB; offsets cannot address it");
    }

    std::ifstream& in = archive->stream_;
    std::byte footer[kFooterSize];
    in.seekg(static_cast<std::streamoff>(file_size - kFooterSize));
    in.read(reinterpret_cast<char*>(footer), kFooterSize);
    if (!in) {
        fail(file, "cannot read footer");
    }
    const std::uint32_t entry_count = load_u32(footer);
    const std::uint32_t dir_offset = load_u32(footer + 4);
    const auto kind_byte = static_cast<std::uint8_t>(footer[8]);
    if (kind_byte > 1) {
        fail(file, "unknown archive kind " + std::to_string(kind_byte));
    }
    archive->kind_ = static_cast<VpkKind>(kind_byte);

    const std::uint64_t dir_end = file_size - kFooterSize;
    if (dir_offset > dir_end) {
        fail(file, "directory offset beyond end of file");
    }
    const std::uint64_t dir_size = dir_end - dir_offset;
    if (entry_count > dir_size / kEntryFixedSize) {
        fail(file, "entry count does not fit in directory");
    }

    std::vector<std::byte> dir(static_cast<std::size_t>(dir_size));
    in.seekg(static_cast<std::streamoff>(dir_offset));
    in.read(reinterpret_cast<char*>(dir.data()), static_cast<std::streamsize>(dir.size()));
    if (!in) {
        fail(file, "cannot read directory");
    }

    archive->entries_.reserve(entry_count);
    archive->index_.reserve(entry_count);
    std::size_t pos = 0;
    for (std::uint32_t i = 0; i < entry_count; ++i) {
        if (dir.size() - pos < 4) {
            fail(file, "truncated directory at entry " + std::to_string(i));
        }
        const std::uint32_t name_length = load_u32(dir.data() + pos);
        pos += 4;
        if (name_length == 0 || name_length > kMaxNameLength || dir.size() - pos < name_length + 8ull) {
            fail(file, "bad name length at entry " + std::to_string(i));
        }
        std::string_view raw_name(reinterpret_cast<const char*>(dir.data() + pos), name_length);
        pos += name_length;

        VpkEntry entry;
        entry.path = normalize_game_path(raw_name);
        entry.offset = load_u32(dir.data() + pos);
        entry.size = load_u32(dir.data() + pos + 4);
        pos += 8;

        if (static_cast<std::uint64_t>(entry.offset) + entry.size > dir_offset) {
            fail(file, "data for '" + entry.path + "' overlaps directory or end of file");
        }
        // Later duplicates within one archive win, matching cross-archive override order.
        archive->index_[entry.path] = archive->entries_.size();
        archive->entries_.push_back(std::move(entry));
    }
    if (pos != dir.size()) {
        fail(file, "unexpected bytes between directory and footer");
    }
    return archive;
}

const VpkEntry* VpkArchive::find(std::string_view path) const {
    auto it = index_.find(normalize_game_path(path));
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::vector<std::byte> VpkArchive::read(const VpkEntry& entry) const {
    std::vector<std::byte> data(entry.size);
    if (entry.size == 0) {
        return data;
    }
    std::lock_guard lock(stream_mutex_);
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(entry.offset));
    stream_.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream_) {
        fail(file_path_, "cannot read data for '" + entry.path + "'");
    }
    return data;
}

void write_vpk(const std::filesystem::path& file, std::span<const VpkWriteEntry> entries, VpkKind kind) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail(file, "cannot create file");
    }

    std::vector<std::byte> dir;
    std::uint64_t offset = 0;
    for (const VpkWriteEntry& e : entries) {
        const std::string name = normalize_game_path(e.path);
        if (name.empty() || name.size() > kMaxNameLength) {
            fail(file, "invalid entry path '" + e.path + "'");
        }
        if (offset + e.data.size() > std::numeric_limits<std::uint32_t>::max()) {
            fail(file, "archive would exceed 4 GiB");
        }
        out.write(reinterpret_cast<const char*>(e.data.data()), static_cast<std::streamsize>(e.data.size()));

        store_u32(dir, static_cast<std::uint32_t>(name.size()));
        const auto* name_bytes = reinterpret_cast<const std::byte*>(name.data());
        dir.insert(dir.end(), name_bytes, name_bytes + name.size());
        store_u32(dir, e.data.empty() ? 0 : static_cast<std::uint32_t>(offset));
        store_u32(dir, static_cast<std::uint32_t>(e.data.size()));
        offset += e.data.size();
    }

    std::vector<std::byte> footer;
    store_u32(footer, static_cast<std::uint32_t>(entries.size()));
    store_u32(footer, static_cast<std::uint32_t>(offset));
    footer.push_back(static_cast<std::byte>(kind));

    out.write(reinterpret_cast<const char*>(dir.data()), static_cast<std::streamsize>(dir.size()));
    out.write(reinterpret_cast<const char*>(footer.data()), static_cast<std::streamsize>(footer.size()));
    if (!out) {
        fail(file, "write failed");
    }
}

} // namespace openbl::formats
