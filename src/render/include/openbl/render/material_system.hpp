#pragma once

#include "openbl/render/asset_source.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace openbl::render {

// What the world shader needs to know about a material.
struct RenderMaterial {
    std::string name;
    std::string shader;
    const rhi::Texture* base = nullptr;  // Never null; a checkerboard if missing.
    const rhi::Texture* base2 = nullptr; // Second layer for WorldVertexTransition; equals base otherwise.
    bool blend_layers = false;           // WorldVertexTransition: blend base -> base2 by vertex alpha.
    bool alpha_test = false;
    float alpha_ref = 0.5f;
    bool translucent = false;
    bool additive = false;
    bool self_illum = false; // Base alpha masks full-bright areas.
    bool unlit = false;      // Ignores lightmaps (UnlitGeneric and friends).
    bool no_cull = false;
    bool skip = false;       // Never drawn (tool materials).
    bool missing = false;    // The .vmt could not be found or parsed.

    // Water shader parameters (linear RGB).
    bool water = false;
    const rhi::Texture* normal_map = nullptr; // Linear; a flat normal if missing.
    std::array<float, 3> fog_color{0.05f, 0.06f, 0.06f};
    std::array<float, 3> reflect_tint{1.0f, 1.0f, 1.0f};
};

// Parses a Source colour parameter: "{r g b}" in 0-255 or "[r g b]" in 0-1.
std::optional<std::array<float, 3>> parse_vmt_color(std::string_view value);

class MaterialSystem {
public:
    MaterialSystem(rhi::Device& device, const AssetSource& assets);
    ~MaterialSystem();

    // `name` is relative to materials/, without extension. Results are cached.
    const RenderMaterial& material(std::string_view name);
    // `path` is a texture game path without extension, e.g. materials/brick/wall. Null if missing.
    // Colour textures are sRGB; data textures (normal maps) must pass srgb = false.
    const rhi::Texture* texture(const std::string& path, bool srgb = true);

    std::size_t missing_textures() const { return missing_textures_; }

private:
    RenderMaterial load(const std::string& name);

    rhi::Device& device_;
    const AssetSource& assets_;
    std::unique_ptr<rhi::Texture> checker_;
    std::unique_ptr<rhi::Texture> flat_normal_;
    std::unordered_map<std::string, std::unique_ptr<rhi::Texture>> textures_; // Null entries cache misses.
    std::unordered_map<std::string, RenderMaterial> materials_;
    std::size_t missing_textures_ = 0;
};

} // namespace openbl::render
