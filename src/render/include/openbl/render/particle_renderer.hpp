#pragma once

// Particle effects placed in a map (active env_particle entities), simulated
// with openbl::particles and drawn as camera-facing sprites, one effect at a
// time in the translucent queue. See docs/PARTICLES.md.

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/draw_order.hpp"
#include "openbl/formats/visibility.hpp"
#include "openbl/particles/particle_system.hpp"
#include "openbl/render/fog.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/translucent.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openbl::render {

struct ParticleStats {
    std::size_t systems = 0;          // Active env_particle effects found.
    std::size_t missing_definitions = 0;
    std::size_t missing_sprites = 0;
};

class ParticleRenderer {
public:
    // `shown`: targetnames of StartHidden or inactive emitters to run anyway.
    ParticleRenderer(rhi::Device& device, const AssetSource& assets, const formats::BspFile& bsp,
                     const std::vector<std::string>& shown = {});
    ~ParticleRenderer();

    // Advances every effect to `time` (seconds); going back in time restarts them.
    void update(float time);
    // One item per effect in the translucent queue (render/translucent.hpp),
    // placed by the leaves its particles reach; its particles are drawn back
    // to front. `skybox`: only effects in the 3D skybox's area, or only the
    // rest; `visibility`: skips effects this view cannot see (null: none).
    void collect_translucent(const formats::BspDrawOrder& order, const std::array<float, 3>& eye, bool skybox,
                             std::uint32_t source, std::vector<TranslucentItem>& out,
                             const formats::ViewVisibility* visibility = nullptr);
    // `right`/`up`: camera axes in world space; `eye`: camera position.
    void begin_translucent(const std::array<float, 16>& view_projection, const std::array<float, 3>& right,
                           const std::array<float, 3>& up, const std::array<float, 3>& eye, const Fog& fog = {});
    void draw_translucent_item(rhi::CommandList& cmd, std::uint32_t index);

    const ParticleStats& stats() const { return stats_; }

private:
    const rhi::Texture* sprite(const std::string& name);

    struct Emitter {
        std::string definition;
        particles::Vec3 origin{};
        particles::Vec3 angles{};
        std::uint32_t seed = 1;
        bool skybox = false; // In the 3D skybox's area.
        std::unique_ptr<particles::ParticleSystem> system;
        std::vector<particles::ParticleQuad> quads; // This view's particles (collect_translucent).
    };

    rhi::Device& device_;
    const AssetSource& assets_;
    const formats::BspFile& bsp_;
    particles::ParticleLibrary library_;
    ParticleStats stats_;
    std::vector<Emitter> emitters_;
    float time_ = 0;
    std::unique_ptr<rhi::Pipeline> pipeline_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::Buffer> corners_;
    std::unique_ptr<rhi::Buffer> indices_;
    std::unique_ptr<rhi::Texture> white_; // Stand-in for missing sprites.
    std::unordered_map<std::string, std::unique_ptr<rhi::Texture>> sprites_; // Lowercase name; null = missing.
    std::unordered_map<const std::string*, const rhi::Texture*> sprite_by_definition_; // Resolved once per definition.
    std::unordered_map<const rhi::Texture*, std::unique_ptr<rhi::BindGroup>> bind_groups_;
    std::vector<float> data_;
    // The translucent pass under way (begin_translucent).
    std::array<float, 16> pass_view_projection_{};
    std::array<float, 3> pass_right_{}, pass_up_{}, pass_eye_{};
    Fog pass_fog_;
};

} // namespace openbl::render
