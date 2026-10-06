#pragma once

#include "openbl/formats/zip.hpp"
#include "openbl/vfs/game_filesystem.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace openbl::render {

// Where the renderer reads files from: an optional map pakfile, searched
// first, then the game filesystem.
class AssetSource {
public:
    explicit AssetSource(const vfs::GameFileSystem& gfs, const formats::ZipArchive* pakfile = nullptr)
        : gfs_(gfs), pakfile_(pakfile) {}

    std::optional<std::vector<std::byte>> read(std::string_view path) const {
        if (pakfile_) {
            if (const auto* entry = pakfile_->find(path)) {
                return pakfile_->read(*entry);
            }
        }
        return gfs_.read(path);
    }

    bool exists(std::string_view path) const {
        return (pakfile_ && pakfile_->find(path)) || gfs_.exists(path);
    }

private:
    const vfs::GameFileSystem& gfs_;
    const formats::ZipArchive* pakfile_;
};

} // namespace openbl::render
