#pragma once

#include "openbl/formats/bsp.hpp"
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
    bool eyes = false;                   // Eyes: base2 is the $iris, drawn over the eyeball (docs/RENDERING.md).
    bool alpha_test = false;
    float alpha_ref = 0.5f;
    bool translucent = false;
    bool additive = false;
    bool self_illum = false; // Base alpha masks full-bright areas.
    bool unlit = false;      // Ignores lightmaps (UnlitGeneric and friends).
    bool no_cull = false;
    bool skip = false;       // Never drawn (tool materials).
    float decal_scale = 1.0f; // $decalscale: world units per base-texture texel when used as a decal.
    std::string model_material; // $modelmaterial: the variant used when this decal lands on a model.
    bool modulate = false;    // DecalModulate: multiplies what is behind it by 2 x the texture.
    bool missing = false;    // The .vmt could not be found or parsed.
    bool camera_feed = false; // $basetexture is the camera render target (func_monitor screens).
    // Cable shader: the range its lighting is clamped to, as encoded values
    // ($minlight, $maxlight; docs/RENDERING.md#ropes-and-cables).
    float min_light = 0.0f;
    float max_light = 1.0f;

    // Sprite shader: how the quad faces the camera ($spriteorientation) and
    // the point of the picture placed at the entity's origin ($spriteorigin, 0-1).
    enum class SpriteOrientation : std::uint8_t {
        ViewParallel,         // vp_parallel: in the view plane (the default).
        ParallelUpright,      // parallel_upright: turns about world z to face the camera.
        Oriented,             // oriented: fixed by the entity's angles.
        ViewParallelOriented, // vp_parallel_oriented: in the view plane, rolled by the entity's angles.
    } sprite_orientation = SpriteOrientation::ViewParallel;
    std::array<float, 2> sprite_origin{0.5f, 0.5f};

    // Environment map ($envmap): a cube map added after lighting, times
    // $envmaptint and the mask (docs/RENDERING.md). Null if none.
    const rhi::Texture* envmap = nullptr;
    bool envmap_nearest = false; // "env_cubemap": models use the cubemap nearest to them instead of `envmap`.
    const rhi::Texture* envmap_mask = nullptr; // $envmapmask (raw values), or the bump map with normal-alpha masks; white otherwise.
    enum class EnvmapMask : std::uint8_t {
        None,       // Full strength.
        Texture,    // $envmapmask's colour.
        BaseAlpha,  // $basealphaenvmapmask: 1 - base alpha.
        MaskAlpha,  // $normalmapalphaenvmapmask: the bump map's alpha (envmap_mask is the bump map).
    } envmap_mask_mode = EnvmapMask::None;

    // Water shader parameters. Colours are as authored (gamma-encoded): the
    // original multiplies encoded render-target colour by them.
    bool water = false;
    // $bottommaterial: what the same surface is drawn with from below the
    // water (often the material itself). Null if it names none.
    const RenderMaterial* bottom = nullptr;
    const rhi::Texture* bumpmap = nullptr;    // $bumpmap of LightmappedGeneric, WorldVertexTransition, VertexLitGeneric, Cable (linear).
    // $detail: raw (encoded) texels multiplied in at 2x, at the untransformed
    // base coordinates times $detailscale (docs/RENDERING.md). Null if none.
    const rhi::Texture* detail = nullptr;
    const rhi::Texture* normal_map = nullptr; // $normalmap, linear; a flat normal if missing.
    const rhi::Texture* dudv = nullptr;       // $bumpmap: signed offsets, biased (0.5 = none).
    bool reflect = false;                     // Has $reflecttexture.
    bool refract = false;                     // Has $refracttexture.
    std::array<float, 3> reflect_tint{1.0f, 1.0f, 1.0f};
    std::array<float, 3> refract_tint{1.0f, 1.0f, 1.0f};
    float reflect_amount = 0.0f; // World units of displacement per unit of dudv offset.
    float refract_amount = 0.0f;
    std::array<float, 3> fog_color{0.0f, 0.0f, 0.0f}; // Below the surface ($fogcolor, $fogstart, $fogend).
    float fog_start = 0.0f, fog_end = 0.0f;
    // Cheap water: blends to fog colour + `envmap` between these distances
    // from the eye ($cheapwaterstartdistance, $cheapwaterenddistance; both 0:
    // never), or everywhere with $forcecheap.
    float cheap_start = 0.0f, cheap_end = 0.0f;
    bool force_cheap = false;
    // $bumptransform or $bumpoffset (scrolled by TextureScroll): u' = [0] u + [1] v + [2], v' = [3] u + [4] v + [5].
    std::array<float, 6> bump_transform{1, 0, 0, 0, 1, 0};

    // Animation from material proxies, updated by MaterialSystem::update.
    // Frames of an animated base texture or water normal map (empty if not
    // animated; frame 0 is also in `base` / `normal_map`). Water dudv frames
    // follow the normal map's frame.
    std::vector<const rhi::Texture*> base_frames;
    std::vector<const rhi::Texture*> normal_frames;
    std::vector<const rhi::Texture*> dudv_frames;
    std::uint32_t base_frame = 0;
    std::uint32_t normal_frame = 0;
    // Row of MaterialSystem::params() with the texture transform and tints;
    // 0 for materials with the defaults.
    std::uint32_t params_index = 0;
};

// Per-material values the shaders read from a table, one row per material
// that needs them (row 0: defaults). Colours are linear unless noted. Shaders
// index rows as 8 float4 each, in this order.
struct MaterialParams {
    std::array<float, 4> transform_u{1, 0, 0, 0}; // $basetexturetransform rows: u' = x u + y v + z.
    std::array<float, 4> transform_v{0, 1, 0, 0};
    std::array<float, 4> color{1, 1, 1, 1};       // $color, $alpha.
    std::array<float, 4> self_illum_tint{1, 1, 1, 0};
    std::array<float, 4> transform2_u{1, 0, 0, 0}; // $texture2transform (UnlitTwoTexture).
    std::array<float, 4> transform2_v{0, 1, 0, 0};
    std::array<float, 4> envmap_tint{1, 1, 1, 0};   // $envmaptint (encoded, as authored); w: $envmapcontrast.
    std::array<float, 4> envmap_tone{1, 4, 0, 0};   // x: $envmapsaturation; y: $detailscale.
};
static_assert(sizeof(MaterialParams) == 128);

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

    // The map's cubemaps (materials/maps/<map>/c<x>_<y>_<z>, and cubemapdefault
    // for "$envmap env_cubemap"). Set before loading materials. World faces
    // use per-cubemap copies of their materials; models take the nearest.
    void set_map(const std::string& map_name, const std::vector<formats::BspCubemap>& cubemaps);
    // The cube map nearest to `position` (the default one if the map has none); null without a map.
    const rhi::Texture* nearest_cubemap(const std::array<float, 3>& position);
    // A black cube map and a white texture, for materials without an
    // environment map or mask.
    const rhi::Texture* black_cube() const { return black_cube_.get(); }
    const rhi::Texture* white() const { return white_.get(); }

    // Runs the material proxies of every loaded material and refreshes their
    // frames and params() rows.
    void update(const formats::ProxyContext& context);
    const std::vector<MaterialParams>& params() const { return params_; }
    // A material's $alpha for one entity: its proxies run with `context`
    // (the entity's distance and direction from the camera), e.g. sprites
    // with PlayerProximity, LampBeam or LampHalo. Without proxies, its static $alpha.
    float entity_alpha(const RenderMaterial& material, const formats::ProxyContext& context);
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
    const rhi::Texture* sphere_cubemap(const std::string& path);

    rhi::Device& device_;
    const AssetSource& assets_;
    std::unique_ptr<rhi::Texture> checker_;
    std::unique_ptr<rhi::Texture> flat_normal_;
    std::unique_ptr<rhi::Texture> black_cube_;
    std::unique_ptr<rhi::Texture> white_;
    std::string default_envmap_;
    std::string map_dir_; // materials/maps/<map>/
    std::vector<formats::BspCubemap> cubemaps_;
    std::unordered_map<std::string, std::unique_ptr<rhi::Texture>> textures_; // Null entries cache misses.
    std::unordered_map<std::string, std::vector<const rhi::Texture*>> frames_;
    std::unordered_map<std::string, RenderMaterial> materials_;
    std::vector<Dynamic> dynamic_;
    std::vector<MaterialParams> params_{MaterialParams{}};
    std::size_t missing_textures_ = 0;
};

} // namespace openbl::render
