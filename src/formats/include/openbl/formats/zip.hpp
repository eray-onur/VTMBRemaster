#pragma once

// Read-only zip archives, as embedded in BSP pakfile lumps. Supports stored
// and deflated entries.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::formats {

class ZipArchive {
public:
    // Parses the central directory. Throws FormatError. The data must outlive the archive.
    explicit ZipArchive(std::span<const std::byte> data);

    struct Entry {
        std::string path; // Normalized game path.
        std::uint16_t method = 0;
        std::uint32_t compressed_size = 0;
        std::uint32_t size = 0;
        std::uint32_t local_header = 0;
    };

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view path) const;
    // Throws FormatError on corrupt data or unsupported compression.
    std::vector<std::byte> read(const Entry& entry) const;

private:
    std::span<const std::byte> data_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> index_;
};

} // namespace openbl::formats
