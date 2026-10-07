#pragma once

#include "openbl/formats/vmt.hpp"
#include "openbl/formats/vmt_proxies.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::render {

// What the world shader needs to know about a material.
struct RenderMaterial {
    std::string name;
    std::string shader;
    const rhi::Texture* base = nullptr;  // Never null; a checkerboard if missing.
    const rhi::Texture* base2 = nullptr; // Second layer (WorldVertexTransition, UnlitTwoTexture); equals base otherwise.
    bool blend_layers = false;           // WorldVertexTransition: blend base -> base2 by vertex alpha.
    bool multiply_layers = false;        // UnlitTwoTexture: base x base2, each with its own transform.
    bool alpha_test = false;
    float alpha_ref = 0.5f;
    bool translucent = false;
    bool additive = false;
    bool self_illum = false; // Base alpha masks full-bright areas.
    bool unlit = false;      // Ignores lightmaps (UnlitGeneric and friends).
    bool no_cull = false;
    bool skip = false;       // Never drawn (tool materials).
    float decal_scale = 1.0f; // $decalscale: world units per base-texture texel when used as a decal.
    bool modulate = false;    // DecalModulate: multiplies what is behind it by 2 x the texture.
    bool missing = false;    // The .vmt could not be found or parsed.
    bool camera_feed = false; // $basetexture is the camera render target (func_monitor screens).

    // Water shader parameters (linear RGB).
    bool water = false;
    const rhi::Texture* normal_map = nullptr; // Linear; a flat normal if missing.
    std::array<float, 3> fog_color{0.05f, 0.06f, 0.06f};
    std::array<float, 3> reflect_tint{1.0f, 1.0f, 1.0f};

    // Animation from material proxies, updated by MaterialSystem::update.
    // Frames of an animated base texture or water normal map (empty if not
    // animated; frame 0 is also in `base` / `normal_map`).
    std::vector<const rhi::Texture*> base_frames;
    std::vector<const rhi::Texture*> normal_frames;
    std::uint32_t base_frame = 0;
    std::uint32_t normal_frame = 0;
    // Row of MaterialSystem::params() with the texture transform and tints;
    // 0 for materials with the defaults.
    std::uint32_t params_index = 0;
};

// Per-material values the shaders read from a table, one row per material
// that needs them (row 0: defaults). Colours are linear. Shaders index rows
// as 6 float4 each, in this order.
struct MaterialParams {
    std::array<float, 4> transform_u{1, 0, 0, 0}; // $basetexturetransform rows: u' = x u + y v + z.
    std::array<float, 4> transform_v{0, 1, 0, 0};
    std::array<float, 4> color{1, 1, 1, 1};       // $color, $alpha.
    std::array<float, 4> self_illum_tint{1, 1, 1, 0};
    std::array<float, 4> transform2_u{1, 0, 0, 0}; // $texture2transform (UnlitTwoTexture).
    std::array<float, 4> transform2_v{0, 1, 0, 0};
};
static_assert(sizeof(MaterialParams) == 96);

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

    // Every frame of an animated texture (one entry if not animated; empty if missing).
    std::vector<const rhi::Texture*> texture_frames(const std::string& path, bool srgb = true);

    std::size_t missing_textures() const { return missing_textures_; }

    // Runs the material proxies of every loaded material and refreshes their
    // frames and params() rows.
    void update(const formats::ProxyContext& context);
    const std::vector<MaterialParams>& params() const { return params_; }
    // Loaded materials whose proxies run, and how many of them animate a texture.
    std::size_t proxy_materials() const { return dynamic_.size(); }
    std::size_t animated_materials() const;

private:
    struct Dynamic {
        RenderMaterial* material;
        formats::MaterialProxies proxies;
        std::string base_frame_var, normal_frame_var; // Empty if not animated.
    };

    RenderMaterial load(const std::string& name, std::optional<formats::Material>& source);
    void register_dynamic(RenderMaterial& material, const formats::Material& source);
    std::optional<formats::VtfTexture> read_vtf(const std::string& path);

    rhi::Device& device_;
    const AssetSource& assets_;
    std::unique_ptr<rhi::Texture> checker_;
    std::unique_ptr<rhi::Texture> flat_normal_;
    std::unordered_map<std::string, std::unique_ptr<rhi::Texture>> textures_; // Null entries cache misses.
    std::unordered_map<std::string, std::vector<const rhi::Texture*>> frames_;
    std::unordered_map<std::string, RenderMaterial> materials_;
    std::vector<Dynamic> dynamic_;
    std::vector<MaterialParams> params_{MaterialParams{}};
    std::size_t missing_textures_ = 0;
};

} // namespace openbl::render
