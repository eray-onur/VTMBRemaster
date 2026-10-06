#pragma once

// Bloodlines VPK archives (packNNN.vpk). This is Troika's own format and is
// unrelated to the later Valve VPK format. See docs/formats/vpk.md.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::formats {

// Value of the trailing flag byte in the footer.
enum class VpkKind : std::uint8_t {
    Data = 0,           // Regular archive holding file data.
    DirectoryIndex = 1, // Lists directory paths only; every entry has offset 0, size 0.
};

struct VpkEntry {
    std::string path; // Normalized (see normalize_game_path).
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

class VpkArchive {
public:
    // Reads the footer and directory. File data is read lazily. Throws FormatError.
    static std::unique_ptr<VpkArchive> open(const std::filesystem::path& file);

    VpkArchive(const VpkArchive&) = delete;
    VpkArchive& operator=(const VpkArchive&) = delete;

    const std::filesystem::path& file_path() const { return file_path_; }
    VpkKind kind() const { return kind_; }
    std::span<const VpkEntry> entries() const { return entries_; }

    // Lookup by game path; the argument does not need to be normalized.
    const VpkEntry* find(std::string_view path) const;

    // Thread-safe. Throws FormatError on I/O failure.
    std::vector<std::byte> read(const VpkEntry& entry) const;

private:
    VpkArchive() = default;

    std::filesystem::path file_path_;
    VpkKind kind_ = VpkKind::Data;
    std::vector<VpkEntry> entries_;
    std::unordered_map<std::string, std::size_t> index_;
    mutable std::mutex stream_mutex_;
    mutable std::ifstream stream_;
};

struct VpkWriteEntry {
    std::string path;
    std::vector<std::byte> data;
};

// Writes an archive in the same layout the original game uses. Throws FormatError.
void write_vpk(const std::filesystem::path& file, std::span<const VpkWriteEntry> entries,
               VpkKind kind = VpkKind::Data);

} // namespace openbl::formats
