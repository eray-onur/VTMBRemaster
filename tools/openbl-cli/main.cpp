// openbl-cli: inspection and extraction tool for Bloodlines game data.

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/zip.hpp"
#include "openbl/formats/image_decode.hpp"
#include "openbl/formats/image_read.hpp"
#include "openbl/formats/image_write.hpp"
#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/mp3.hpp"
#include "openbl/formats/sav.hpp"
#ifdef OPENBL_WITH_FFMPEG
#include "openbl/media/video.hpp"
#endif
#include "openbl/formats/save_write.hpp"
#include "openbl/formats/vtx.hpp"
#include "openbl/formats/wav.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/text_encoding.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vmt.hpp"
#include "openbl/formats/vpk.hpp"
#include "openbl/formats/vtf.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <span>
#include <unordered_map>
#include "openbl/vfs/game_filesystem.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <map>
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
                 "  openbl-cli fs coverage [game-dir]\n"
                 "  openbl-cli fs extract <out-dir> [prefix[*suffix] [game-dir]]\n"
                 "  openbl-cli fs which <path> [game-dir]\n"
                 "  openbl-cli fs cat <path> [game-dir]\n"
                 "  openbl-cli tex info <texture> [game-dir]\n"
                 "  openbl-cli tex export <texture> <out.tga> [game-dir]\n"
                 "  openbl-cli mat info <material> [game-dir]\n"
                 "  openbl-cli mat catalogue [out.md|-] [game-dir]\n"
                 "  openbl-cli mdl info <model> [game-dir]\n"
                 "  openbl-cli mdl export <model> <out.obj> [--flex name=weight ...] [game-dir]\n"
                 "  openbl-cli mdl sequences <model> [game-dir]\n"
                 "  openbl-cli mdl pose <model> <sequence> <frame> <out.obj> [game-dir]\n"
                 "  openbl-cli bsp info <map> [game-dir]\n"
                 "  openbl-cli bsp pak <map> [game-dir]\n"
                 "  openbl-cli bsp pak-cat <map> <path> [game-dir]\n"
                 "  openbl-cli bsp export <map> <out.obj> [game-dir]\n"
                 "  openbl-cli image compare <reference> <candidate> [x y w h]\n"
                 "  openbl-cli audio export <sound> <out.wav> [game-dir]\n"
                 "  openbl-cli save rewrite <in.sav> <out.sav>\n"
                 "  openbl-cli video frame <movie> <n> <out.png> [game-dir]\n"
                 "\n"
                 "<texture> and <material> are game paths with or without extension, e.g.\n"
                 "materials/brick/bradwllb; the materials/ prefix is optional for <material>.\n"
                 "\n"
                 "game-dir defaults to the OPENBL_GAME_DIR environment variable.\n";
}

// Mod folders to mount over the game, from OPENBL_MODS ("Unofficial_Patch",
// several separated by ';').
std::vector<std::string> game_mods() {
    std::vector<std::string> mods;
    if (const char* env = std::getenv("OPENBL_MODS")) {
        std::string_view list(env);
        while (!list.empty()) {
            const auto sep = list.find(';');
            if (const auto mod = list.substr(0, sep); !mod.empty()) {
                mods.emplace_back(mod);
            }
            list = sep == std::string_view::npos ? std::string_view() : list.substr(sep + 1);
        }
    }
    return mods;
}

fs::path game_dir_arg(const std::vector<std::string_view>& args, std::size_t index) {
    if (index < args.size() && !args[index].empty()) {
        return fs::path(args[index]);
    }
    if (const char* env = std::getenv("OPENBL_GAME_DIR")) {
        return fs::path(env);
    }
    throw std::runtime_error("no game directory given and OPENBL_GAME_DIR is not set");
}

bool write_file(const fs::path& file, const std::vector<std::byte>& data) {
    if (file.has_parent_path()) { // A bare file name has none, and create_directories("") throws.
        fs::create_directories(file.parent_path());
    }
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
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
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
        const vfs::GameFileSystem gfs(game_dir_arg(args, 4), game_mods());
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

void print_kv(const formats::KvNode& node, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    if (!node.is_block) {
        std::cout << pad << node.key << " = " << node.value << "\n";
        return;
    }
    std::cout << pad << node.key << "\n";
    for (const auto& child : node.children) {
        print_kv(child, indent + 1);
    }
}

// Classifies a material parameter value for the shader catalogue.
std::string_view value_kind(const vfs::GameFileSystem& gfs, std::string_view value,
                            std::unordered_map<std::string, bool>& texture_cache) {
    auto is_number = [](std::string_view s, bool integer) {
        if (s.empty()) {
            return false;
        }
        double d = 0;
        long long i = 0;
        const char* end = s.data() + s.size();
        return integer ? std::from_chars(s.data(), end, i).ptr == end : std::from_chars(s.data(), end, d).ptr == end;
    };
    const std::string_view v = value;
    if (v.empty()) {
        return "empty";
    }
    if (is_number(v, true)) {
        return "int";
    }
    if (is_number(v, false) || (v.front() == '.' && is_number(std::string("0") + std::string(v), false))) {
        return "float";
    }
    if (v.front() == '[' || v.front() == '{') {
        return "vector";
    }
    if (v.starts_with("center") || v.starts_with("scale") || v.starts_with("rotate") || v.starts_with("translate")) {
        return "transform";
    }
    const std::string tex = formats::material_texture_path(v);
    auto [it, inserted] = texture_cache.try_emplace(tex, false);
    if (inserted) {
        it->second = gfs.exists(tex + ".tth") || gfs.exists(tex + ".vtf");
    }
    if (it->second) {
        return "texture";
    }
    if (gfs.exists(tex + ".vmt")) {
        return "material";
    }
    return "string";
}

// Material parameters the renderer reads (src/render/material_system.cpp);
// keep in sync so the catalogue shows what is still unsupported.
constexpr std::string_view kRendererParams[] = {
    "$basetexture", "$basetexture2", "$alphatest", "$alphatestreference", "$additive", "$translucent",
    "$selfillum", "$nocull", "$normalmap", "$fogcolor", "$reflecttint", "%compilenodraw", "%compiletrigger",
    "%compileclip", "%compileplayerclip", "%compilenpcclip", "%compileinvisible", "%compilehint", "%compileskip",
    "%compileareaportal", "%compileorigin", "%compilefog",
};

// Writes a Markdown catalogue of every shader, parameter and proxy used by
// the game's materials, including those packed into maps.
int cmd_mat_catalogue(const vfs::GameFileSystem& gfs, std::ostream& out) {
    struct Param {
        std::size_t uses = 0;
        std::map<std::string, std::size_t> kinds;
        std::string example;
    };
    struct Shader {
        std::size_t materials = 0, from_maps = 0;
        std::map<std::string, Param> params;
        std::map<std::string, std::size_t> fallbacks;
        std::size_t junk_keys = 0;
    };
    struct Proxy {
        std::size_t uses = 0;
        std::map<std::string, std::size_t> keys;
        std::map<std::string, std::size_t> shaders;
    };
    std::map<std::string, Shader> shaders;
    std::map<std::string, Proxy> proxies;
    std::map<std::string, std::size_t> junk; // Keys that are not parameters, across all shaders.
    std::unordered_map<std::string, bool> texture_cache;
    std::size_t failed = 0;
    auto lower = [](std::string s) {
        for (char& c : s) {
            c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        return s;
    };

    auto add = [&](const formats::Material& mat, bool from_map) {
        Shader& s = shaders[mat.shader];
        ++s.materials;
        s.from_maps += from_map;
        for (const auto& [key, value] : mat.params) {
            // Real parameters start with '$' or '%'. Anything else is a key/value
            // pair knocked out of step by a typo (see docs/formats/materials.md).
            if (key.empty() || (key[0] != '$' && key[0] != '%')) {
                ++s.junk_keys;
                std::string shown = formats::cp1252_to_utf8(key);
                for (char& c : shown) {
                    c = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
                }
                ++junk[shown];
                continue;
            }
            Param& p = s.params[key];
            ++p.uses;
            ++p.kinds[std::string(value_kind(gfs, value, texture_cache))];
            if (p.example.empty() && !value.empty()) {
                p.example = formats::cp1252_to_utf8(value.size() > 40 ? value.substr(0, 37) + "..." : value);
            }
        }
        for (const auto& block : mat.sub_blocks) {
            ++s.fallbacks[lower(block.key)];
        }
        for (const auto& proxy : mat.proxies) {
            Proxy& p = proxies[lower(proxy.key)];
            ++p.uses;
            ++p.shaders[mat.shader];
            for (const auto& child : proxy.children) {
                ++p.keys[formats::cp1252_to_utf8(lower(child.key))];
            }
        }
    };
    auto parse = [&](std::span<const std::byte> data) {
        return formats::parse_vmt({reinterpret_cast<const char*>(data.data()), data.size()});
    };

    for (const auto& path : gfs.list_files()) {
        if (path.ends_with(".vmt")) {
            try {
                add(parse(*gfs.read(path)), false);
            } catch (const formats::FormatError&) {
                ++failed;
            }
        } else if (path.starts_with("maps/") && path.ends_with(".bsp")) {
            const auto bsp = formats::parse_bsp(*gfs.read(path));
            const formats::ZipArchive pak(bsp.pakfile);
            for (const auto& entry : pak.entries()) {
                if (!entry.path.ends_with(".vmt")) {
                    continue;
                }
                try {
                    auto mat = parse(pak.read(entry));
                    if (formats::is_vmt_patch(mat)) {
                        const std::string base_path = formats::vmt_patch_include(mat);
                        const auto* in_pak = pak.find(base_path);
                        const auto base_data = in_pak ? std::optional(pak.read(*in_pak)) : gfs.read(base_path);
                        if (!base_data) {
                            ++failed;
                            continue;
                        }
                        mat = formats::apply_vmt_patch(parse(*base_data), mat);
                    }
                    add(mat, true);
                } catch (const formats::FormatError&) {
                    ++failed;
                }
            }
        }
    }

    std::size_t total = 0;
    for (const auto& [name, s] : shaders) {
        total += s.materials;
    }
    std::vector<std::pair<std::size_t, std::string>> order;
    for (const auto& [name, s] : shaders) {
        order.emplace_back(s.materials, name);
    }
    std::sort(order.rbegin(), order.rend());

    out << "# Shader catalogue\n\n"
        << "Generated by `openbl-cli mat catalogue` from the Steam release: " << total << " materials (loose, in archives,"
        << " and in map pakfiles with patches applied), " << shaders.size() << " shaders, " << proxies.size()
        << " proxy types. " << failed << " materials could not be read.\n\n"
        << "Value kinds: `texture` (resolves to a texture), `material` (resolves to another material), `int`, `float`,"
        << " `vector` (`[x y z]` or `{r g b}`), `transform` (`center … scale … rotate … translate …`), `string`,"
        << " `empty`. The OpenBL column marks parameters the renderer reads today.\n\n"
        << "## Shaders\n\n| Shader | Materials | From maps | Parameters |\n|---|---|---|---|\n";
    for (const auto& [n, name] : order) {
        const auto& s = shaders[name];
        out << "| [" << name << "](#" << name << ") | " << s.materials << " | " << s.from_maps << " | "
            << s.params.size() << " |\n";
    }
    for (const auto& [n, name] : order) {
        const auto& s = shaders[name];
        out << "\n### " << name << "\n\n" << s.materials << " materials.";
        if (!s.fallbacks.empty()) {
            out << " Fallback blocks:";
            for (const auto& [f, c] : s.fallbacks) {
                out << " `" << f << "` (" << c << ")";
            }
            out << ".";
        }
        if (s.junk_keys) {
            out << " " << s.junk_keys << " keys that are not parameters (typos; see below).";
        }
        out << "\n\n| Parameter | Uses | Kinds | Example | OpenBL |\n|---|---|---|---|---|\n";
        std::vector<std::pair<std::size_t, std::string>> params;
        for (const auto& [key, p] : s.params) {
            params.emplace_back(p.uses, key);
        }
        std::sort(params.begin(), params.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (const auto& [uses, key] : params) {
            const auto& p = s.params.at(key);
            out << "| `" << key << "` | " << uses << " | ";
            bool first = true;
            for (const auto& [kind, c] : p.kinds) {
                out << (first ? "" : ", ") << kind << (p.kinds.size() > 1 ? " " + std::to_string(c) : "");
                first = false;
            }
            std::string example = p.example;
            std::replace(example.begin(), example.end(), '|', '/');
            const bool supported = std::find(std::begin(kRendererParams), std::end(kRendererParams), key) !=
                                   std::end(kRendererParams);
            out << " | `" << example << "` | " << (supported ? "yes" : "") << " |\n";
        }
    }
    std::size_t junk_total = 0;
    std::vector<std::pair<std::size_t, std::string>> junk_order;
    for (const auto& [key, n] : junk) {
        junk_total += n;
        junk_order.emplace_back(n, key);
    }
    std::sort(junk_order.rbegin(), junk_order.rend());
    out << "\n## Keys that are not parameters\n\n" << junk_total
        << " keys across all materials do not start with `$` or `%`. They come from typos that knock keys and"
        << " values out of step, mostly `\\\\` used as if it were a comment. The most common:\n\n";
    for (std::size_t i = 0; i < junk_order.size() && i < 15; ++i) {
        out << "- `" << junk_order[i].second << "` (" << junk_order[i].first << ")\n";
    }
    out << "\n## Proxies\n\n| Proxy | Uses | Keys | Shaders |\n|---|---|---|---|\n";
    std::vector<std::pair<std::size_t, std::string>> porder;
    for (const auto& [name, p] : proxies) {
        porder.emplace_back(p.uses, name);
    }
    std::sort(porder.rbegin(), porder.rend());
    for (const auto& [n, name] : porder) {
        const auto& p = proxies[name];
        out << "| `" << name << "` | " << p.uses << " | ";
        bool first = true;
        for (const auto& [key, c] : p.keys) {
            out << (first ? "" : ", ") << "`" << key << "`" << (c != p.uses ? " (" + std::to_string(c) + ")" : "");
            first = false;
        }
        out << " | ";
        first = true;
        for (const auto& [shader, c] : p.shaders) {
            out << (first ? "" : ", ") << shader;
            first = false;
        }
        out << " |\n";
    }
    return 0;
}

int cmd_mat(const std::vector<std::string_view>& args) {
    if (args.size() >= 2 && args[1] == "catalogue") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
        if (args.size() >= 3 && args[2] != "-") {
            std::ofstream out(fs::path(args[2]), std::ios::binary);
            return cmd_mat_catalogue(gfs, out);
        }
        return cmd_mat_catalogue(gfs, std::cout);
    }
    if (args.size() < 3 || args[1] != "info") {
        usage();
        return 2;
    }
    const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
    std::string path = formats::normalize_game_path(args[2]);
    if (!path.ends_with(".vmt")) {
        path += ".vmt";
    }
    if (!path.starts_with("materials/")) {
        path.insert(0, "materials/");
    }
    const auto data = gfs.read(path);
    if (!data) {
        std::cerr << "not found: " << path << "\n";
        return 1;
    }
    const auto mat =
        formats::parse_vmt(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));

    std::cout << "shader: " << mat.shader << "\n";
    std::vector<std::pair<std::string, std::string>> params(mat.params.begin(), mat.params.end());
    std::sort(params.begin(), params.end());
    for (const auto& [key, value] : params) {
        std::cout << "  " << key << " = " << value;
        const std::string tex = formats::material_texture_path(value);
        if (gfs.exists(tex + ".tth") || gfs.exists(tex + ".vtf")) {
            std::cout << "  [texture]";
        }
        std::cout << "\n";
    }
    for (const auto& block : mat.sub_blocks) {
        print_kv(block, 1);
    }
    if (!mat.proxies.empty()) {
        std::cout << "proxies:\n";
        for (const auto& proxy : mat.proxies) {
            print_kv(proxy, 1);
        }
    }
    for (const auto& d : mat.diagnostics) {
        std::cout << "warning: line " << d.line << ": " << d.message << "\n";
    }
    return 0;
}

struct LoadedModel {
    formats::MdlFile mdl;
    formats::VtxFile vtx;
};

LoadedModel load_model(const vfs::GameFileSystem& gfs, std::string_view path) {
    std::string base = formats::normalize_game_path(path);
    if (base.ends_with(".mdl")) {
        base.erase(base.size() - 4);
    }
    if (!base.starts_with("models/")) {
        base.insert(0, "models/");
    }
    auto mdl_data = gfs.read(base + ".mdl");
    auto vtx_data = gfs.read(base + ".dx80.vtx");
    if (!mdl_data || !vtx_data) {
        throw std::runtime_error("model not found (needs .mdl and .dx80.vtx): " + base);
    }
    LoadedModel out{formats::parse_mdl(*mdl_data), formats::parse_vtx(*vtx_data)};
    if (out.mdl.checksum != out.vtx.checksum) {
        throw std::runtime_error("checksum mismatch between .mdl and .vtx");
    }
    return out;
}

// Game path of the material a texture name resolves to, or empty.
std::string find_model_material(const vfs::GameFileSystem& gfs, const formats::MdlFile& mdl, std::int32_t texture) {
    if (texture < 0) {
        return {};
    }
    for (const auto& dir : mdl.texture_dirs) {
        const std::string path = formats::normalize_game_path("materials/" + dir + "/" +
                                                              mdl.textures[static_cast<std::size_t>(texture)]) +
                                 ".vmt";
        if (gfs.exists(path)) {
            return path;
        }
    }
    return {};
}

const char* vertex_format_name(formats::MdlVertexFormat f) {
    switch (f) {
    case formats::MdlVertexFormat::Float:
        return "float";
    case formats::MdlVertexFormat::Quantized16:
        return "quantized16";
    default:
        return "quantized8";
    }
}

// --- Posing (CPU skinning) for inspection ------------------------------------

struct FoundSequence {
    std::shared_ptr<formats::MdlFile> file; // Model that owns the sequence's animation.
    formats::MdlSequence sequence;
};

// Searches a model and, recursively, its include models for a sequence label.
std::optional<FoundSequence> find_sequence(const vfs::GameFileSystem& gfs, const std::string& path,
                                           std::string_view label, int depth = 0) {
    if (depth > 8) {
        return std::nullopt;
    }
    const auto data = gfs.read(path);
    if (!data) {
        return std::nullopt;
    }
    auto mdl = std::make_shared<formats::MdlFile>(formats::parse_mdl(*data));
    for (const auto& s : mdl->sequences) {
        if (formats::iequals(s.label, label) && s.animation >= 0) {
            return FoundSequence{mdl, s};
        }
    }
    for (const auto& inc : mdl->include_models) {
        if (auto found = find_sequence(gfs, inc, label, depth + 1)) {
            return found;
        }
    }
    return std::nullopt;
}

void list_sequences(const vfs::GameFileSystem& gfs, const std::string& path, int depth = 0) {
    const auto data = gfs.read(path);
    if (!data || depth > 8) {
        return;
    }
    const auto mdl = formats::parse_mdl(*data);
    std::cout << std::string(static_cast<std::size_t>(depth) * 2, ' ') << path << ": " << mdl.sequences.size()
              << " sequences, " << mdl.animations.size() << " animations\n";
    for (const auto& s : mdl.sequences) {
        std::cout << std::string(static_cast<std::size_t>(depth) * 2 + 2, ' ') << s.label;
        if (s.animation >= 0) {
            const auto& a = mdl.animations[static_cast<std::size_t>(s.animation)];
            std::cout << "  (" << a.frame_count << " frames @ " << a.fps << " fps"
                      << ((s.flags & formats::sequence_flags::Looping) ? ", loops" : "") << ")";
        }
        std::cout << "  " << s.activity_name << "\n";
    }
    for (const auto& inc : mdl.include_models) {
        list_sequences(gfs, inc, depth + 1);
    }
}

std::string model_path(std::string_view arg) {
    std::string base = formats::normalize_game_path(arg);
    if (!base.ends_with(".mdl")) {
        base += ".mdl";
    }
    if (!base.starts_with("models/")) {
        base.insert(0, "models/");
    }
    return base;
}

// Research helper for the packed-normal tables: one CSV row per vertex of
// every model, with its packed normal code (quantized formats) or stored
// normal (float format) and the area-weighted normal of its LOD 0 triangles.
int cmd_mdl_normal_samples(const vfs::GameFileSystem& gfs, const fs::path& out_path) {
    std::ofstream out(out_path, std::ios::binary);
    out << "format,code,sx,sy,sz,gx,gy,gz\n";
    std::size_t models = 0;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".mdl")) {
            continue;
        }
        LoadedModel model;
        try {
            model = load_model(gfs, path);
        } catch (const std::exception&) {
            continue;
        }
        ++models;
        for (std::size_t b = 0; b < model.mdl.body_parts.size() && b < model.vtx.body_parts.size(); ++b) {
            for (std::size_t m = 0; m < model.mdl.body_parts[b].models.size(); ++m) {
                const auto& mo = model.mdl.body_parts[b].models[m];
                const auto& vm = model.vtx.body_parts[b].models[m];
                if (vm.lods.empty()) {
                    continue;
                }
                std::vector<std::array<double, 3>> acc(mo.vertices.size(), {0, 0, 0});
                for (std::size_t me = 0; me < mo.meshes.size() && me < vm.lods[0].meshes.size(); ++me) {
                    const auto tris = vm.lods[0].meshes[me].triangles();
                    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
                        std::array<std::size_t, 3> i{};
                        for (std::size_t k = 0; k < 3; ++k) {
                            i[k] = mo.meshes[me].vertex_offset + tris[t + k];
                        }
                        if (i[0] >= acc.size() || i[1] >= acc.size() || i[2] >= acc.size()) {
                            continue;
                        }
                        const auto& a = mo.vertices[i[0]].position;
                        const auto& c1 = mo.vertices[i[1]].position;
                        const auto& c2 = mo.vertices[i[2]].position;
                        const double u[3] = {c1[0] - a[0], c1[1] - a[1], c1[2] - a[2]};
                        const double w[3] = {c2[0] - a[0], c2[1] - a[1], c2[2] - a[2]};
                        const double n[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
                                             u[0] * w[1] - u[1] * w[0]};
                        for (std::size_t k = 0; k < 3; ++k) {
                            for (std::size_t c = 0; c < 3; ++c) {
                                acc[i[k]][c] += n[c];
                            }
                        }
                    }
                }
                for (std::size_t v = 0; v < mo.vertices.size(); ++v) {
                    const auto& g = acc[v];
                    const double len = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                    if (len < 1e-9) {
                        continue;
                    }
                    const auto& vx = mo.vertices[v];
                    out << static_cast<int>(mo.vertex_format) << "," << vx.packed_normal << ","
                        << vx.normal[0] << "," << vx.normal[1] << "," << vx.normal[2] << "," << g[0] / len << ","
                        << g[1] / len << "," << g[2] / len << "\n";
                }
            }
        }
    }
    std::cout << "sampled " << models << " models into " << out_path.string() << "\n";
    return 0;
}

int cmd_mdl(const std::vector<std::string_view>& args) {
    if (args.size() >= 3 && args[1] == "normal-samples") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
        return cmd_mdl_normal_samples(gfs, fs::path(args[2]));
    }
    if (args.size() >= 3 && args[1] == "sequences") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
        list_sequences(gfs, model_path(args[2]));
        return 0;
    }
    if (args.size() >= 6 && args[1] == "pose") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 6), game_mods());
        const auto model = load_model(gfs, args[2]);
        const auto found = find_sequence(gfs, model_path(args[2]), args[3]);
        if (!found) {
            std::cerr << "sequence not found: " << args[3] << "\n";
            return 1;
        }
        const auto& anim_file = *found->file;
        const auto anim_pose =
            anim_file.sample_animation(static_cast<std::size_t>(found->sequence.animation),
                                       static_cast<std::uint32_t>(std::stoul(std::string(args[4]))));
        // Map animation bones onto the model's bones by name; unmapped bones keep their bind pose.
        const auto& bones = model.mdl.bones;
        auto local = formats::mdl_bind_pose(bones);
        std::size_t mapped = 0;
        for (std::size_t b = 0; b < bones.size(); ++b) {
            for (std::size_t a = 0; a < anim_file.bones.size(); ++a) {
                if (formats::iequals(anim_file.bones[a].name, bones[b].name)) {
                    local[b] = anim_pose[a];
                    ++mapped;
                    break;
                }
            }
        }
        const auto skin = formats::mdl_skinning_matrices(bones, formats::mdl_world_transforms(bones, local));
        std::ofstream out{fs::path(args[5])};
        std::size_t base = 1;
        for (std::size_t bp = 0; bp < model.mdl.body_parts.size(); ++bp) {
            const auto& part = model.mdl.body_parts[bp];
            if (part.models.empty()) {
                continue;
            }
            const auto& mo = part.models[0];
            for (const auto& v : mo.vertices) {
                std::array<float, 3> p{};
                for (std::size_t k = 0; k < 3; ++k) {
                    const float w = v.weights[k] / 255.0f;
                    if (w <= 0 || v.bones[k] >= skin.size()) {
                        continue;
                    }
                    const formats::MdlMatrix& m = skin[v.bones[k]];
                    for (std::size_t i = 0; i < 3; ++i) {
                        p[i] += w * (m[i][0] * v.position[0] + m[i][1] * v.position[1] + m[i][2] * v.position[2] + m[i][3]);
                    }
                }
                out << "v " << p[0] << " " << p[1] << " " << p[2] << "\n";
            }
            const auto& lod = model.vtx.body_parts[bp].models[0].lods[0];
            for (std::size_t me = 0; me < mo.meshes.size() && me < lod.meshes.size(); ++me) {
                const auto tris = lod.meshes[me].triangles();
                for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
                    out << "f " << base + mo.meshes[me].vertex_offset + tris[t] << " "
                        << base + mo.meshes[me].vertex_offset + tris[t + 1] << " "
                        << base + mo.meshes[me].vertex_offset + tris[t + 2] << "\n";
                }
            }
            base += mo.vertices.size();
        }
        std::cout << "posed with '" << found->sequence.label << "' (" << mapped << " of " << bones.size()
                  << " bones animated) -> " << args[5] << "\n";
        return 0;
    }
    if (args.size() >= 3 && args[1] == "info") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
        const auto model = load_model(gfs, args[2]);
        const auto& mdl = model.mdl;
        std::cout << "name:      " << mdl.name << "\n"
                  << "bones:     " << mdl.bones.size() << "\n"
                  << "skins:     " << mdl.skin_families.size() << "\n"
                  << "lods:      " << model.vtx.lod_count << "\n"
                  << "textures:\n";
        for (std::size_t t = 0; t < mdl.textures.size(); ++t) {
            const std::string mat = find_model_material(gfs, mdl, static_cast<std::int32_t>(t));
            std::cout << "  " << mdl.textures[t] << "  ->  " << (mat.empty() ? "(not found)" : mat) << "\n";
        }
        for (std::size_t b = 0; b < mdl.body_parts.size(); ++b) {
            const auto& part = mdl.body_parts[b];
            std::cout << "body part '" << part.name << "'\n";
            for (std::size_t m = 0; m < part.models.size(); ++m) {
                const auto& mo = part.models[m];
                std::size_t tris = 0;
                for (const auto& mesh : model.vtx.body_parts[b].models[m].lods[0].meshes) {
                    tris += mesh.triangles().size() / 3;
                }
                std::cout << "  model '" << mo.name << "': " << mo.vertices.size() << " vertices ("
                          << vertex_format_name(mo.vertex_format) << "), " << mo.meshes.size() << " meshes, " << tris
                          << " triangles\n";
            }
        }
        return 0;
    }
    if (args.size() >= 4 && args[1] == "export") {
        // Optional "--flex name=weight" options (anywhere after the output path) apply facial morphs.
        std::vector<std::pair<std::string, float>> flex_weights;
        std::vector<std::string_view> rest(args.begin(), args.begin() + 4);
        for (std::size_t i = 4; i < args.size(); ++i) {
            if (args[i] == "--flex" && i + 1 < args.size()) {
                const std::string spec(args[++i]);
                const std::size_t eq = spec.find('=');
                flex_weights.emplace_back(spec.substr(0, eq), eq == std::string::npos ? 1.0f : std::stof(spec.substr(eq + 1)));
            } else {
                rest.push_back(args[i]);
            }
        }
        const vfs::GameFileSystem gfs(game_dir_arg(rest, 4), game_mods());
        const auto model = load_model(gfs, args[2]);
        std::ofstream out{fs::path(args[3])};
        out << "# Exported by openbl-cli from " << model.mdl.name << " (first model of each body part, LOD 0)\n";
        // A mesh flex's influence ramps up from targets[0] to targets[1], holds
        // until targets[2] and ramps down to targets[3].
        auto ramp = [](const std::array<float, 4>& t, float w) {
            if (w <= t[0] || w >= t[3]) {
                return 0.0f;
            }
            if (w < t[1]) {
                return (w - t[0]) / (t[1] - t[0]);
            }
            return w <= t[2] ? 1.0f : (t[3] - w) / (t[3] - t[2]);
        };
        std::size_t base = 1;
        for (std::size_t b = 0; b < model.mdl.body_parts.size(); ++b) {
            const auto& part = model.mdl.body_parts[b];
            if (part.models.empty()) {
                continue;
            }
            const auto& mo = part.models[0];
            std::vector<std::array<float, 3>> positions;
            for (const auto& v : mo.vertices) {
                positions.push_back(v.position);
            }
            for (const auto& mesh : mo.meshes) {
                for (const auto& f : mesh.flexes) {
                    for (const auto& [name, weight] : flex_weights) {
                        if (f.flex < 0 || model.mdl.flex_names[static_cast<std::size_t>(f.flex)] != name) {
                            continue;
                        }
                        const float s = ramp(f.targets, weight);
                        for (const auto& fv : f.vertices) {
                            for (std::size_t k = 0; k < 3; ++k) {
                                positions[mesh.vertex_offset + fv.index][k] += s * fv.delta[k];
                            }
                        }
                    }
                }
            }
            out << "o " << part.name << "\n";
            for (std::size_t i = 0; i < mo.vertices.size(); ++i) {
                const auto& v = mo.vertices[i];
                const auto& p = positions[i];
                out << "v " << p[0] << " " << p[1] << " " << p[2] << "\n";
                out << "vt " << v.uv[0] << " " << 1.0f - v.uv[1] << "\n";
            }
            const auto& lod = model.vtx.body_parts[b].models[0].lods[0];
            for (std::size_t me = 0; me < mo.meshes.size() && me < lod.meshes.size(); ++me) {
                const auto& mesh = mo.meshes[me];
                const auto tex = model.mdl.texture_for(mesh.material);
                out << "usemtl " << (tex >= 0 ? model.mdl.textures[static_cast<std::size_t>(tex)] : "none") << "\n";
                const auto tris = lod.meshes[me].triangles();
                for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
                    out << "f";
                    for (std::size_t k = 0; k < 3; ++k) {
                        const std::size_t idx = base + mesh.vertex_offset + tris[t + k];
                        out << " " << idx << "/" << idx;
                    }
                    out << "\n";
                }
            }
            base += mo.vertices.size();
        }
        std::cout << "wrote " << args[3] << "\n";
        return out ? 0 : 1;
    }
    usage();
    return 2;
}

formats::BspFile load_map(const vfs::GameFileSystem& gfs, std::string_view name) {
    std::string path = formats::normalize_game_path(name);
    if (!path.ends_with(".bsp")) {
        path += ".bsp";
    }
    if (!path.starts_with("maps/")) {
        path.insert(0, "maps/");
    }
    const auto data = gfs.read(path);
    if (!data) {
        throw std::runtime_error("map not found: " + path);
    }
    return formats::parse_bsp(*data);
}

// Faces that are never drawn: tool surfaces and the sky (rendered separately).
bool face_is_drawn(const formats::BspFile& bsp, const formats::BspFace& face) {
    using namespace formats::surf_flags;
    return (bsp.texinfos[face.texinfo].flags & (NoDraw | Sky | Sky2D | Trigger | Skip | Hint)) == 0;
}

int cmd_bsp(const std::vector<std::string_view>& args) {
    if (args.size() >= 3 && (args[1] == "pak" || args[1] == "pak-cat")) {
        // pak <map> [game-dir]: list the embedded files; pak-cat <map> <path> [game-dir]: print one.
        const bool cat = args[1] == "pak-cat";
        if (cat && args.size() < 4) {
            usage();
            return 2;
        }
        const vfs::GameFileSystem gfs(game_dir_arg(args, cat ? 4 : 3), game_mods());
        const auto bsp = load_map(gfs, args[2]);
        const formats::ZipArchive pak(bsp.pakfile);
        if (cat) {
            const auto* entry = pak.find(formats::normalize_game_path(args[3]));
            if (!entry) {
                std::cerr << "not in the pakfile: " << args[3] << "\n";
                return 1;
            }
            const auto data = pak.read(*entry);
            std::cout.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            return 0;
        }
        for (const auto& e : pak.entries()) {
            std::cout << e.size << "  " << e.path << "\n";
        }
        return 0;
    }
    if (args.size() >= 3 && args[1] == "info") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
        const auto bsp = load_map(gfs, args[2]);
        const formats::ZipArchive pak(bsp.pakfile);
        std::size_t in_game = 0, in_pak = 0, missing = 0;
        for (const auto& td : bsp.texdatas) {
            const std::string vmt = "materials/" + td.material + ".vmt";
            if (gfs.exists(vmt)) {
                ++in_game;
            } else if (pak.find(vmt)) {
                ++in_pak;
            } else {
                ++missing;
                std::cout << "missing material: " << td.material << "\n";
            }
        }
        std::size_t displacements = 0;
        for (const auto& f : bsp.faces) {
            displacements += f.dispinfo >= 0;
        }
        std::map<std::string, std::size_t> classes;
        for (const auto& e : bsp.entities) {
            auto it = e.find("classname");
            ++classes[it == e.end() ? "?" : it->second];
        }
        std::cout << "version:       " << bsp.version << " (revision " << bsp.map_revision << ")\n"
                  << "faces:         " << bsp.faces.size() << " (" << displacements << " displacements)\n"
                  << "brush models:  " << bsp.models.size() << "\n"
                  << "static props:  " << bsp.static_props.size() << " using " << bsp.static_prop_models.size()
                  << " models\n"
                  << "lighting:      " << bsp.lighting.size() << " bytes\n"
                  << "pakfile:       " << pak.entries().size() << " files\n"
                  << "materials:     " << bsp.texdatas.size() << " (" << in_game << " in game data, " << in_pak
                  << " in pakfile, " << missing << " missing)\n"
                  << "entities:      " << bsp.entities.size() << "\n";
        std::vector<std::pair<std::size_t, std::string>> sorted;
        for (const auto& [name, n] : classes) {
            sorted.emplace_back(n, name);
        }
        std::sort(sorted.rbegin(), sorted.rend());
        for (std::size_t i = 0; i < sorted.size() && i < 15; ++i) {
            std::cout << "  " << sorted[i].first << "  " << sorted[i].second << "\n";
        }
        return 0;
    }
    if (args.size() >= 4 && args[1] == "export") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 4), game_mods());
        const auto bsp = load_map(gfs, args[2]);
        std::ofstream out{fs::path(args[3])};
        out << "# World geometry exported by openbl-cli\n";
        std::size_t base = 1, faces = 0;
        const auto& world = bsp.models.at(0);
        for (std::int32_t i = 0; i < world.num_faces; ++i) {
            const std::size_t fi = static_cast<std::size_t>(world.first_face + i);
            const auto& face = bsp.faces[fi];
            if (!face_is_drawn(bsp, face)) {
                continue;
            }
            const auto mesh = formats::build_face_mesh(bsp, fi);
            const auto& ti = bsp.texinfos[face.texinfo];
            out << "usemtl "
                << (ti.texdata >= 0 ? bsp.texdatas[static_cast<std::size_t>(ti.texdata)].material : "none") << "\n";
            for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
                out << "v " << mesh.positions[v][0] << " " << mesh.positions[v][1] << " " << mesh.positions[v][2]
                    << "\nvt " << mesh.uvs[v][0] << " " << 1.0f - mesh.uvs[v][1] << "\n";
            }
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                out << "f";
                for (std::size_t k = 0; k < 3; ++k) {
                    const std::size_t idx = base + mesh.indices[t + k];
                    out << " " << idx << "/" << idx;
                }
                out << "\n";
            }
            base += mesh.positions.size();
            ++faces;
        }
        std::cout << "wrote " << faces << " faces to " << args[3] << "\n";
        return out ? 0 : 1;
    }
    usage();
    return 2;
}

// Brightness and difference statistics between two screenshots (PNG or TGA),
// optionally restricted to a region given in reference-image pixels.
// Writes frame N of a movie (e.g. media/troika.bik) to a PNG.
int cmd_video(const std::vector<std::string_view>& args) {
    if (args.size() < 5 || args[1] != "frame") {
        usage();
        return 2;
    }
#ifdef OPENBL_WITH_FFMPEG
    const vfs::GameFileSystem gfs(game_dir_arg(args, 5), game_mods());
    const std::string path = formats::normalize_game_path(args[2]);
    const auto data = gfs.read(path);
    if (!data) {
        std::cerr << "not found: " << path << '\n';
        return 1;
    }
    media::VideoDecoder dec(*data);
    const auto wanted = std::stoul(std::string(args[3]));
    media::VideoFrame frame;
    for (std::size_t i = 0; i <= wanted; ++i) {
        if (!dec.next(frame)) {
            std::cerr << "the movie has only " << i << " frames\n";
            return 1;
        }
    }
    formats::write_png(fs::path(args[4]), frame.rgba, frame.width, frame.height);
    std::cout << dec.codec_name() << ' ' << frame.width << 'x' << frame.height << ", frame " << wanted << " at "
              << frame.time << " s -> " << args[4] << '\n';
    return 0;
#else
    std::cerr << "built without FFmpeg (OPENBL_WITH_FFMPEG=OFF)\n";
    return 1;
#endif
}

// Parses a save and writes it back with OpenBL's writer (round-trip testing).
int cmd_save(const std::vector<std::string_view>& args) {
    if (args.size() < 4 || args[1] != "rewrite") {
        usage();
        return 2;
    }
    std::ifstream in{fs::path(args[2]), std::ios::binary};
    const std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const auto save = formats::parse_sav(std::as_bytes(std::span(raw.data(), raw.size())));
    const auto out_bytes = formats::write_sav(save);
    std::ofstream out{fs::path(args[3]), std::ios::binary};
    out.write(reinterpret_cast<const char*>(out_bytes.data()), static_cast<std::streamsize>(out_bytes.size()));
    std::cout << "rewrote " << save.files.size() << " embedded files, " << out_bytes.size() << " bytes -> " << args[3]
              << '\n';
    return out ? 0 : 1;
}

// Decodes a game sound (WAV: PCM or Microsoft ADPCM; MP3) to a 16-bit PCM WAV file.
int cmd_audio(const std::vector<std::string_view>& args) {
    if (args.size() < 4 || args[1] != "export") {
        usage();
        return 2;
    }
    const vfs::GameFileSystem gfs(game_dir_arg(args, 4), game_mods());
    std::string path = formats::normalize_game_path(args[2]);
    if (!path.starts_with("sound/")) {
        path.insert(0, "sound/");
    }
    const auto data = gfs.read(path);
    if (!data) {
        std::cerr << "not found: " << path << "\n";
        return 1;
    }
    const auto clip = path.ends_with(".mp3") ? formats::decode_mp3(*data) : formats::decode_wav(*data);
    const auto bytes = static_cast<std::uint32_t>(clip.samples.size() * 2);
    std::ofstream out{fs::path(args[3]), std::ios::binary};
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(clip.channels);
    u32(clip.sample_rate);
    u32(clip.sample_rate * clip.channels * 2);
    u16(static_cast<std::uint16_t>(clip.channels * 2));
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(clip.samples.data()), static_cast<std::streamsize>(bytes));
    std::cout << "wrote " << clip.frames() << " frames, " << clip.channels << " channel(s) at " << clip.sample_rate
              << " Hz\n";
    return out ? 0 : 1;
}

int cmd_image(const std::vector<std::string_view>& args) {
    if (args.size() < 4 || args[1] != "compare") {
        usage();
        return 2;
    }
    const auto a = formats::read_image(fs::path(args[2]));
    const auto b = formats::read_image(fs::path(args[3]));
    if (a.width != b.width || a.height != b.height) {
        std::cerr << "image sizes differ: " << a.width << "x" << a.height << " vs " << b.width << "x" << b.height
                  << "\n";
        return 1;
    }
    std::uint32_t x0 = 0, y0 = 0, w = a.width, h = a.height;
    if (args.size() >= 8) {
        x0 = static_cast<std::uint32_t>(std::stoul(std::string(args[4])));
        y0 = static_cast<std::uint32_t>(std::stoul(std::string(args[5])));
        w = static_cast<std::uint32_t>(std::stoul(std::string(args[6])));
        h = static_cast<std::uint32_t>(std::stoul(std::string(args[7])));
        if (x0 + w > a.width || y0 + h > a.height) {
            std::cerr << "region outside the image\n";
            return 1;
        }
    }
    double sum_a[3] = {}, sum_b[3] = {}, abs_diff = 0, sq_diff = 0;
    for (std::uint32_t y = y0; y < y0 + h; ++y) {
        for (std::uint32_t x = x0; x < x0 + w; ++x) {
            const std::size_t i = (std::size_t{y} * a.width + x) * 4;
            for (std::size_t c = 0; c < 3; ++c) {
                sum_a[c] += a.rgba[i + c];
                sum_b[c] += b.rgba[i + c];
                const double d = static_cast<double>(b.rgba[i + c]) - a.rgba[i + c];
                abs_diff += std::abs(d);
                sq_diff += d * d;
            }
        }
    }
    const double n = static_cast<double>(w) * h;
    const char* names[] = {"red  ", "green", "blue "};
    std::cout << "region " << x0 << "," << y0 << " " << w << "x" << h << "\n";
    for (std::size_t c = 0; c < 3; ++c) {
        std::cout << "  " << names[c] << "  reference " << sum_a[c] / n << "  candidate " << sum_b[c] / n
                  << "  ratio " << (sum_a[c] > 0 ? sum_b[c] / sum_a[c] : 0.0) << "\n";
    }
    const double mse = sq_diff / (n * 3);
    std::cout << "  mean absolute difference " << abs_diff / (n * 3) << "\n"
              << "  PSNR " << (mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0) << " dB\n";
    return 0;
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

// Phase 0 progress: what OpenBL does with each file type in the game's content.
struct FormatStatus {
    std::string_view extension;
    std::string_view status;  // parsed, todo, codec, reference
    std::string_view note;
};
constexpr FormatStatus kFormats[] = {
    {".vmt", "parsed", "materials"},
    {".tth", "parsed", "texture headers + small mips"},
    {".ttz", "parsed", "texture large mips (zlib)"},
    {".mdl", "parsed", "models, animations, flexes (partial: cloth data)"},
    {".vtx", "parsed", "model triangle strips"},
    {".bsp", "parsed", "maps (partial: brushes, visibility, cubemaps, detail props, overlays)"},
    {".tga", "parsed", "particle and font images"},
    {".phy", "parsed", "collision models"},
    {".lip", "parsed", "lip-sync: words, phonemes, captions"},
    {".vcd", "parsed", "choreography scenes"},
    {".txt", "parsed", "KeyValues tables, expression tables, localisation, script and table formats"},
    {".dlg", "parsed", "dialogue trees"},
    {".ain", "parsed", "AI node graphs"},
    {".loc", "parsed", "AI graph companion number (meaning unknown)"},
    {".vfe", "parsed", "compiled facial expression sets"},
    {".fnt", "parsed", "bitmap fonts"},
    {".res", "parsed", "UI layouts (KeyValues)"},
    {".pyc", "parsed", "Python 2.1 bytecode (magic 60202); sources are shipped too"},
    {".py", "reference", "Python 2.1 level scripts (run in Phase 2)"},
    {".cfg", "parsed", "console command scripts"},
    {".lst", "parsed", "key bindings (KeyValues); rooms/woncomm are Half-Life leftovers"},
    {".scr", "parsed", "server options layout script"},
    {".gam", "parsed", "game info (KeyValues)"},
    {".bmp", "parsed", "face-poser toolbar icons (tool leftovers)"},
    {".sfk", "reference", "Sound Forge peak files (editor leftovers)"},
    {".pk", "reference", "audio editor peak files (leftovers)"},
    {".mp3", "parsed", "voice lines and music (minimp3)"},
    {".wav", "parsed", "audio: PCM and Microsoft ADPCM"},
    {".bik", "parsed", "Bink video (container; decoded with FFmpeg)"},
    {".psh", "reference", "original DX8 pixel shader assembly"},
    {".vcs", "reference", "original compiled shaders"},
    {".dll", "reference", "original engine binaries (never disassembled)"},
    {"(none)", "parsed", "scripts/dialog_* UI layouts (KeyValues)"},
    {".wc", "reference", "model compile scripts left in the data"},
    {".dsp", "reference", "Visual Studio project left in the data"},
    {".xls", "reference", "a designer's Excel sheet (stealth tuning)"},
    {".old", "reference", "backup of a Python script"},
    {".gam~", "reference", "editor backup of liblist.gam"},
    {".tmp", "reference", "leftover temporary file"},
    {".dt", "reference", "voice_ban.dt: engine voice-chat ban list"},
    {".bin", "reference", "vidcfg.bin: engine video configuration"},
    {".rc", "parsed", "console startup script"},
    {".rad", "reference", "lights.rad: texture light emission for the map compiler"},
    {".vbsp", "reference", "detail.vbsp: detail prop types for the map compiler"},
};

int cmd_fs(const std::vector<std::string_view>& args) {
    if (args.size() < 2) {
        usage();
        return 2;
    }
    const std::string_view sub = args[1];

    if (sub == "stat" || sub == "list") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 2), game_mods());
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
    if (sub == "extract" && args.size() >= 3) {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 4), game_mods());
        const fs::path out_dir(args[2]);
        // "materials/*.vmt": a prefix and an optional suffix around one '*'.
        std::string prefix = args.size() >= 4 ? std::string(args[3]) : std::string();
        std::string suffix;
        if (const auto star = prefix.find('*'); star != std::string::npos) {
            suffix = formats::normalize_game_path(prefix.substr(star + 1));
            prefix.resize(star);
        }
        prefix = formats::normalize_game_path(prefix);
        std::size_t count = 0;
        for (const auto& path : gfs.list_files()) {
            if (!path.starts_with(prefix) || !path.ends_with(suffix)) {
                continue;
            }
            const auto data = gfs.read(path);
            if (!data || !write_file(out_dir / fs::path(path), *data)) {
                std::cerr << "failed: " << path << "\n";
                continue;
            }
            ++count;
        }
        std::cout << "extracted " << count << " files to " << out_dir.string() << "\n";
        return 0;
    }
    if (sub == "coverage") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 2), game_mods());
        std::map<std::string, std::size_t> counts;
        for (const auto& path : gfs.list_files()) {
            const auto ext = fs::path(path).extension().string();
            ++counts[ext.empty() ? "(none)" : ext];
        }
        std::vector<std::pair<std::size_t, std::string>> sorted;
        for (const auto& [ext, n] : counts) {
            sorted.emplace_back(n, ext);
        }
        std::sort(sorted.rbegin(), sorted.rend());
        std::map<std::string, std::size_t> by_status;
        for (const auto& [n, ext] : sorted) {
            const auto it = std::find_if(std::begin(kFormats), std::end(kFormats),
                                         [&](const FormatStatus& f) { return f.extension == ext; });
            const std::string_view status = it != std::end(kFormats) ? it->status : "unknown";
            by_status[std::string(status)] += n;
            std::cout << std::left << std::setw(8) << ext << std::right << std::setw(7) << n << "  " << std::left
                      << std::setw(10) << status << (it != std::end(kFormats) ? it->note : std::string_view()) << "\n";
        }
        std::cout << "\n";
        for (const auto& [status, n] : by_status) {
            std::cout << std::left << std::setw(10) << status << std::right << std::setw(7) << n << " files\n";
        }
        return 0;
    }
    if ((sub == "which" || sub == "cat") && args.size() >= 3) {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3), game_mods());
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
        if (args[0] == "mat") {
            return cmd_mat(args);
        }
        if (args[0] == "mdl") {
            return cmd_mdl(args);
        }
        if (args[0] == "bsp") {
            return cmd_bsp(args);
        }
        if (args[0] == "image") {
            return cmd_image(args);
        }
        if (args[0] == "video") {
            return cmd_video(args);
        }
        if (args[0] == "save") {
            return cmd_save(args);
        }
        if (args[0] == "audio") {
            return cmd_audio(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    usage();
    return 2;
}
