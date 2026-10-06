#include "openbl/vfs/game_filesystem.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>

namespace openbl::vfs {

namespace {

// Parses "packNNN.vpk" (case-insensitive) and returns NNN.
std::optional<int> pack_number(const std::filesystem::path& file) {
    const std::string name = formats::normalize_game_path(file.filename().string());
    if (!name.starts_with("pack") || !name.ends_with(".vpk") || name.size() <= 8) {
        return std::nullopt;
    }
    int number = 0;
    const char* first = name.data() + 4;
    const char* last = name.data() + name.size() - 4;
    auto [ptr, ec] = std::from_chars(first, last, number);
    if (ec != std::errc() || ptr != last) {
        return std::nullopt;
    }
    return number;
}

void add_parent_directories(std::unordered_set<std::string>& dirs, const std::string& file) {
    for (size_t slash = file.rfind('/'); slash != std::string::npos && slash > 0;
         slash = file.rfind('/', slash - 1)) {
        if (!dirs.insert(file.substr(0, slash)).second) {
            break; // Parents of an already-known directory are known too.
        }
    }
}

} // namespace

std::optional<std::filesystem::path> GameFileSystem::find_content_dir(const std::filesystem::path& game_dir) {
    std::error_code ec;
    for (const auto& candidate : {game_dir / "Vampire", game_dir}) {
        if (std::filesystem::exists(candidate / "pack000.vpk", ec)) {
            return candidate;
        }
    }
    return std::nullopt;
}

GameFileSystem::GameFileSystem(const std::filesystem::path& game_dir) {
    auto content = find_content_dir(game_dir);
    if (!content) {
        throw formats::FormatError(game_dir.string() + ": no pack000.vpk found here or in Vampire/");
    }
    content_dir_ = *content;

    std::vector<std::pair<int, std::filesystem::path>> packs;
    std::vector<std::filesystem::path> loose;
    for (const auto& item : std::filesystem::recursive_directory_iterator(content_dir_)) {
        if (!item.is_regular_file()) {
            continue;
        }
        if (item.path().parent_path() == content_dir_) {
            if (auto n = pack_number(item.path())) {
                packs.emplace_back(*n, item.path());
                continue;
            }
        }
        loose.push_back(item.path());
    }
    std::sort(packs.begin(), packs.end());

    // Lowest priority first; later insertions overwrite earlier ones.
    for (auto& [number, file] : packs) {
        auto archive = formats::VpkArchive::open(file);
        const std::size_t archive_index = archives_.size();
        if (archive->kind() == formats::VpkKind::DirectoryIndex) {
            for (const auto& entry : archive->entries()) {
                directories_.insert(entry.path);
                add_parent_directories(directories_, entry.path);
            }
        } else {
            const auto entries = archive->entries();
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto [it, inserted] = files_.insert_or_assign(entries[i].path, Resolved{archive_index, i, {}});
                shadowed_count_ += inserted ? 0 : 1;
                add_parent_directories(directories_, entries[i].path);
            }
        }
        archives_.push_back(std::move(archive));
    }

    for (const auto& file : loose) {
        std::string path =
            formats::normalize_game_path(std::filesystem::relative(file, content_dir_).generic_string());
        auto [it, inserted] = files_.insert_or_assign(path, Resolved{npos, 0, file});
        shadowed_count_ += inserted ? 0 : 1;
        add_parent_directories(directories_, path);
    }
}

const GameFileSystem::Resolved* GameFileSystem::resolve(std::string_view path) const {
    auto it = files_.find(formats::normalize_game_path(path));
    return it == files_.end() ? nullptr : &it->second;
}

bool GameFileSystem::exists(std::string_view path) const {
    return resolve(path) != nullptr;
}

std::optional<GameFileSystem::Source> GameFileSystem::source_of(std::string_view path) const {
    const Resolved* r = resolve(path);
    if (!r) {
        return std::nullopt;
    }
    if (r->archive == npos) {
        return Source{Source::Kind::Loose, r->loose_file};
    }
    return Source{Source::Kind::Archive, archives_[r->archive]->file_path()};
}

std::optional<std::vector<std::byte>> GameFileSystem::read(std::string_view path) const {
    const Resolved* r = resolve(path);
    if (!r) {
        return std::nullopt;
    }
    if (r->archive != npos) {
        const auto& archive = *archives_[r->archive];
        return archive.read(archive.entries()[r->entry]);
    }
    std::ifstream in(r->loose_file, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::byte> data(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in) {
        return std::nullopt;
    }
    return data;
}

std::vector<std::string> GameFileSystem::list_files() const {
    std::vector<std::string> out;
    out.reserve(files_.size());
    for (const auto& [path, _] : files_) {
        out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool GameFileSystem::is_directory(std::string_view path) const {
    return directories_.contains(formats::normalize_game_path(path));
}

} // namespace openbl::vfs
