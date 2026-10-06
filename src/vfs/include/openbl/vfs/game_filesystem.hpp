#pragma once

#include "openbl/formats/vpk.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openbl::vfs {

// Read-only view of a game content directory (the "Vampire" folder).
//
// Resolution order, highest priority first:
//   1. Loose files under the content directory (how patches and mods ship).
//   2. packNNN.vpk data archives, higher NNN first.
// Directory-index archives (see VpkKind::DirectoryIndex) contribute directory listings only.
class GameFileSystem {
public:
    // Accepts either the install root or its "Vampire" content directory.
    // Throws formats::FormatError if an archive is malformed.
    explicit GameFileSystem(const std::filesystem::path& game_dir);

    struct Source {
        enum class Kind { Loose, Archive } kind;
        std::filesystem::path file; // Loose file or archive path.
    };

    const std::filesystem::path& content_dir() const { return content_dir_; }
    const std::vector<std::unique_ptr<formats::VpkArchive>>& archives() const { return archives_; }

    bool exists(std::string_view path) const;
    std::optional<Source> source_of(std::string_view path) const;
    std::optional<std::vector<std::byte>> read(std::string_view path) const;

    // All resolvable file paths (normalized), sorted.
    std::vector<std::string> list_files() const;
    bool is_directory(std::string_view path) const;

    // Number of (path, lower-priority source) pairs hidden by a higher-priority source.
    std::size_t shadowed_count() const { return shadowed_count_; }

    // Resolves the content directory for an install root or content dir, or nullopt.
    static std::optional<std::filesystem::path> find_content_dir(const std::filesystem::path& game_dir);

private:
    struct Resolved {
        std::size_t archive = 0; // Index into archives_, or npos for a loose file.
        std::size_t entry = 0;
        std::filesystem::path loose_file;
    };
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    const Resolved* resolve(std::string_view path) const;

    std::filesystem::path content_dir_;
    std::vector<std::unique_ptr<formats::VpkArchive>> archives_;
    std::unordered_map<std::string, Resolved> files_;
    std::unordered_set<std::string> directories_;
    std::size_t shadowed_count_ = 0;
};

} // namespace openbl::vfs
