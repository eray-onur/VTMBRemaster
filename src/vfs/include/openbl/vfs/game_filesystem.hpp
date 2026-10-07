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

// Read-only view of a game content directory (the "Vampire" folder), with
// optional mod folders layered on top (e.g. "Unofficial_Patch", a sibling of
// "Vampire" in the install root).
//
// Resolution order, highest priority first:
//   1. Mod folders, the last one given first; within each, loose files, then
//      its packNNN.vpk archives, higher NNN first.
//   2. Loose files under the content directory (how patches ship).
//   3. packNNN.vpk data archives, higher NNN first.
// Directory-index archives (see VpkKind::DirectoryIndex) contribute directory listings only.
class GameFileSystem {
public:
    // Accepts either the install root or its "Vampire" content directory.
    // Throws formats::FormatError if an archive is malformed.
    // `mods` name folders in the install root (or are absolute paths).
    // Throws formats::FormatError if a mod folder is missing.
    explicit GameFileSystem(const std::filesystem::path& game_dir, const std::vector<std::string>& mods = {});

    struct Source {
        enum class Kind { Loose, Archive } kind;
        std::filesystem::path file; // Loose file or archive path.
    };

    const std::filesystem::path& content_dir() const { return content_dir_; }
    // Mounted mod folders, lowest priority first.
    const std::vector<std::filesystem::path>& mod_dirs() const { return mod_dirs_; }
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
    void mount(const std::filesystem::path& root);

    std::filesystem::path content_dir_;
    std::vector<std::filesystem::path> mod_dirs_;
    std::vector<std::unique_ptr<formats::VpkArchive>> archives_;
    std::unordered_map<std::string, Resolved> files_;
    std::unordered_set<std::string> directories_;
    std::size_t shadowed_count_ = 0;
};

} // namespace openbl::vfs
