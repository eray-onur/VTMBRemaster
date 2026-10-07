#pragma once

// Bloodlines particle definitions (particles/*.txt). A file holds one root
// block (normally "Particle") of properties, child emitters ("spawn") and
// collision responses ("collide" { self, decal, spawn }). See
// docs/formats/particles.md.
//
// Numeric values are keyframe curves: comma-separated keys, each a number or
// a "min~max" random range, optionally followed by "(t)" (the key's time in
// frames) and "!" (hold: step rather than blend to the next key).

#include "openbl/formats/keyvalues.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

struct ParticleKey {
    float min = 0.0f, max = 0.0f;  // Equal unless the key is a "min~max" range.
    std::optional<float> time;     // "(t)": frame of this key, if given.
    bool hold = false;             // "!".
};

struct ParticleCurve {
    std::vector<ParticleKey> keys;

    // Value at `frame` of a particle living `lifetime` frames. Keys without
    // a time are spread evenly over the lifetime; `pick` in [0, 1] chooses
    // within ranges (one draw per particle, so ranges stay coherent).
    float at(float frame, float lifetime, float pick = 0.5f) const;
};

// Parses a curve; nullopt if `text` is not numeric (e.g. a sprite name).
std::optional<ParticleCurve> parse_particle_curve(std::string_view text);

struct ParticleBlock {
    std::string name; // "Particle", "spawn", "collide", "self", "decal", ...
    std::vector<std::pair<std::string, std::string>> values; // Leaf key/values as written.
    std::vector<ParticleBlock> children;

    // Case-insensitive lookups (the files mix "Radius_speed" and "radius_speed").
    std::string_view text(std::string_view key) const;
    std::optional<ParticleCurve> curve(std::string_view key) const;
    const ParticleBlock* child(std::string_view name) const;
};

struct ParticleFile {
    ParticleBlock root;              // The first root block.
    std::vector<ParticleBlock> extra; // Further root blocks (one file has six).
    std::vector<TextDiagnostic> diagnostics; // From KeyValues parsing and curve syntax.
};

ParticleFile parse_particle(std::string_view text);

} // namespace openbl::formats
