#pragma once

// Sprites placed in a map (env_sprite): glows, halos and light cones. Each is
// one textured quad sized from its texture and `scale`, tinted by its render
// colour and amount, and blended by its render mode (docs/RENDERING.md).

#include "openbl/formats/bsp.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openbl::render {

struct SpriteStats {
    std::size_t sprites = 0;           // Drawn (switched on at map start).
    std::size_t off = 0;               // Switched off until a script turns them on.
    std::size_t missing_materials = 0;
    std::size_t hidden_glows = 0;      // Glows inside a prop (a lamp globe), which the original hides.
};

class SpriteRenderer {
public:
    // `shown`: targetnames of sprites to draw although they start off.
    SpriteRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp,
                   const std::vector<std::string>& shown = {});
    ~SpriteRenderer();

    // `view`: column-major view matrix (its rows are the camera axes); `eye`:
    // camera position. Glow sprites behind world geometry (seen from `eye`)
    // are not drawn. `skybox`: draw only sprites in the 3D skybox's area, or only the rest.
    void draw(rhi::CommandList& cmd, const std::array<float, 16>& view_projection, const std::array<float, 16>& view,
              const std::array<float, 3>& eye, float time, const Fog& fog = {}, bool skybox = false);

    const SpriteStats& stats() const { return stats_; }

    // The original shows a glow only where its centre is visible, props
    // included; glows placed inside a closed prop (e.g. a lamp globe) never
    // show. `encloses` tells whether a point is inside one.
    void hide_enclosed_glows(const std::function<bool(const std::array<float, 3>&)>& encloses);

private:
    struct Sprite {
        const RenderMaterial* material = nullptr;
        std::array<float, 3> origin{};
        std::array<float, 3> angles{};   // Pitch, yaw, roll (degrees), for oriented sprites.
        std::array<float, 3> color{};    // rendercolor, 0-1.
        float amount = 1.0f;             // renderamt / 255.
        int mode = 0;                    // rendermode.
        int fx = 0;                      // renderfx.
        float scale = 1.0f;
        float framerate = 10.0f;
        bool skybox = false;             // In the 3D skybox's area.
    };

    const rhi::BindGroup& bind_group(const rhi::Texture* texture);

    rhi::Device& device_;
    const formats::BspFile& bsp_;
    SpriteStats stats_;
    std::vector<Sprite> sprites_;
    std::unique_ptr<rhi::Pipeline> pipeline_;      // Depth-tested.
    std::unique_ptr<rhi::Pipeline> glow_pipeline_; // Glows: drawn over everything (occlusion is traced).
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::Buffer> corners_;
    std::unique_ptr<rhi::Buffer> indices_;
    std::unordered_map<const rhi::Texture*, std::unique_ptr<rhi::BindGroup>> bind_groups_;
};

} // namespace openbl::render
