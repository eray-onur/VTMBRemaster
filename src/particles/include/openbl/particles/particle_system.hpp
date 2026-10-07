#pragma once

// Simulation of Bloodlines particle effects (particles/*.txt; see
// docs/formats/particles.md and docs/PARTICLES.md). The semantics are
// inferred from the definitions and checked against captures of the original.

#include "openbl/formats/particle.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::particles {

using Vec3 = std::array<float, 3>;

// Loads and caches particle definitions by name ("Smoke1", "particles/Smoke1.txt", ...).
class ParticleLibrary {
public:
    // Returns the file's text, or nullopt if it does not exist.
    using Reader = std::function<std::optional<std::string>(const std::string& path)>;
    explicit ParticleLibrary(Reader reader) : reader_(std::move(reader)) {}

    // Null if the definition is missing.
    const formats::ParticleBlock* find(std::string_view name);
    // Game path of a definition's name, e.g. "particles/smoke1.txt".
    static std::string path_of(std::string_view name);

private:
    Reader reader_;
    std::unordered_map<std::string, std::unique_ptr<formats::ParticleFile>> files_; // Null: missing.
};

// One particle ready to draw.
struct ParticleQuad {
    Vec3 position{};
    float width = 0, height = 0;       // World units.
    float rotation = 0;                // Degrees, roll on screen.
    std::array<float, 3> color{};      // Additive colour, 0-1 per channel (rgb x color strength).
    float mask = 0;                    // Darkening opacity, 0-1.
    bool align = false;                // movealign: stretch along `velocity` instead of facing the screen.
    Vec3 velocity{};
    const std::string* sprite = nullptr;
};

// One running effect (e.g. an env_particle).
class ParticleSystem {
public:
    ParticleSystem(ParticleLibrary& library, std::string_view definition, const Vec3& origin, std::uint32_t seed = 1);

    bool valid() const { return root_ != nullptr; }
    // Advances by `dt` seconds (internally in steps of at most 1/30 s).
    void advance(float dt);
    float time() const { return time_; }
    std::size_t particle_count() const { return particles_.size(); }

    // Visible particles (those with a sprite), in no particular order.
    void collect(std::vector<ParticleQuad>& out) const;

private:
    struct Emitter {
        const formats::ParticleBlock* spawn = nullptr;     // The spawn block.
        const formats::ParticleBlock* particle = nullptr;  // Its child definition.
        float accumulator = 0;                              // Fractional particles owed by `rate`.
        bool burst_done = false;
    };
    struct Particle {
        const formats::ParticleBlock* def = nullptr;
        std::uint64_t parent_id = 0; // 0: none (the root).
        std::uint64_t id = 0;
        float age = 0;        // Frames.
        float lifetime = 0;   // Frames; infinite for looping roots.
        float fps = 30;
        float pick = 0.5f;    // One random draw per particle for curve ranges.
        Vec3 anchor{};        // Spawn position (plus inherited parent motion).
        Vec3 offset{};        // Accumulated Cartesian motion.
        float radius = 0, theta = 0, phi = 0; // Spherical position around the anchor (degrees).
        Vec3 position{}, velocity{};
        Vec3 parent_last{};   // Parent position at the previous step.
        std::vector<Emitter> emitters;
        bool alive = true;
    };

    void spawn(const formats::ParticleBlock& def, const Particle* parent, const formats::ParticleBlock* spawn_block);
    float curve(const Particle& p, std::string_view key, float fallback) const;
    void step(float dt);

    ParticleLibrary& library_;
    const formats::ParticleBlock* root_ = nullptr;
    Vec3 origin_{};
    std::vector<Particle> particles_;
    std::unordered_map<std::uint64_t, std::size_t> index_; // id -> index, rebuilt each step.
    std::uint64_t next_id_ = 1;
    float time_ = 0;
    std::mt19937 rng_;
};

// Upper bound on live particles per system, against runaway definitions.
constexpr std::size_t kMaxParticlesPerSystem = 4096;

} // namespace openbl::particles
