#include "openbl/render/material_system.hpp"

#include "openbl/formats/error.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vmt.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/render/texture_upload.hpp"

#include <cstdio>
#include <cstdlib>

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

MaterialSystem::MaterialSystem(rhi::Device& device, const AssetSource& assets)
    : device_(device), assets_(assets), checker_(create_checker_texture(device)) {}

MaterialSystem::~MaterialSystem() = default;

const rhi::Texture* MaterialSystem::texture(const std::string& path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) {
        return it->second.get();
    }
    std::unique_ptr<rhi::Texture> tex;
    try {
        if (auto tth = assets_.read(path + ".tth")) {
            const auto ttz = assets_.read(path + ".ttz");
            auto assembled =
                formats::assemble_vtf(*tth, ttz ? std::optional(std::span<const std::byte>(*ttz)) : std::nullopt);
            tex = upload_vtf(device_, formats::VtfTexture::parse(std::move(assembled.data)));
        } else if (auto vtf = assets_.read(path + ".vtf")) {
            tex = upload_vtf(device_, formats::VtfTexture::parse(std::move(*vtf)));
        }
    } catch (const formats::FormatError& e) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), e.what());
    }
    if (!tex) {
        ++missing_textures_;
    }
    return textures_.emplace(path, std::move(tex)).first->second.get();
}

const RenderMaterial& MaterialSystem::material(std::string_view name) {
    const std::string key = formats::normalize_game_path(name);
    auto it = materials_.find(key);
    if (it != materials_.end()) {
        return it->second;
    }
    return materials_.emplace(key, load(key)).first->second;
}

RenderMaterial MaterialSystem::load(const std::string& name) {
    RenderMaterial out;
    out.name = name;
    out.base = out.base2 = checker_.get();

    std::optional<formats::Material> mat;
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
    out.alpha_test = flag(*mat, "$alphatest");
    if (auto ref = mat->param("$alphatestreference")) {
        out.alpha_ref = static_cast<float>(std::atof(std::string(*ref).c_str()));
    }
    out.additive = flag(*mat, "$additive");
    out.translucent = flag(*mat, "$translucent") || out.additive;
    out.self_illum = flag(*mat, "$selfillum");
    out.no_cull = flag(*mat, "$nocull");
    out.unlit = out.shader.starts_with("unlitgeneric") || out.shader == "sprite";
    out.skip = out.shader == "water" || out.shader == "waterwarp";
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
