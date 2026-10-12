#pragma once

// Sprites placed in a map (env_sprite): glows, halos and light cones. Each is
// one textured quad sized from its texture and `scale`, tinted by its render
// colour and amount, and blended by its render mode (docs/RENDERING.md).

#include "openbl/formats/bsp.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openbl::render {

struct SpriteStats {
    std::size_t sprites = 0;           // Drawn (switched on at map start).
    std::size_t off = 0;               // Switched off until a script turns them on.
    std::size_t missing_materials = 0;
};

class SpriteRenderer {
public:
    // `shown`: targetnames of sprites to draw although they start off.
    SpriteRenderer(rhi::Device& device, MaterialSystem& materials, const formats::BspFile& bsp,
                   const std::vector<std::string>& shown = {});
    ~SpriteRenderer();

    // `view`: column-major view matrix (its rows are the camera axes); `eye`:
    // camera position. Glow sprites behind world geometry (seen from `eye`)
    // are not drawn. `skybox`: draw only sprites in the 3D skybox's area, or
    // only the rest; `world_scale`: world units per unit of distance (the
    // skybox's scale), for distance-driven material proxies.
    void draw(rhi::CommandList& cmd, const std::array<float, 16>& view_projection, const std::array<float, 16>& view,
              const std::array<float, 3>& eye, float time, const Fog& fog = {}, bool skybox = false,
              float world_scale = 1.0f);

    const SpriteStats& stats() const { return stats_; }

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
        float phase = 0.0f;              // Pulse offset (entity index x 363, as in Source).
        bool skybox = false;             // In the 3D skybox's area.
    };

    const rhi::BindGroup& bind_group(const rhi::Texture* texture);

    rhi::Device& device_;
    const formats::BspFile& bsp_;
    MaterialSystem& materials_;
    SpriteStats stats_;
    std::vector<Sprite> sprites_;
    std::unique_ptr<rhi::Pipeline> pipeline_; // Depth-tested, glows included.
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::Buffer> corners_;
    std::unique_ptr<rhi::Buffer> indices_;
    std::unordered_map<const rhi::Texture*, std::unique_ptr<rhi::BindGroup>> bind_groups_;
};

} // namespace openbl::render
