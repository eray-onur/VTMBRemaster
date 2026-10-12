#pragma once

// Material proxies (the "Proxies" block of a .vmt): small per-frame programs
// that write material variables. See docs/formats/proxies.md.
//
// MaterialProxies holds a material's variables (numbers, vectors and texture
// transforms) and runs its proxies in file order for a given time, so one
// proxy's result can feed the next. Proxies that need engine state (player
// position, rain) read it from ProxyContext; those that drive engine features
// OpenBL does not have yet are listed in unsupported() and do nothing.

#include "openbl/formats/vmt.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::formats {

struct ProxyVar {
    std::array<float, 4> v{}; // Components; a number has one.
    std::uint8_t count = 1;
    bool is_matrix = false;
    // 2x3 texture transform, row-major: u' = m0 u + m1 v + m2, v' = m3 u + m4 v + m5.
    std::array<float, 6> m{1, 0, 0, 0, 1, 0};
};

struct ProxyContext {
    float time = 0;             // Seconds.
    float player_distance = 0;  // PlayerProximity: distance from the player to the entity.
    float player_speed = 0;     // PlayerSpeed.
    std::array<float, 3> player_position{}; // PlayerPosition.
    float wetness = 0;          // GlobalWetness, 0 (dry) to 1.
    // LampBeam and LampHalo: z of the normalized direction from the camera to
    // the entity (1: straight up at it, -1: straight down).
    float view_direction_z = 0;
};

// Parses a texture transform value: "center .5 .5 scale 1 1 rotate 0 translate 0 0"
// (any subset, in any order). Nullopt if the text is not a transform.
std::optional<std::array<float, 6>> parse_texture_transform(std::string_view text);
// The transform built the way materials describe it: about `center`, scale,
// then rotate (degrees), then translate.
std::array<float, 6> build_texture_transform(std::array<float, 2> center, std::array<float, 2> scale, float rotate,
                                             std::array<float, 2> translate);

class MaterialProxies {
public:
    MaterialProxies() = default;
    explicit MaterialProxies(const Material& material, std::uint32_t seed = 1);

    bool empty() const { return ops_.empty(); }
    // Proxy names (lowercase) that are recognised but not run.
    const std::vector<std::string>& unsupported() const { return unsupported_; }

    // AnimatedTexture needs the frame count of the texture it animates;
    // `texture_var` is e.g. "$basetexture". Frames default to 1.
    void set_frame_count(std::string_view texture_var, std::uint32_t frames);
    // Texture variables animated by AnimatedTexture, each with the variable
    // that receives its frame number (e.g. "$normalmap" and "$bumpframe").
    std::vector<std::pair<std::string, std::string>> animated_textures() const;

    void evaluate(const ProxyContext& context);

    // Lowercase name with '$'. Null if the material never mentions it.
    const ProxyVar* var(std::string_view name) const;
    const std::unordered_map<std::string, ProxyVar>& vars() const { return vars_; }

private:
    struct Input {
        bool is_var = false;
        float constant = 0;
        std::string var;
        int component = -1;
    };
    struct Output {
        std::string var;
        int component = -1; // -1: the whole variable.
    };
    struct Op {
        std::string kind;
        std::array<Input, 6> in{};
        Output out;
        std::string texture_var; // AnimatedTexture.
        bool flag = false;       // AnimatedTexture: no wrap.
    };

    Input input(const KvNode& proxy, std::string_view key, float fallback);
    ProxyVar& slot(const std::string& name);
    ProxyVar read(const Input& in) const;
    void write(const Output& out, const ProxyVar& value);
    void write_scalar(const Output& out, float value);

    std::unordered_map<std::string, ProxyVar> vars_;
    std::unordered_map<std::string, std::uint32_t> frame_counts_;
    std::vector<Op> ops_;
    std::vector<std::string> unsupported_;
    std::mt19937 rng_;
};

} // namespace openbl::formats
