#include "openbl/formats/vmt.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"

namespace openbl::formats {

namespace {

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

} // namespace

std::optional<std::string_view> Material::param(std::string_view name) const {
    auto it = params.find(to_lower(name));
    if (it == params.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> Material::texture_path(std::string_view name) const {
    auto value = param(name);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return material_texture_path(*value);
}

std::string material_texture_path(std::string_view value) {
    std::string path = normalize_game_path(value);
    for (std::string_view ext : {".vtf", ".tth", ".ttz"}) {
        if (path.ends_with(ext)) {
            path.erase(path.size() - ext.size());
            break;
        }
    }
    if (!path.starts_with("materials/")) {
        path.insert(0, "materials/");
    }
    return path;
}

Material parse_vmt(std::string_view text) {
    KvDocument doc = parse_keyvalues(text);
    if (doc.roots.empty() || !doc.roots.front().is_block) {
        throw FormatError("material has no shader block");
    }
    if (doc.roots.size() > 1) {
        doc.diagnostics.push_back({doc.roots[1].line, "content after the shader block is ignored"});
    }

    KvNode& root = doc.roots.front();
    Material mat;
    mat.shader = to_lower(root.key);
    for (KvNode& child : root.children) {
        if (child.is_block) {
            if (iequals(child.key, "proxies")) {
                for (KvNode& proxy : child.children) {
                    mat.proxies.push_back(std::move(proxy));
                }
            } else {
                mat.sub_blocks.push_back(std::move(child));
            }
            continue;
        }
        mat.params[to_lower(child.key)] = std::move(child.value);
    }
    mat.diagnostics = std::move(doc.diagnostics);
    return mat;
}

} // namespace openbl::formats
