#pragma once

// Internal: resolving a studio model's texture references to material names.

#include "openbl/formats/mdl.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/render/asset_source.hpp"

#include <string>

namespace openbl::render::detail {

// Material name (relative to materials/, no extension) for a model texture:
// the first texture directory where the .vmt exists, else the first candidate.
inline std::string model_material_name(const AssetSource& assets, const formats::MdlFile& mdl, std::int32_t texture) {
    if (texture < 0) {
        return {};
    }
    const std::string& tex = mdl.textures[static_cast<std::size_t>(texture)];
    std::string first;
    for (const std::string& dir : mdl.texture_dirs) {
        const std::string name = formats::normalize_game_path(dir + "/" + tex);
        if (first.empty()) {
            first = name;
        }
        if (assets.exists("materials/" + name + ".vmt")) {
            return name;
        }
    }
    return first.empty() ? formats::normalize_game_path(tex) : first;
}

} // namespace openbl::render::detail
