// openbl-cli: inspection and extraction tool for Bloodlines game data.

#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/vpk.hpp"
#include "openbl/vfs/game_filesystem.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace openbl;

namespace {

void usage() {
    std::cerr << "usage:\n"
                 "  openbl-cli vpk info <archive.vpk>\n"
                 "  openbl-cli vpk list <archive.vpk>\n"
                 "  openbl-cli vpk extract <archive.vpk> <out-dir> [path-prefix]\n"
                 "  openbl-cli fs stat [game-dir]\n"
                 "  openbl-cli fs list [game-dir] [path-prefix]\n"
                 "  openbl-cli fs which <path> [game-dir]\n"
                 "  openbl-cli fs cat <path> [game-dir]\n"
                 "\n"
                 "game-dir defaults to the OPENBL_GAME_DIR environment variable.\n";
}

fs::path game_dir_arg(const std::vector<std::string_view>& args, std::size_t index) {
    if (index < args.size()) {
        return fs::path(args[index]);
    }
    if (const char* env = std::getenv("OPENBL_GAME_DIR")) {
        return fs::path(env);
    }
    throw std::runtime_error("no game directory given and OPENBL_GAME_DIR is not set");
}

bool write_file(const fs::path& file, const std::vector<std::byte>& data) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

int cmd_vpk(const std::vector<std::string_view>& args) {
    if (args.size() < 3) {
        usage();
        return 2;
    }
    const auto archive = formats::VpkArchive::open(fs::path(args[2]));

    if (args[1] == "info") {
        std::uint64_t total = 0;
        for (const auto& e : archive->entries()) {
            total += e.size;
        }
        std::cout << "kind:    "
                  << (archive->kind() == formats::VpkKind::DirectoryIndex ? "directory-index" : "data") << "\n"
                  << "entries: " << archive->entries().size() << "\n"
                  << "bytes:   " << total << "\n";
        return 0;
    }
    if (args[1] == "list") {
        for (const auto& e : archive->entries()) {
            std::cout << e.size << "\t" << e.path << "\n";
        }
        return 0;
    }
    if (args[1] == "extract" && args.size() >= 4) {
        const fs::path out_dir(args[3]);
        const std::string prefix = args.size() >= 5 ? formats::normalize_game_path(args[4]) : std::string();
        std::size_t count = 0;
        for (const auto& e : archive->entries()) {
            if (!e.path.starts_with(prefix)) {
                continue;
            }
            if (!formats::is_safe_relative_path(e.path)) {
                std::cerr << "skipping unsafe path: " << e.path << "\n";
                continue;
            }
            if (archive->kind() == formats::VpkKind::DirectoryIndex) {
                fs::create_directories(out_dir / e.path);
            } else if (!write_file(out_dir / e.path, archive->read(e))) {
                std::cerr << "failed to write " << e.path << "\n";
                return 1;
            }
            ++count;
        }
        std::cout << "extracted " << count << " entries\n";
        return 0;
    }
    usage();
    return 2;
}

int cmd_fs(const std::vector<std::string_view>& args) {
    if (args.size() < 2) {
        usage();
        return 2;
    }
    const std::string_view sub = args[1];

    if (sub == "stat" || sub == "list") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 2));
        if (sub == "stat") {
            std::cout << "content dir: " << gfs.content_dir().string() << "\n"
                      << "archives:    " << gfs.archives().size() << "\n"
                      << "files:       " << gfs.list_files().size() << "\n"
                      << "shadowed:    " << gfs.shadowed_count() << "\n";
            for (const auto& a : gfs.archives()) {
                std::cout << "  " << a->file_path().filename().string() << "  " << a->entries().size()
                          << (a->kind() == formats::VpkKind::DirectoryIndex ? "  (directory index)" : "") << "\n";
            }
            return 0;
        }
        const std::string prefix = args.size() >= 4 ? formats::normalize_game_path(args[3]) : std::string();
        for (const auto& path : gfs.list_files()) {
            if (path.starts_with(prefix)) {
                std::cout << path << "\n";
            }
        }
        return 0;
    }
    if ((sub == "which" || sub == "cat") && args.size() >= 3) {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3));
        if (sub == "which") {
            auto source = gfs.source_of(args[2]);
            if (!source) {
                std::cerr << "not found: " << args[2] << "\n";
                return 1;
            }
            std::cout << (source->kind == vfs::GameFileSystem::Source::Kind::Loose ? "loose   " : "archive ")
                      << source->file.string() << "\n";
            return 0;
        }
        auto data = gfs.read(args[2]);
        if (!data) {
            std::cerr << "not found: " << args[2] << "\n";
            return 1;
        }
        std::fwrite(data->data(), 1, data->size(), stdout);
        return 0;
    }
    usage();
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 2;
    }
    try {
        if (args[0] == "vpk") {
            return cmd_vpk(args);
        }
        if (args[0] == "fs") {
            return cmd_fs(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    usage();
    return 2;
}
