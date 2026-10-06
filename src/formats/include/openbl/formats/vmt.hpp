#pragma once

// Materials (.vmt): a shader name plus parameters, in KeyValues syntax.
// See docs/formats/materials.md.

#include "openbl/formats/keyvalues.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::formats {

struct Material {
    std::string shader; // Lowercase, e.g. "vertexlitgeneric".
    // Lowercase parameter name -> value. When a parameter repeats, the last value wins.
    std::unordered_map<std::string, std::string> params;
    std::vector<KvNode> proxies;    // Children of the "Proxies" block, in file order.
    std::vector<KvNode> sub_blocks; // Other nested blocks, e.g. "LightmappedGeneric_DX6" fallbacks.
    std::vector<KvDiagnostic> diagnostics;

    // Case-insensitive.
    std::optional<std::string_view> param(std::string_view name) const;

    // Resolves a texture parameter to the texture's game path without extension,
    // e.g. "models\foo\bar" -> "materials/models/foo/bar".
    std::optional<std::string> texture_path(std::string_view name) const;
};

// Throws FormatError if the text has no shader block at all. Other problems
// are recovered from and listed in Material::diagnostics.
Material parse_vmt(std::string_view text);

// Game path of a texture referenced from a material, without extension.
std::string material_texture_path(std::string_view value);

} // namespace openbl::formats
