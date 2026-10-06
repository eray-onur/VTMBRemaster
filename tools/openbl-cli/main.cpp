// openbl-cli: inspection and extraction tool for Bloodlines game data.

#include "openbl/formats/error.hpp"
#include "openbl/formats/image_decode.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vpk.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/vfs/game_filesystem.hpp"

#include <cstdint>
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
                 "  openbl-cli tex info <texture> [game-dir]\n"
                 "  openbl-cli tex export <texture> <out.tga> [game-dir]\n"
                 "\n"
                 "<texture> is a game path with or without extension, e.g. materials/brick/bradwllb.\n"
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

struct LoadedTexture {
    formats::VtfTexture vtf;
    formats::TextureDamage damage = formats::TextureDamage::None;
};

// Loads a texture from a .tth/.ttz pair, or from a plain .vtf if one exists.
LoadedTexture load_texture(const vfs::GameFileSystem& gfs, std::string_view path) {
    std::string base = formats::normalize_game_path(path);
    for (std::string_view ext : {".tth", ".ttz", ".vtf"}) {
        if (base.ends_with(ext)) {
            base.erase(base.size() - ext.size());
        }
    }
    if (auto tth = gfs.read(base + ".tth")) {
        auto ttz = gfs.read(base + ".ttz");
        auto assembled = formats::assemble_vtf(*tth, ttz ? std::optional(std::span<const std::byte>(*ttz)) : std::nullopt);
        return {formats::VtfTexture::parse(std::move(assembled.data)), assembled.damage};
    }
    if (auto vtf = gfs.read(base + ".vtf")) {
        return {formats::VtfTexture::parse(std::move(*vtf)), formats::TextureDamage::None};
    }
    throw std::runtime_error("texture not found: " + base);
}

bool write_tga(const fs::path& file, const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height) {
    std::vector<std::byte> out(18);
    out[2] = std::byte{2}; // Uncompressed true-color.
    out[12] = static_cast<std::byte>(width & 0xFF);
    out[13] = static_cast<std::byte>(width >> 8);
    out[14] = static_cast<std::byte>(height & 0xFF);
    out[15] = static_cast<std::byte>(height >> 8);
    out[16] = std::byte{32};
    out[17] = std::byte{0x28}; // 8 alpha bits, top-left origin.
    out.reserve(out.size() + rgba.size());
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        for (std::size_t c : {2, 1, 0, 3}) {
            out.push_back(static_cast<std::byte>(rgba[i + c]));
        }
    }
    return write_file(file, out);
}

const char* damage_name(formats::TextureDamage damage) {
    switch (damage) {
    case formats::TextureDamage::MissingTtz:
        return "missing .ttz (large mips absent)";
    case formats::TextureDamage::CorruptTtz:
        return "corrupt .ttz";
    default:
        return "none";
    }
}

int cmd_tex(const std::vector<std::string_view>& args) {
    if (args.size() >= 3 && args[1] == "info") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3));
        const auto tex = load_texture(gfs, args[2]);
        const auto& h = tex.vtf.header();
        std::cout << "vtf version: " << h.version_major << "." << h.version_minor << "\n"
                  << "size:        " << h.width << "x" << h.height << "\n"
                  << "format:      " << formats::vtf_format_name(h.format) << "\n"
                  << "mips:        " << int{h.mip_count} << " (largest available: " << tex.vtf.largest_available_mip()
                  << ")\n"
                  << "frames:      " << h.frame_count << "\n"
                  << "faces:       " << tex.vtf.face_count() << "\n"
                  << "flags:       0x" << std::hex << h.flags << std::dec << "\n"
                  << "thumbnail:   " << formats::vtf_format_name(h.thumbnail_format) << " "
                  << int{h.thumbnail_width} << "x" << int{h.thumbnail_height} << "\n"
                  << "damage:      " << damage_name(tex.damage) << "\n";
        return 0;
    }
    if (args.size() >= 4 && args[1] == "export") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 4));
        const auto tex = load_texture(gfs, args[2]);
        const std::uint32_t mip = tex.vtf.largest_available_mip();
        if (!tex.vtf.has_mip(mip)) {
            std::cerr << "no image data present\n";
            return 1;
        }
        const std::uint32_t w = tex.vtf.mip_width(mip);
        const std::uint32_t h = tex.vtf.mip_height(mip);
        const auto rgba = formats::decode_to_rgba8(tex.vtf.header().format, tex.vtf.image(mip), w, h);
        if (!write_tga(fs::path(args[3]), rgba, w, h)) {
            std::cerr << "failed to write " << args[3] << "\n";
            return 1;
        }
        std::cout << "wrote " << w << "x" << h << " (mip " << mip << ") to " << args[3] << "\n";
        return 0;
    }
    usage();
    return 2;
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
        if (args[0] == "tex") {
            return cmd_tex(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    usage();
    return 2;
}
