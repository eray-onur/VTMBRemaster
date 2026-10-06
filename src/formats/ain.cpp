#include "openbl/formats/ain.hpp"

#include "openbl/formats/error.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace openbl::formats {
namespace {

// Whitespace-separated tokens. The writer inserts a "Nodes:" label every 32
// nodes and wraps lines mid-record, so line structure carries no meaning.
class Tokens {
public:
    explicit Tokens(std::string_view text) : text_(text) {}

    std::string_view next() {
        for (;;) {
            while (pos_ < text_.size() && is_space(text_[pos_])) {
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                throw FormatError("AIN: unexpected end of file");
            }
            const std::size_t start = pos_;
            while (pos_ < text_.size() && !is_space(text_[pos_])) {
                ++pos_;
            }
            const std::string_view tok = text_.substr(start, pos_ - start);
            if (tok != "Nodes:") {
                return tok;
            }
        }
    }

    bool at_end() {
        while (pos_ < text_.size() && is_space(text_[pos_])) {
            ++pos_;
        }
        return pos_ >= text_.size();
    }

    void expect(std::string_view label) {
        const std::string_view tok = next();
        if (tok != label) {
            throw FormatError("AIN: expected '" + std::string(label) + "', got '" + std::string(tok) + "'");
        }
    }

    template <typename T>
    T number() {
        const std::string_view tok = next();
        T v{};
        const auto [end, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), v);
        if (ec != std::errc() || end != tok.data() + tok.size()) {
            throw FormatError("AIN: expected a number, got '" + std::string(tok) + "'");
        }
        return v;
    }

    std::int32_t count(std::int32_t limit) {
        const auto v = number<std::int32_t>();
        if (v < 0 || v > limit) {
            throw FormatError("AIN: count out of range: " + std::to_string(v));
        }
        return v;
    }

private:
    static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    std::string_view text_;
    std::size_t pos_ = 0;
};

constexpr std::int32_t kMaxHulls = 64;
constexpr std::int32_t kMaxNodes = 1 << 16;
constexpr std::int32_t kMaxLinks = 1 << 20;

} // namespace

AinGraph parse_ain(std::string_view text) {
    Tokens in(text);
    AinGraph g;
    in.expect("Version");
    g.version = in.number<std::int32_t>();
    if (g.version != 30) {
        throw FormatError("AIN: unsupported version " + std::to_string(g.version));
    }
    in.expect("NumHulls:");
    const std::int32_t hulls = in.count(kMaxHulls);
    in.expect("UsedHullBits:");
    g.used_hull_bits = in.number<std::int32_t>();
    in.expect("ZoneCount:");
    g.zones.resize(static_cast<std::size_t>(in.count(kMaxNodes)));
    for (auto& z : g.zones) {
        z = in.number<std::int32_t>();
    }
    in.expect("NumNodes:");
    const std::int32_t node_count = in.count(kMaxNodes);
    const std::size_t mask_words = (static_cast<std::size_t>(node_count) + 31) / 32;
    g.nodes.resize(static_cast<std::size_t>(node_count));
    for (auto& n : g.nodes) {
        // "x,y,z" is a single token.
        const std::string_view origin = in.next();
        const char* p = origin.data();
        const char* end = origin.data() + origin.size();
        for (int k = 0; k < 3; ++k) {
            const auto r = std::from_chars(p, end, n.origin[k]);
            if (r.ec != std::errc() || (k < 2 && (r.ptr == end || *r.ptr != ',')) || (k == 2 && r.ptr != end)) {
                throw FormatError("AIN: malformed node origin '" + std::string(origin) + "'");
            }
            p = r.ptr + (k < 2);
        }
        n.yaw = in.number<float>();
        n.hull_offsets.resize(static_cast<std::size_t>(hulls));
        for (auto& h : n.hull_offsets) {
            h = in.number<float>();
        }
        n.type = in.number<std::int32_t>();
        n.info = in.number<std::int32_t>();
        n.mask.resize(mask_words);
        for (auto& w : n.mask) {
            w = static_cast<std::uint32_t>(in.number<std::int32_t>());
        }
        n.zone = in.number<std::int32_t>();
        n.link_count = in.number<std::int32_t>();
        if (n.zone < 0 || static_cast<std::size_t>(n.zone) >= std::max<std::size_t>(g.zones.size(), 1)) {
            throw FormatError("AIN: node zone out of range");
        }
    }
    in.expect("TotalNumLinks");
    g.links.resize(static_cast<std::size_t>(in.count(kMaxLinks)));
    for (auto& l : g.links) {
        l.source = in.number<std::int32_t>();
        l.dest = in.number<std::int32_t>();
        if (l.source < 0 || l.source >= node_count || l.dest < 0 || l.dest >= node_count) {
            throw FormatError("AIN: link node out of range");
        }
        l.flags = in.number<std::int32_t>();
        l.hull_moves.resize(static_cast<std::size_t>(hulls));
        for (auto& m : l.hull_moves) {
            m = in.number<std::int32_t>();
        }
    }
    in.expect("WCLookup:");
    g.editor_ids.resize(static_cast<std::size_t>(node_count));
    for (auto& id : g.editor_ids) {
        id = in.number<std::int32_t>();
    }
    if (!in.at_end()) {
        throw FormatError("AIN: trailing data");
    }
    return g;
}

std::int64_t parse_loc(std::string_view text) {
    Tokens in(text);
    const auto v = in.number<std::int64_t>();
    if (!in.at_end()) {
        throw FormatError("LOC: trailing data");
    }
    return v;
}

} // namespace openbl::formats
