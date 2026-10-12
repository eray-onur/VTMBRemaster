#include "openbl/formats/vmt_proxies.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <sstream>

namespace openbl::formats {
namespace {

constexpr float kPi = 3.14159265358979f;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<float> number(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.front() == '+') {
        s.remove_prefix(1);
    }
    float f = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), f);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return f;
}

// A material parameter's value as a variable: number, "[a b c]" (0-1),
// "{r g b}" (0-255) or a texture transform. Nullopt for anything else
// (texture names, strings).
std::optional<ProxyVar> parse_value(std::string_view text) {
    text = trim(text);
    ProxyVar var;
    if (auto f = number(text)) {
        var.v[0] = *f;
        return var;
    }
    if (text.size() >= 2 && (text.front() == '[' || text.front() == '{')) {
        const bool bytes = text.front() == '{';
        std::istringstream in(std::string(text.substr(1, text.size() - 2)));
        float f = 0;
        var.count = 0;
        while (var.count < 4 && in >> f) {
            var.v[var.count++] = bytes ? f / 255.0f : f;
        }
        if (var.count == 0) {
            return std::nullopt;
        }
        return var;
    }
    if (auto m = parse_texture_transform(text)) {
        var.is_matrix = true;
        var.m = *m;
        return var;
    }
    return std::nullopt;
}

// "$color[0]" -> ("$color", 0).
std::pair<std::string, int> split_component(std::string_view ref) {
    ref = trim(ref);
    int component = -1;
    if (const auto open = ref.find('['); open != std::string_view::npos && ref.back() == ']') {
        int c = 0;
        const auto inner = ref.substr(open + 1, ref.size() - open - 2);
        if (std::from_chars(inner.data(), inner.data() + inner.size(), c).ec == std::errc() && c >= 0 && c < 4) {
            component = c;
        }
        ref = ref.substr(0, open);
    }
    return {lower(ref), component};
}

const KvNode* find_key(const KvNode& proxy, std::string_view key) {
    for (const KvNode& c : proxy.children) {
        if (!c.is_block && iequals(c.key, key)) {
            return &c;
        }
    }
    return nullptr;
}

bool is_transform_name(std::string_view name) {
    return name.size() > 9 && name.substr(name.size() - 9) == "transform";
}

std::array<float, 6> multiply(const std::array<float, 6>& a, const std::array<float, 6>& b) {
    // (a * b) applies b first.
    return {a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
            a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5]};
}

} // namespace

std::array<float, 6> build_texture_transform(std::array<float, 2> center, std::array<float, 2> scale, float rotate,
                                             std::array<float, 2> translate) {
    const float r = rotate * kPi / 180.0f;
    const float c = std::cos(r), s = std::sin(r);
    std::array<float, 6> m{1, 0, -center[0], 0, 1, -center[1]};
    m = multiply({scale[0], 0, 0, 0, scale[1], 0}, m);
    m = multiply({c, -s, 0, s, c, 0}, m);
    m = multiply({1, 0, center[0] + translate[0], 0, 1, center[1] + translate[1]}, m);
    return m;
}

std::optional<std::array<float, 6>> parse_texture_transform(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::array<float, 2> center{0.5f, 0.5f}, scale{1, 1}, translate{0, 0};
    float rotate = 0;
    std::string word;
    bool any = false;
    while (in >> word) {
        word = lower(word);
        if (word == "center" && in >> center[0] >> center[1]) {
        } else if (word == "scale" && in >> scale[0] >> scale[1]) {
        } else if (word == "rotate" && in >> rotate) {
        } else if (word == "translate" && in >> translate[0] >> translate[1]) {
        } else {
            return std::nullopt;
        }
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return build_texture_transform(center, scale, rotate, translate);
}

MaterialProxies::MaterialProxies(const Material& material, std::uint32_t seed) : rng_(seed) {
    for (const auto& [name, value] : material.params) {
        if (auto v = parse_value(value)) {
            vars_[lower(name)] = *v;
        }
    }
    for (const KvNode& proxy : material.proxies) {
        if (!proxy.is_block) {
            continue;
        }
        Op op;
        op.kind = lower(proxy.key);
        auto out = [&](std::string_view key) {
            if (const KvNode* k = find_key(proxy, key)) {
                auto [var, component] = split_component(k->value);
                op.out = {var, component};
                slot(var);
            }
        };
        const std::string& k = op.kind;
        if (k == "sine") {
            op.in = {input(proxy, "sinemin", -1), input(proxy, "sinemax", 1), input(proxy, "sineperiod", 1),
                     input(proxy, "timeoffset", 0)};
            out("resultvar");
        } else if (k == "linearramp") {
            op.in = {input(proxy, "rate", 1), input(proxy, "initialvalue", 0)};
            out("resultvar");
        } else if (k == "uniformnoise") {
            op.in = {input(proxy, "minval", 0), input(proxy, "maxval", 1)};
            out("resultvar");
        } else if (k == "gaussiannoise") {
            op.in = {input(proxy, "mean", 0), input(proxy, "halfwidth", 1), input(proxy, "minval", -FLT_MAX),
                     input(proxy, "maxval", FLT_MAX)};
            out("resultvar");
        } else if (k == "add" || k == "subtract" || k == "multiply") {
            op.in = {input(proxy, "srcvar1", 0), input(proxy, "srcvar2", 0)};
            out("resultvar");
        } else if (k == "abs") {
            op.in = {input(proxy, "srcvar1", 0)};
            out("resultvar");
        } else if (k == "exponential") {
            op.in = {input(proxy, "srcvar1", 0), input(proxy, "scale", 1), input(proxy, "offset", 0),
                     input(proxy, "minval", -FLT_MAX), input(proxy, "maxval", FLT_MAX)};
            out("resultvar");
        } else if (k == "lessorequal") {
            op.in = {input(proxy, "srcvar1", 0), input(proxy, "srcvar2", 0), input(proxy, "lessequalvar", 0),
                     input(proxy, "greatervar", 0)};
            out("resultvar");
        } else if (k == "animatedtexture") {
            if (const KvNode* t = find_key(proxy, "animatedtexturevar")) {
                op.texture_var = lower(trim(t->value));
            }
            op.in = {input(proxy, "animatedtextureframerate", 15)};
            if (const KvNode* nw = find_key(proxy, "animationnowrap")) {
                op.flag = number(nw->value).value_or(0) != 0;
            }
            out("animatedtextureframenumvar");
        } else if (k == "texturescroll") {
            op.in = {input(proxy, "texturescrollrate", 1), input(proxy, "texturescrollangle", 0),
                     input(proxy, "texturescale", 1)};
            out("texturescrollvar");
        } else if (k == "texturetransform") {
            // Variable names; missing inputs keep their identity values.
            auto var_input = [&](std::string_view key) {
                Input in;
                if (const KvNode* n = find_key(proxy, key)) {
                    auto [var, component] = split_component(n->value);
                    in = {true, 0, var, component};
                    slot(var);
                }
                return in;
            };
            op.in = {var_input("centervar"), var_input("scalevar"), var_input("rotatevar"), var_input("translatevar")};
            out("resultvar");
        } else if (k == "playerproximity" || k == "playerspeed" || k == "playerposition" || k == "globalwetness") {
            op.in = {input(proxy, "scale", 1)};
            out("resultvar");
        } else if (k == "lampbeam" || k == "lamphalo") {
            // No keys: both write $alpha (read from the client DLL's code).
            op.out = {"$alpha", -1};
            slot("$alpha");
        } else {
            unsupported_.push_back(k);
            continue;
        }
        if (op.out.var.empty()) {
            continue; // Nothing to write.
        }
        ops_.push_back(std::move(op));
    }
}

MaterialProxies::Input MaterialProxies::input(const KvNode& proxy, std::string_view key, float fallback) {
    Input in;
    in.constant = fallback;
    if (const KvNode* k = find_key(proxy, key)) {
        if (auto f = number(k->value)) {
            in.constant = *f;
        } else if (!trim(k->value).empty() && trim(k->value).front() == '$') {
            auto [var, component] = split_component(k->value);
            in = {true, 0, var, component};
            slot(var);
        }
    }
    return in;
}

ProxyVar& MaterialProxies::slot(const std::string& name) {
    auto [it, inserted] = vars_.try_emplace(name);
    if (inserted && is_transform_name(name)) {
        it->second.is_matrix = true;
    }
    return it->second;
}

ProxyVar MaterialProxies::read(const Input& in) const {
    ProxyVar out;
    if (!in.is_var) {
        out.v[0] = in.constant;
        return out;
    }
    const auto it = vars_.find(in.var);
    if (it == vars_.end()) {
        return out;
    }
    if (in.component >= 0) {
        out.v[0] = it->second.v[static_cast<std::size_t>(in.component)];
        return out;
    }
    return it->second;
}

void MaterialProxies::write(const Output& out, const ProxyVar& value) {
    ProxyVar& dst = slot(out.var);
    if (out.component >= 0) {
        dst.v[static_cast<std::size_t>(out.component)] = value.v[0];
        dst.count = std::max<std::uint8_t>(dst.count, static_cast<std::uint8_t>(out.component + 1));
    } else if (value.is_matrix) {
        dst.is_matrix = true;
        dst.m = value.m;
    } else if (value.count == 1 && dst.count > 1) {
        // A number written to a vector sets every component.
        std::fill(dst.v.begin(), dst.v.begin() + dst.count, value.v[0]);
    } else {
        dst.v = value.v;
        dst.count = value.count;
    }
}

void MaterialProxies::write_scalar(const Output& out, float value) {
    ProxyVar v;
    v.v[0] = value;
    write(out, v);
}

void MaterialProxies::set_frame_count(std::string_view texture_var, std::uint32_t frames) {
    frame_counts_[lower(texture_var)] = std::max<std::uint32_t>(frames, 1);
}

std::vector<std::pair<std::string, std::string>> MaterialProxies::animated_textures() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Op& op : ops_) {
        if (op.kind == "animatedtexture" && !op.texture_var.empty()) {
            out.emplace_back(op.texture_var, op.out.var);
        }
    }
    return out;
}

const ProxyVar* MaterialProxies::var(std::string_view name) const {
    const auto it = vars_.find(lower(name));
    return it == vars_.end() ? nullptr : &it->second;
}

void MaterialProxies::evaluate(const ProxyContext& ctx) {
    auto f = [&](const Op& op, std::size_t i) { return read(op.in[i]).v[0]; };
    for (const Op& op : ops_) {
        const std::string& k = op.kind;
        if (k == "sine") {
            const float lo = f(op, 0), hi = f(op, 1), period = f(op, 2), offset = f(op, 3);
            const float s = period != 0 ? std::sin(2 * kPi * (ctx.time - offset) / period) : 0.0f;
            write_scalar(op.out, (hi - lo) * (s * 0.5f + 0.5f) + lo);
        } else if (k == "linearramp") {
            write_scalar(op.out, f(op, 1) + f(op, 0) * ctx.time);
        } else if (k == "uniformnoise") {
            std::uniform_real_distribution<float> d(0.0f, 1.0f);
            write_scalar(op.out, f(op, 0) + (f(op, 1) - f(op, 0)) * d(rng_));
        } else if (k == "gaussiannoise") {
            std::normal_distribution<float> d(0.0f, 1.0f);
            write_scalar(op.out, std::clamp(f(op, 0) + f(op, 1) * d(rng_), f(op, 2), f(op, 3)));
        } else if (k == "add" || k == "subtract" || k == "multiply") {
            const ProxyVar a = read(op.in[0]), b = read(op.in[1]);
            ProxyVar r;
            r.count = std::max(a.count, b.count);
            for (std::size_t c = 0; c < r.count; ++c) {
                const float x = a.v[a.count > 1 ? c : 0], y = b.v[b.count > 1 ? c : 0];
                r.v[c] = k == "add" ? x + y : k == "subtract" ? x - y : x * y;
            }
            write(op.out, r);
        } else if (k == "abs") {
            ProxyVar r = read(op.in[0]);
            for (float& c : r.v) {
                c = std::abs(c);
            }
            write(op.out, r);
        } else if (k == "exponential") {
            write_scalar(op.out, std::clamp(f(op, 1) * std::exp(f(op, 0) + f(op, 2)), f(op, 3), f(op, 4)));
        } else if (k == "lessorequal") {
            write(op.out, read(op.in[f(op, 0) <= f(op, 1) ? 2 : 3]));
        } else if (k == "animatedtexture") {
            const auto it = frame_counts_.find(op.texture_var);
            const std::uint32_t frames = it == frame_counts_.end() ? 1 : it->second;
            auto frame = static_cast<std::int64_t>(std::floor(ctx.time * f(op, 0)));
            frame = op.flag ? std::clamp<std::int64_t>(frame, 0, frames - 1) : ((frame % frames) + frames) % frames;
            write_scalar(op.out, static_cast<float>(frame));
        } else if (k == "texturescroll") {
            // Offsets wrap at whole texture widths to keep precision over time.
            const float rate = f(op, 0), angle = f(op, 1) * kPi / 180.0f, scale = f(op, 2);
            const float s = std::fmod(std::cos(angle) * rate * ctx.time, 1.0f);
            const float t = std::fmod(std::sin(angle) * rate * ctx.time, 1.0f);
            ProxyVar m;
            m.is_matrix = true;
            m.m = {scale, 0, s, 0, scale, t};
            write(op.out, m);
        } else if (k == "texturetransform") {
            auto vec2 = [&](std::size_t i, std::array<float, 2> fallback) {
                if (!op.in[i].is_var) {
                    return fallback;
                }
                const ProxyVar v = read(op.in[i]);
                return v.count >= 2 ? std::array<float, 2>{v.v[0], v.v[1]} : std::array<float, 2>{v.v[0], v.v[0]};
            };
            ProxyVar m;
            m.is_matrix = true;
            m.m = build_texture_transform(vec2(0, {0.5f, 0.5f}), vec2(1, {1, 1}), op.in[2].is_var ? f(op, 2) : 0.0f,
                                          vec2(3, {0, 0}));
            write(op.out, m);
        } else if (k == "playerproximity") {
            write_scalar(op.out, f(op, 0) * ctx.player_distance);
        } else if (k == "lampbeam") {
            // Fades as the view turns vertical: an upright beam seen from below.
            write_scalar(op.out, 1.0f - std::abs(ctx.view_direction_z));
        } else if (k == "lamphalo") {
            // Shows as the view turns vertical: a halo seen looking up at the lamp.
            write_scalar(op.out, std::clamp((std::abs(ctx.view_direction_z) - 0.25f) * 1.35f, 0.0f, 1.0f));
        } else if (k == "playerspeed") {
            write_scalar(op.out, f(op, 0) * ctx.player_speed);
        } else if (k == "globalwetness") {
            write_scalar(op.out, f(op, 0) * ctx.wetness);
        } else if (k == "playerposition") {
            ProxyVar p;
            p.count = 3;
            for (std::size_t c = 0; c < 3; ++c) {
                p.v[c] = f(op, 0) * ctx.player_position[c];
            }
            write(op.out, p);
        }
    }
}

} // namespace openbl::formats
