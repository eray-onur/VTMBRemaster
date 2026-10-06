#include "openbl/render/light_styles.hpp"

#include <cmath>
#include <cstdlib>
#include <string_view>

namespace openbl::render {

namespace {

// Brightness patterns for styles 1..12, one letter per tenth of a second.
constexpr std::string_view kPatterns[] = {
    "m",                                                   // 0  normal
    "mmnmmommommnonmmonqnmmo",                             // 1  flicker A
    "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba", // 2  slow strong pulse
    "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",                  // 3  candle A
    "mamamamamama",                                        // 4  fast strobe
    "jklmnopqrstuvwxyzyxwvutsrqponmlkj",                   // 5  gentle pulse
    "nmonqnmomnmomomno",                                   // 6  flicker B
    "mmmaaaabcdefgmmmmaaaammmaamm",                        // 7  candle B
    "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",          // 8  candle C
    "aaaaaaaazzzzzzzz",                                    // 9  slow strobe
    "mmamammmmammamamaaamammma",                           // 10 fluorescent flicker
    "abcdefghijklmnopqrrqponmlkjihgfedcba",                // 11 slow pulse, no black
    "mmnnmmnnnmmnn",                                       // 12 underwater mutation
};

float letter_value(char c) {
    return static_cast<float>(c - 'a') / 12.0f;
}

} // namespace

LightStyles::LightStyles(const formats::BspFile& bsp) {
    on_.fill(true);
    // A switchable style is off by default only if all of its lights start dark.
    std::array<bool, kCount> seen{};
    std::array<bool, kCount> any_on{};
    for (const auto& e : bsp.entities) {
        auto cls = e.find("classname");
        auto style = e.find("style");
        if (cls == e.end() || style == e.end() || !cls->second.starts_with("light")) {
            continue;
        }
        const long s = std::strtol(style->second.c_str(), nullptr, 10);
        if (s < 32 || s >= static_cast<long>(kCount)) {
            continue;
        }
        long flags = 0;
        if (auto f = e.find("spawnflags"); f != e.end()) {
            flags = std::strtol(f->second.c_str(), nullptr, 10);
        }
        seen[static_cast<std::size_t>(s)] = true;
        any_on[static_cast<std::size_t>(s)] = any_on[static_cast<std::size_t>(s)] || (flags & 1) == 0;
    }
    for (std::size_t s = 32; s < kCount; ++s) {
        if (seen[s]) {
            on_[s] = any_on[s];
        }
    }
}

std::array<float, LightStyles::kCount> LightStyles::values(float time) const {
    std::array<float, kCount> out{};
    const auto tick = static_cast<std::size_t>(std::max(0.0f, time) * 10.0f);
    for (std::size_t s = 0; s < kCount; ++s) {
        if (s < std::size(kPatterns)) {
            const std::string_view p = kPatterns[s];
            out[s] = letter_value(p[tick % p.size()]);
        } else {
            out[s] = on_[s] ? 1.0f : 0.0f;
        }
    }
    return out;
}

float LightStyles::default_value(std::size_t style) const {
    return style < kCount && on_[style] ? 1.0f : 0.0f;
}

} // namespace openbl::render
