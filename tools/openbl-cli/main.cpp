// openbl-cli: inspection and extraction tool for Bloodlines game data.

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/error.hpp"
#include "openbl/formats/zip.hpp"
#include "openbl/formats/image_decode.hpp"
#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/mdl.hpp"
#include "openbl/formats/vtx.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vmt.hpp"
#include "openbl/formats/vpk.hpp"
#include "openbl/formats/vtf.hpp"

#include <algorithm>
#include "openbl/vfs/game_filesystem.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
                 "  openbl-cli fs which <path> [game-dir]\n"
                 "  openbl-cli fs cat <path> [game-dir]\n"
                 "  openbl-cli tex info <texture> [game-dir]\n"
                 "  openbl-cli tex export <texture> <out.tga> [game-dir]\n"
                 "  openbl-cli mat info <material> [game-dir]\n"
                 "  openbl-cli mdl info <model> [game-dir]\n"
                 "  openbl-cli mdl export <model> <out.obj> [game-dir]\n"
                 "  openbl-cli mdl sequences <model> [game-dir]\n"
                 "  openbl-cli mdl pose <model> <sequence> <frame> <out.obj> [game-dir]\n"
                 "  openbl-cli bsp info <map> [game-dir]\n"
                 "  openbl-cli bsp export <map> <out.obj> [game-dir]\n"
                 "\n"
                 "<texture> and <material> are game paths with or without extension, e.g.\n"
                 "materials/brick/bradwllb; the materials/ prefix is optional for <material>.\n"
                 "\n"
                 "game-dir defaults to the OPENBL_GAME_DIR environment variable.\n";
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

int cmd_mat(const std::vector<std::string_view>& args) {
    if (args.size() < 3 || args[1] != "info") {
        usage();
        return 2;
    }
    const vfs::GameFileSystem gfs(game_dir_arg(args, 3));
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

int cmd_mdl(const std::vector<std::string_view>& args) {
    if (args.size() >= 3 && args[1] == "sequences") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3));
        list_sequences(gfs, model_path(args[2]));
        return 0;
    }
    if (args.size() >= 6 && args[1] == "pose") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 6));
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
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3));
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
        const vfs::GameFileSystem gfs(game_dir_arg(args, 4));
        const auto model = load_model(gfs, args[2]);
        std::ofstream out{fs::path(args[3])};
        out << "# Exported by openbl-cli from " << model.mdl.name << " (first model of each body part, LOD 0)\n";
        std::size_t base = 1;
        for (std::size_t b = 0; b < model.mdl.body_parts.size(); ++b) {
            const auto& part = model.mdl.body_parts[b];
            if (part.models.empty()) {
                continue;
            }
            const auto& mo = part.models[0];
            out << "o " << part.name << "\n";
            for (const auto& v : mo.vertices) {
                out << "v " << v.position[0] << " " << v.position[1] << " " << v.position[2] << "\n";
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
    if (args.size() >= 3 && args[1] == "info") {
        const vfs::GameFileSystem gfs(game_dir_arg(args, 3));
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
        const vfs::GameFileSystem gfs(game_dir_arg(args, 4));
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
        if (args[0] == "mat") {
            return cmd_mat(args);
        }
        if (args[0] == "mdl") {
            return cmd_mdl(args);
        }
        if (args[0] == "bsp") {
            return cmd_bsp(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    usage();
    return 2;
}
