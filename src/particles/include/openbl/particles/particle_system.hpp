#pragma once

// Simulation of Bloodlines particle effects (particles/*.txt; see
// docs/formats/particles.md and docs/PARTICLES.md). The semantics follow the
// game's own template (particles/000_template.txt) where it speaks, and are
// otherwise inferred and checked against captures of the original.

#include "openbl/formats/particle.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::particles {

using Vec3 = std::array<float, 3>;
using Curve = std::optional<formats::ParticleCurve>;

// Curves a particle definition can animate over its life.
enum class Key : std::uint8_t {
    Size, Width, Height, Rotation, Color, Red, Green, Blue, Mask, MoveAlign,
    XSpeed, YSpeed, ZSpeed, ElevationSpeed, RadiusSpeed, ThetaSpeed, PhiSpeed, ParentSpeed, Count
};
// Values of a spawn block (placement, modulation of its children, emission).
enum class SpawnKey : std::uint8_t {
    X, Y, Z, Elevation, Radius, Theta, Phi, Red, Green, Blue, Color, Mask, Size, Width, Height, Rotation, Timescale,
    Rate, Burst, Count
};

struct Definition;

struct SpawnDefinition {
    const Definition* particle = nullptr; // The child effect; null if missing.
    std::array<Curve, static_cast<std::size_t>(SpawnKey::Count)> curves;
};

// A particle definition with its curves parsed once, shared by all its particles.
struct Definition {
    const formats::ParticleBlock* block = nullptr;
    std::array<Curve, static_cast<std::size_t>(Key::Count)> curves;
    Curve fps, frames, min_frames, max_frames, loop;
    const std::string* sprite = nullptr; // Null or empty: invisible (an emitter).
    std::vector<SpawnDefinition> spawns;
    // "precipitation": spawned in a box around the viewer by an effect that follows it.
    bool precipitation = false;
    // "collide": the particle ends where it meets the world, starting its
    // `spawn` particle there and its `decal` particle lying flat on the surface.
    bool collides = false;
    const Definition* collide_spawn = nullptr;
    const Definition* collide_decal = nullptr;

    const Curve& curve(Key k) const { return curves[static_cast<std::size_t>(k)]; }
};

// Loads, parses and compiles particle definitions by name ("Smoke1",
// "particles/Smoke1.txt", ...), caching them.
class ParticleLibrary {
public:
    // Returns the file's text, or nullopt if it does not exist.
    using Reader = std::function<std::optional<std::string>(const std::string& path)>;
    explicit ParticleLibrary(Reader reader) : reader_(std::move(reader)) {}

    // Null if the definition is missing.
    const Definition* find(std::string_view name);
    // Game path of a definition's name, e.g. "particles/smoke1.txt".
    static std::string path_of(std::string_view name);

private:
    struct Entry {
        std::unique_ptr<formats::ParticleFile> file; // Null: missing.
        std::unique_ptr<Definition> definition;
    };
    Reader reader_;
    std::unordered_map<std::string, Entry> entries_;
};

// One particle ready to draw.
struct ParticleQuad {
    Vec3 position{};
    float width = 0, height = 0;       // World units.
    float rotation = 0;                // Degrees, roll on screen.
    std::array<float, 3> color{};      // Light added, 0-1 per channel (rgb x color strength).
    float mask = 0;                    // Darkening opacity, 0-1.
    bool align = false;                // movealign: stretch along `velocity` instead of facing the screen.
    Vec3 velocity{};
    Vec3 normal{};                     // Non-zero: lies flat on a surface facing this way (collision decals).
    const std::string* sprite = nullptr; // Stable per definition, usable as a cache key.
};

// Where a particle meets the world.
struct ParticleHit {
    Vec3 point{};
    Vec3 normal{}; // Unit, facing the side the particle came from.
};

// The world particles collide with; either function may be empty.
struct ParticleWorld {
    // The first surface the segment from `from` to `to` crosses.
    std::function<std::optional<ParticleHit>(const Vec3& from, const Vec3& to)> trace;
    // True if open sky lies straight above `point`.
    std::function<bool(const Vec3& point)> open_sky;
};

// How an entity places and scales its effect.
struct SystemPlacement {
    // env_particle "ramp_scale": scales every emission rate (0: nothing is
    // emitted until a script raises it, as for Santa Monica's rain).
    float rate_scale = 1.0f;
    // func_particle: the root's children start at random points in this world box.
    std::optional<std::array<Vec3, 2>> volume;
    // env_particle attached to the player ("attach_type" 11) with "bounds":
    // the root follows the viewer, and its precipitation children start at
    // random points under open sky up to this far from it horizontally and
    // above it. 0: off.
    float precipitation_bounds = 0.0f;
};

// One running effect (e.g. an env_particle).
class ParticleSystem {
public:
    // `angles`: the system's pitch, yaw and roll (Source order, degrees); its
    // x/y/z axes rotate with it, while "elevation" is always world up.
    ParticleSystem(ParticleLibrary& library, std::string_view definition, const Vec3& origin, std::uint32_t seed = 1,
                   const Vec3& angles = {});

    bool valid() const { return root_ != nullptr; }
    // Optional; set before the first advance(). `world` must outlive the system.
    void set_placement(const SystemPlacement& placement, const ParticleWorld* world);
    // The viewer, for effects that follow it.
    void set_viewer(const Vec3& eye) { viewer_ = eye; }
    // Advances by `dt` seconds (internally in steps of at most 1/30 s).
    void advance(float dt);
    float time() const { return time_; }
    std::size_t particle_count() const { return particles_.size(); }

    // Visible particles (those with a sprite), in no particular order.
    void collect(std::vector<ParticleQuad>& out) const;

private:
    struct Emitter {
        float accumulator = 0; // Fractional particles owed by `rate`.
        bool burst_done = false;
    };
    struct Particle {
        const Definition* def = nullptr;
        std::uint64_t parent_id = 0; // 0: none (the root).
        std::uint64_t id = 0;
        float age = 0;        // Frames.
        float lifetime = 0;   // Frames; infinite for looping particles.
        float key_frames = 30; // `frames`: the span keyframes are spread over (and repeat over when looping).
        bool loop = false;
        float fps = 30;
        // Set by the spawn block that emitted this particle.
        std::array<float, 3> rgb_scale{1, 1, 1};
        float color_scale = 1, mask_scale = 1, size_scale = 1, width_scale = 1, height_scale = 1, rotation_add = 0;
        float pick = 0.5f;    // One random draw per particle for curve ranges.
        Vec3 anchor{};        // Spawn position (plus inherited parent motion).
        Vec3 offset{};        // Accumulated Cartesian motion.
        float radius = 0, theta = 0, phi = 0; // Spherical position around the anchor (degrees).
        Vec3 position{}, velocity{};
        Vec3 parent_last{};   // Parent position at the previous step.
        std::vector<Emitter> emitters; // One per Definition::spawns entry.
        float floor_z = -std::numeric_limits<float>::infinity(); // Precipitation ends here.
        Vec3 flat_normal{}; // Collision decals lie flat on the surface.
        bool alive = true;
    };

    // Null if not spawned (the cap, or precipitation with no open sky).
    Particle* spawn(const Definition& def, const Particle* parent, const SpawnDefinition* spawn);
    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng_); }
    float value(const Particle& p, Key key, float fallback) const;
    // A spawn block's value at its parent's current age.
    float spawn_value(const Particle& parent, const SpawnDefinition& spawn, SpawnKey key, float fallback) const;
    float initial(const SpawnDefinition& spawn, SpawnKey key, float fallback);
    Vec3 to_world(const Vec3& system) const; // System axes to world.
    void step(float dt);

    ParticleLibrary& library_;
    const Definition* root_ = nullptr;
    Vec3 origin_{};
    std::array<Vec3, 3> axes_{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; // System x, y, z in world space.
    std::vector<Particle> particles_;
    std::unordered_map<std::uint64_t, std::size_t> index_; // id -> index, rebuilt each step.
    std::uint64_t next_id_ = 1;
    float time_ = 0;
    std::mt19937 rng_;
    SystemPlacement placement_;
    const ParticleWorld* world_ = nullptr;
    Vec3 viewer_{};
};

// Upper bound on live particles per system, against runaway definitions.
constexpr std::size_t kMaxParticlesPerSystem = 4096;

} // namespace openbl::particles
