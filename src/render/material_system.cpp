#include "openbl/render/material_system.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vmt.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/render/texture_upload.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace openbl::render {

namespace {

bool flag(const formats::Material& m, std::string_view name) {
    auto v = m.param(name);
    return v && std::atoi(std::string(*v).c_str()) != 0;
}

std::optional<formats::Material> read_material(const AssetSource& assets, const std::string& path, int depth = 0) {
    const auto data = assets.read(path);
    if (!data) {
        return std::nullopt;
    }
    auto mat = formats::parse_vmt(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    if (formats::is_vmt_patch(mat)) {
        if (depth > 4) {
            throw formats::FormatError("patch materials nested too deeply: " + path);
        }
        auto base = read_material(assets, formats::vmt_patch_include(mat), depth + 1);
        if (!base) {
            return std::nullopt;
        }
        return formats::apply_vmt_patch(*base, mat);
    }
    return mat;
}

} // namespace

std::optional<std::array<float, 3>> parse_vmt_color(std::string_view value) {
    if (value.size() < 2) {
        return std::nullopt;
    }
    const bool bytes = value.front() == '{';
    if (!bytes && value.front() != '[') {
        return std::nullopt;
    }
    std::array<float, 3> c{};
    std::istringstream in(std::string(value.substr(1)));
    for (float& f : c) {
        if (!(in >> f)) {
            return std::nullopt;
        }
        f = bytes ? f / 255.0f : f;
    }
    return c;
}

MaterialSystem::MaterialSystem(rhi::Device& device, const AssetSource& assets)
    : device_(device), assets_(assets), checker_(create_checker_texture(device)) {
    const std::uint8_t flat[4] = {128, 128, 255, 255};
    const std::span<const std::byte> mip = std::as_bytes(std::span(flat));
    flat_normal_ = device.create_texture({1, 1, 1, rhi::Format::RGBA8_UNorm}, std::span(&mip, 1));
    const std::uint8_t white[4] = {255, 255, 255, 255};
    const std::span<const std::byte> white_mip = std::as_bytes(std::span(white));
    white_ = device.create_texture({1, 1, 1, rhi::Format::RGBA8_UNorm}, std::span(&white_mip, 1));
    const std::array<std::uint8_t, 24> black{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
                                             0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    const std::span<const std::byte> cube_mip = std::as_bytes(std::span(black));
    rhi::TextureDesc cube{1, 1, 1, rhi::Format::RGBA8_UNorm};
    cube.cube = true;
    black_cube_ = device.create_texture(cube, std::span(&cube_mip, 1));
}

MaterialSystem::~MaterialSystem() = default;

void MaterialSystem::set_map(const std::string& map_name, const std::vector<formats::BspCubemap>& cubemaps) {
    map_dir_ = "materials/maps/" + map_name + "/";
    default_envmap_ = map_dir_ + "cubemapdefault";
    cubemaps_ = cubemaps;
}

const rhi::Texture* MaterialSystem::nearest_cubemap(const std::array<float, 3>& position) {
    const formats::BspCubemap* best = nullptr;
    float best_d2 = 0.0f;
    for (const auto& c : cubemaps_) {
        float d2 = 0.0f;
        for (std::size_t k = 0; k < 3; ++k) {
            const float d = static_cast<float>(c.origin[k]) - position[k];
            d2 += d * d;
        }
        if (!best || d2 < best_d2) {
            best = &c;
            best_d2 = d2;
        }
    }
    if (best) {
        const std::string path = map_dir_ + "c" + std::to_string(best->origin[0]) + "_" + std::to_string(best->origin[1]) +
                                 "_" + std::to_string(best->origin[2]);
        if (const auto* t = texture(path); t && t->desc().cube) {
            return t;
        }
    }
    if (default_envmap_.empty()) {
        return nullptr;
    }
    const auto* t = texture(default_envmap_);
    return t && t->desc().cube ? t : nullptr;
}

std::optional<formats::VtfTexture> MaterialSystem::read_vtf(const std::string& path) {
    try {
        if (auto tth = assets_.read(path + ".tth")) {
            const auto ttz = assets_.read(path + ".ttz");
            auto assembled =
                formats::assemble_vtf(*tth, ttz ? std::optional(std::span<const std::byte>(*ttz)) : std::nullopt);
            return formats::VtfTexture::parse(std::move(assembled.data));
        }
        if (auto vtf = assets_.read(path + ".vtf")) {
            return formats::VtfTexture::parse(std::move(*vtf));
        }
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), e.what());
    }
    return std::nullopt;
}

const rhi::Texture* MaterialSystem::texture(const std::string& path, bool srgb) {
    const std::string key = srgb ? path : path + "#linear";
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second.get();
    }
    std::unique_ptr<rhi::Texture> tex;
    try {
        if (auto vtf = read_vtf(path)) {
            tex = upload_vtf(device_, *vtf, srgb);
        }
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), e.what());
    }
    if (!tex) {
        ++missing_textures_;
    }
    return textures_.emplace(key, std::move(tex)).first->second.get();
}

std::vector<const rhi::Texture*> MaterialSystem::texture_frames(const std::string& path, bool srgb) {
    const std::string key = srgb ? path : path + "#linear";
    if (auto it = frames_.find(key); it != frames_.end()) {
        return it->second;
    }
    std::vector<const rhi::Texture*> frames;
    if (const auto* first = texture(path, srgb)) {
        frames.push_back(first);
        if (auto vtf = read_vtf(path)) {
            for (std::uint32_t f = 1; f < vtf->header().frame_count; ++f) {
                try {
                    auto tex = upload_vtf(device_, *vtf, srgb, f);
                    frames.push_back(tex.get());
                    textures_.emplace(key + "#frame" + std::to_string(f), std::move(tex));
                } catch (const formats::FormatError& e) {
                    std::fprintf(stderr, "texture %s frame %u: %s\n", path.c_str(), f, e.what());
                    break;
                }
            }
        }
    }
    return frames_.emplace(key, std::move(frames)).first->second;
}

const RenderMaterial& MaterialSystem::material(std::string_view name) {
    const std::string key = formats::normalize_game_path(name);
    auto it = materials_.find(key);
    if (it != materials_.end()) {
        return it->second;
    }
    std::optional<formats::Material> source;
    RenderMaterial& out = materials_.emplace(key, load(key, source)).first->second;
    if (source) {
        register_dynamic(out, *source);
    }
    return out;
}

namespace {

std::array<float, 3> to_linear(std::array<float, 3> c) {
    for (float& f : c) {
        f = std::pow(std::max(f, 0.0f), 2.2f);
    }
    return c;
}

// The params row described by a material's variables (static values, or
// values after its proxies ran).
MaterialParams params_from(const formats::MaterialProxies& vars) {
    MaterialParams p;
    if (const auto* t = vars.var("$basetexturetransform"); t && t->is_matrix) {
        p.transform_u = {t->m[0], t->m[1], t->m[2], 0};
        p.transform_v = {t->m[3], t->m[4], t->m[5], 0};
    }
    if (const auto* t = vars.var("$texture2transform"); t && t->is_matrix) {
        p.transform2_u = {t->m[0], t->m[1], t->m[2], 0};
        p.transform2_v = {t->m[3], t->m[4], t->m[5], 0};
    }
    auto rgb = [](const formats::ProxyVar& v) {
        return v.count >= 3 ? std::array<float, 3>{v.v[0], v.v[1], v.v[2]} : std::array<float, 3>{v.v[0], v.v[0], v.v[0]};
    };
    // Material colours are authored in gamma space; the shaders work in linear.
    if (const auto* c = vars.var("$color"); c && !c->is_matrix) {
        const auto lin = to_linear(rgb(*c));
        p.color = {lin[0], lin[1], lin[2], 1};
    }
    if (const auto* a = vars.var("$alpha"); a && !a->is_matrix) {
        p.color[3] = std::clamp(a->v[0], 0.0f, 1.0f);
    }
    if (const auto* s = vars.var("$selfillumtint"); s && !s->is_matrix) {
        const auto lin = to_linear(rgb(*s));
        p.self_illum_tint = {lin[0], lin[1], lin[2], 0};
    }
    // The original multiplies the encoded envmap colour by these as authored.
    if (const auto* t = vars.var("$envmaptint"); t && !t->is_matrix) {
        const auto c = rgb(*t);
        p.envmap_tint = {c[0], c[1], c[2], p.envmap_tint[3]};
    }
    if (const auto* c = vars.var("$envmapcontrast"); c && !c->is_matrix) {
        p.envmap_tint[3] = c->v[0];
    }
    if (const auto* s = vars.var("$envmapsaturation"); s && !s->is_matrix) {
        p.envmap_tone[0] = s->v[0];
    }
    return p;
}

// Water's dudv and normal map transform: $bumptransform or $bumpoffset
// (materials scroll either), a matrix once TextureScroll has run, else an offset.
std::array<float, 6> bump_transform(const formats::MaterialProxies& vars) {
    const auto* v = vars.var("$bumptransform");
    if (!v || !v->is_matrix) {
        v = vars.var("$bumpoffset");
    }
    if (!v) {
        return {1, 0, 0, 0, 1, 0};
    }
    if (v->is_matrix) {
        return v->m;
    }
    return {1, 0, v->v[0], 0, 1, v->count >= 2 ? v->v[1] : v->v[0]};
}

bool is_default(const MaterialParams& p) {
    const MaterialParams d;
    return p.transform_u == d.transform_u && p.transform_v == d.transform_v && p.color == d.color &&
           p.self_illum_tint == d.self_illum_tint && p.transform2_u == d.transform2_u &&
           p.transform2_v == d.transform2_v && p.envmap_tint == d.envmap_tint && p.envmap_tone == d.envmap_tone;
}

} // namespace

void MaterialSystem::register_dynamic(RenderMaterial& material, const formats::Material& source) {
    formats::MaterialProxies proxies(source, static_cast<std::uint32_t>(dynamic_.size() + 1));
    std::string base_var, normal_var;
    for (const auto& [texture_var, frame_var] : proxies.animated_textures()) {
        const bool normal = texture_var == "$normalmap" || texture_var == "$bumpmap";
        if (texture_var != "$basetexture" && !normal) {
            continue; // Other layers are not animated in OpenBL yet.
        }
        const auto path = source.texture_path(texture_var);
        if (!path) {
            continue;
        }
        auto frames = texture_frames(*path, !normal);
        if (frames.size() <= 1) {
            continue;
        }
        proxies.set_frame_count(texture_var, static_cast<std::uint32_t>(frames.size()));
        if (material.water && texture_var == "$bumpmap") {
            // Water steps its dudv map ($bumpmap) and normal map together.
            material.dudv_frames = std::move(frames);
            if (const auto normal_path = source.texture_path("$normalmap")) {
                material.normal_frames = texture_frames(*normal_path, false);
            }
        } else {
            (normal ? material.normal_frames : material.base_frames) = std::move(frames);
        }
        (normal ? normal_var : base_var) = frame_var;
    }
    const MaterialParams initial = params_from(proxies);
    if (proxies.empty() && is_default(initial)) {
        return;
    }
    material.params_index = static_cast<std::uint32_t>(params_.size());
    params_.push_back(initial);
    if (!proxies.empty()) {
        dynamic_.push_back({&material, std::move(proxies), base_var, normal_var});
    }
}

std::size_t MaterialSystem::animated_materials() const {
    return static_cast<std::size_t>(std::count_if(dynamic_.begin(), dynamic_.end(), [](const Dynamic& d) {
        return !d.base_frame_var.empty() || !d.normal_frame_var.empty();
    }));
}

void MaterialSystem::update(const formats::ProxyContext& context) {
    for (Dynamic& d : dynamic_) {
        d.proxies.evaluate(context);
        params_[d.material->params_index] = params_from(d.proxies);
        auto frame = [&](const std::string& var, std::size_t count) -> std::uint32_t {
            const auto* v = var.empty() ? nullptr : d.proxies.var(var);
            if (!v || count == 0) {
                return 0;
            }
            return static_cast<std::uint32_t>(std::max(0.0f, v->v[0])) % static_cast<std::uint32_t>(count);
        };
        d.material->base_frame = frame(d.base_frame_var, d.material->base_frames.size());
        d.material->normal_frame = frame(d.normal_frame_var,
                                         std::max(d.material->normal_frames.size(), d.material->dudv_frames.size()));
        if (d.material->water) {
            d.material->bump_transform = bump_transform(d.proxies);
        }
    }
}

RenderMaterial MaterialSystem::load(const std::string& name, std::optional<formats::Material>& mat) {
    RenderMaterial out;
    out.name = name;
    out.base = out.base2 = checker_.get();

    try {
        mat = read_material(assets_, "materials/" + name + ".vmt");
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "material %s: %s\n", name.c_str(), e.what());
    }
    if (!mat) {
        out.missing = true;
        return out;
    }
    out.shader = mat->shader;
    out.camera_feed = mat->texture_path("$basetexture") == "materials/engine/camerarendertarget";
    if (auto path = mat->texture_path("$basetexture")) {
        if (const auto* t = texture(*path)) {
            out.base = out.base2 = t;
        }
    }
    if (out.shader == "worldvertextransition") {
        if (auto path = mat->texture_path("$basetexture2")) {
            if (const auto* t = texture(*path)) {
                out.base2 = t;
                out.blend_layers = true;
            }
        }
    }
    if (out.shader == "eyes") {
        if (auto path = mat->texture_path("$iris")) {
            if (const auto* t = texture(*path)) {
                out.base2 = t;
                out.eyes = true;
            }
        }
    }
    if (out.shader == "unlittwotexture") {
        if (auto path = mat->texture_path("$texture2")) {
            if (const auto* t = texture(*path)) {
                out.base2 = t;
                out.multiply_layers = true;
            }
        }
    }
    out.alpha_test = flag(*mat, "$alphatest");
    if (auto ref = mat->param("$alphatestreference")) {
        out.alpha_ref = static_cast<float>(std::atof(std::string(*ref).c_str()));
    }
    out.additive = flag(*mat, "$additive");
    out.translucent = flag(*mat, "$translucent") || out.additive;
    out.self_illum = flag(*mat, "$selfillum");
    out.no_cull = flag(*mat, "$nocull");
    out.unlit = out.shader.starts_with("unlitgeneric") || out.shader == "sprite" || out.shader == "unlittwotexture";
    out.modulate = out.shader == "decalmodulate";
    if (auto variant = mat->param("$modelmaterial")) {
        out.model_material = std::string(*variant);
    }
    if (auto scale = mat->param("$decalscale")) {
        out.decal_scale = static_cast<float>(std::atof(std::string(*scale).c_str()));
    }
    // Environment map: a cube texture; "env_cubemap" (left unpatched by the
    // map compiler, e.g. on models) means the map's default cubemap.
    if (auto env = mat->param("$envmap"); env && !env->empty() && out.shader != "water" && out.shader != "waterwarp") {
        const std::string path = formats::normalize_game_path(*env) == "env_cubemap" ? default_envmap_
                                                                                     : formats::material_texture_path(*env);
        if (!path.empty()) {
            const auto* t = texture(path);
            if (t && t->desc().cube) {
                out.envmap = t;
                out.envmap_nearest = formats::normalize_game_path(*env) == "env_cubemap";
            }
        }
    }
    if (out.envmap) {
        out.envmap_mask = white_.get();
        if (flag(*mat, "$basealphaenvmapmask")) {
            out.envmap_mask_mode = RenderMaterial::EnvmapMask::BaseAlpha;
        } else if (flag(*mat, "$normalmapalphaenvmapmask") || flag(*mat, "$normalalphaenvmapmask")) {
            if (auto path = mat->texture_path("$bumpmap"); path && texture(*path, false)) {
                out.envmap_mask = texture(*path, false);
                out.envmap_mask_mode = RenderMaterial::EnvmapMask::MaskAlpha;
            }
        } else if (auto path = mat->texture_path("$envmapmask"); path && texture(*path, false)) {
            out.envmap_mask = texture(*path, false);
            out.envmap_mask_mode = RenderMaterial::EnvmapMask::Texture;
        }
    }
    out.water = out.shader == "water" || out.shader == "waterwarp";
    if (out.water) {
        out.translucent = true;
        out.no_cull = true;
        // A flat normal map is also a zero dudv offset (both 0.5, 0.5).
        out.normal_map = out.dudv = flat_normal_.get();
        if (auto path = mat->texture_path("$normalmap")) {
            if (const auto* t = texture(*path, false)) {
                out.normal_map = t;
            }
        }
        if (auto path = mat->texture_path("$bumpmap")) {
            if (const auto* t = texture(*path, false)) {
                out.dudv = t;
            }
        }
        auto number = [&](const char* key) {
            const auto v = mat->param(key);
            return v ? static_cast<float>(std::atof(std::string(*v).c_str())) : 0.0f;
        };
        out.reflect = mat->param("$reflecttexture").has_value();
        out.refract = mat->param("$refracttexture").has_value();
        out.reflect_amount = number("$reflectamount");
        out.refract_amount = number("$refractamount");
        if (auto c = mat->param("$fogcolor"); c && parse_vmt_color(*c)) {
            out.fog_color = *parse_vmt_color(*c);
        }
        if (flag(*mat, "$fogenable")) {
            out.fog_start = number("$fogstart");
            out.fog_end = number("$fogend");
        }
        if (auto c = mat->param("$reflecttint"); c && parse_vmt_color(*c)) {
            out.reflect_tint = *parse_vmt_color(*c);
        }
        if (auto c = mat->param("$refracttint"); c && parse_vmt_color(*c)) {
            out.refract_tint = *parse_vmt_color(*c);
        }
    }
    // Tool materials are compiled into the map but never drawn.
    for (const char* tool : {"%compilenodraw", "%compiletrigger", "%compileclip", "%compileplayerclip",
                             "%compilenpcclip", "%compileinvisible", "%compilehint", "%compileskip",
                             "%compileareaportal", "%compileorigin", "%compilefog"}) {
        out.skip = out.skip || flag(*mat, tool);
    }
    out.skip = out.skip || name.starts_with("tools/");
    return out;
}

} // namespace openbl::render
