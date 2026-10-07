#include "openbl/formats/particle.hpp"

#include <charconv>
#include <cstdlib>

namespace openbl::formats {
namespace {

bool parse_number(std::string_view& s, float& out) {
    // std::from_chars rejects a leading '+' and ".5"-style values are fine.
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto r = std::from_chars(begin, end, out);
    if (r.ec != std::errc()) {
        return false;
    }
    s.remove_prefix(static_cast<std::size_t>(r.ptr - begin));
    return true;
}

void trim(std::string_view& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
}

ParticleBlock convert(const KvNode& node, std::vector<TextDiagnostic>& diagnostics) {
    ParticleBlock b;
    b.name = node.key;
    for (const KvNode& c : node.children) {
        if (c.is_block) {
            b.children.push_back(convert(c, diagnostics));
            continue;
        }
        b.values.emplace_back(c.key, c.value);
        // Names (sprites, particles, decals) are text; anything that starts
        // like a number must parse as a curve.
        const char f = c.value.empty() ? 'x' : c.value.front();
        if ((f == '-' || f == '.' || (f >= '0' && f <= '9')) && !parse_particle_curve(c.value)) {
            diagnostics.push_back({c.line, "malformed value for \"" + c.key + "\": " + c.value});
        }
    }
    return b;
}

} // namespace

std::optional<ParticleCurve> parse_particle_curve(std::string_view text) {
    ParticleCurve curve;
    trim(text);
    if (!text.empty() && text.back() == ',') {
        text.remove_suffix(1); // A trailing comma occurs in the shipped files.
    }
    while (true) {
        trim(text);
        ParticleKey key;
        if (!parse_number(text, key.min)) {
            return std::nullopt;
        }
        key.max = key.min;
        if (!text.empty() && text.front() == '~') {
            text.remove_prefix(1);
            if (!parse_number(text, key.max)) {
                return std::nullopt;
            }
        }
        if (!text.empty() && text.front() == '(') {
            text.remove_prefix(1);
            float t = 0.0f;
            if (!parse_number(text, t) || text.empty() || text.front() != ')') {
                return std::nullopt;
            }
            text.remove_prefix(1);
            key.time = t;
        }
        if (!text.empty() && text.front() == '!') {
            text.remove_prefix(1);
            key.hold = true;
        }
        curve.keys.push_back(key);
        trim(text);
        if (text.empty()) {
            return curve;
        }
        if (text.front() != ',') {
            return std::nullopt;
        }
        text.remove_prefix(1);
    }
}

float ParticleCurve::at(float frame, float lifetime, float pick) const {
    if (keys.empty()) {
        return 0.0f;
    }
    auto value = [&](const ParticleKey& k) { return k.min + (k.max - k.min) * pick; };
    if (keys.size() == 1) {
        return value(keys.front());
    }
    // Key times: explicit, or spread evenly over the lifetime.
    const float step = keys.size() > 1 ? lifetime / static_cast<float>(keys.size() - 1) : 0.0f;
    auto time = [&](std::size_t i) { return keys[i].time.value_or(step * static_cast<float>(i)); };
    if (frame <= time(0)) {
        return value(keys.front());
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const float t0 = time(i), t1 = time(i + 1);
        if (frame < t1) {
            if (keys[i].hold || t1 <= t0) {
                return value(keys[i]);
            }
            const float u = (frame - t0) / (t1 - t0);
            return value(keys[i]) + (value(keys[i + 1]) - value(keys[i])) * u;
        }
    }
    return value(keys.back());
}

std::string_view ParticleBlock::text(std::string_view key) const {
    for (const auto& [k, v] : values) {
        if (iequals(k, key)) {
            return v;
        }
    }
    return {};
}

std::optional<ParticleCurve> ParticleBlock::curve(std::string_view key) const {
    const auto t = text(key);
    return t.empty() ? std::nullopt : parse_particle_curve(t);
}

const ParticleBlock* ParticleBlock::child(std::string_view block) const {
    for (const auto& c : children) {
        if (iequals(c.name, block)) {
            return &c;
        }
    }
    return nullptr;
}

ParticleFile parse_particle(std::string_view text) {
    const KvDocument doc = parse_keyvalues(text);
    ParticleFile file;
    file.diagnostics = doc.diagnostics;
    bool first = true;
    for (const KvNode& n : doc.roots) {
        if (!n.is_block) {
            continue;
        }
        (first ? file.root : file.extra.emplace_back()) = convert(n, file.diagnostics);
        first = false;
    }
    return file;
}

} // namespace openbl::formats
