#include "openbl/formats/expressions.hpp"

#include "binary_reader.hpp"

#include <charconv>
#include <cstdint>
#include <optional>

namespace openbl::formats {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Quoted strings (no escapes) and bare words on one line.
std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && is_space(line[i])) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        if (line[i] == '"') {
            const std::size_t end = line.find('"', i + 1);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end;
            out.push_back(line.substr(i + 1, stop - i - 1));
            i = stop + 1;
        } else {
            const std::size_t start = i;
            while (i < line.size() && !is_space(line[i])) {
                ++i;
            }
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

std::optional<float> to_float(std::string_view s) {
    float v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

// "_", a single character, or a code point written as "0x0279".
std::optional<char32_t> to_code(std::string_view s) {
    if (s.size() == 1) {
        return static_cast<unsigned char>(s[0]);
    }
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        std::uint32_t v = 0;
        const auto [end, ec] = std::from_chars(s.data() + 2, s.data() + s.size(), v, 16);
        if (ec == std::errc() && end == s.data() + s.size()) {
            return static_cast<char32_t>(v);
        }
    }
    return std::nullopt;
}

} // namespace

ExpressionSet parse_expression_table(std::string_view text) {
    ExpressionSet set;
    bool weighted = false;
    std::size_t line_no = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = text.find('\n', pos);
        std::string_view line = text.substr(pos, end == std::string_view::npos ? text.npos : end - pos);
        pos = end == std::string_view::npos ? text.size() : end + 1;
        ++line_no;
        const auto t = tokens(line);
        if (t.empty() || t[0].starts_with("//")) {
            continue;
        }
        if (t[0] == "$keys") {
            set.keys.assign(t.begin() + 1, t.end());
            continue;
        }
        if (t[0] == "$hasweighting") {
            weighted = true;
            continue;
        }
        if (t[0].starts_with("$")) {
            set.diagnostics.push_back({line_no, "unknown directive '" + std::string(t[0]) + "'"});
            continue;
        }
        // name code value [weight] ... description
        const std::size_t per_key = weighted ? 2 : 1;
        const std::size_t expected = 3 + set.keys.size() * per_key;
        if (t.size() != expected) {
            set.diagnostics.push_back({line_no, "row has " + std::to_string(t.size()) + " fields, expected " +
                                                    std::to_string(expected)});
            continue;
        }
        Expression e;
        e.name = t[0];
        const auto code = to_code(t[1]);
        if (!code) {
            set.diagnostics.push_back({line_no, "bad code '" + std::string(t[1]) + "'"});
            continue;
        }
        e.code = *code;
        bool ok = true;
        for (std::size_t k = 0; k < set.keys.size(); ++k) {
            const auto v = to_float(t[2 + k * per_key]);
            const auto w = weighted ? to_float(t[3 + k * per_key]) : std::optional<float>(1.0f);
            ok = ok && v && w;
            e.values.push_back(v.value_or(0));
            e.weights.push_back(w.value_or(0));
        }
        if (!ok) {
            set.diagnostics.push_back({line_no, "bad number in row '" + e.name + "'"});
            continue;
        }
        e.description = t.back();
        set.expressions.push_back(std::move(e));
    }
    return set;
}

// Layout: "EFV\0", int32 version (0), the set's own path in a fixed-size
// field (128 bytes; 64 in two files from an older compiler), then the header:
//   length, setting count, setting offset, 0, index count, index offset,
//   key count, key name table offset, key mapping offset
// Setting records are 24 bytes: name offset (relative to the record), 0,
// count, code, 0, data offset (relative to the record). Data is count x
// {int32 key, float value, float weight}.
ExpressionSet parse_vfe(std::span<const std::byte> data) {
    detail::BinaryReader r(data, "VFE");
    if (data.size() < 8 || data[0] != std::byte{'E'} || data[1] != std::byte{'F'} || data[2] != std::byte{'V'} ||
        data[3] != std::byte{0}) {
        r.fail("bad signature");
    }
    r.seek(4);
    if (r.i32() != 0) {
        r.fail("unsupported version");
    }
    std::size_t header = 0;
    for (const std::size_t candidate : {std::size_t{0x88}, std::size_t{0x48}}) {
        if (candidate + 36 <= data.size() && detail::load_u32(data.data() + candidate) == data.size()) {
            header = candidate;
            break;
        }
    }
    if (header == 0) {
        r.fail("header not found");
    }
    r.seek(header + 4);
    const auto settings = r.i32();
    const auto setting_offset = r.i32();
    r.i32();
    r.i32(); // Index count: code -> setting lookup, implied by the codes.
    r.i32(); // Index offset.
    const auto key_count = r.i32();
    const auto key_names = r.i32();
    if (settings < 0 || key_count < 0 || settings > 65536 || key_count > 4096 || setting_offset < 0 || key_names < 0) {
        r.fail("header out of range");
    }

    auto string_at = [&](std::size_t offset) {
        if (offset >= data.size()) {
            r.fail("string offset out of range");
        }
        std::string s;
        for (std::size_t i = offset; i < data.size() && data[i] != std::byte{0}; ++i) {
            s += static_cast<char>(data[i]);
        }
        return s;
    };

    ExpressionSet set;
    r.seek(static_cast<std::size_t>(key_names));
    for (std::int32_t k = 0; k < key_count; ++k) {
        set.keys.push_back(string_at(r.u32()));
    }
    for (std::int32_t s = 0; s < settings; ++s) {
        const std::size_t record = static_cast<std::size_t>(setting_offset) + 24 * static_cast<std::size_t>(s);
        r.seek(record);
        const auto name = r.u32();
        r.i32();
        const auto count = r.i32();
        const auto code = r.u32();
        r.i32();
        const auto values = r.u32();
        Expression e;
        e.name = string_at(record + name);
        e.code = static_cast<char32_t>(code);
        e.values.assign(static_cast<std::size_t>(key_count), 0.0f);
        e.weights.assign(static_cast<std::size_t>(key_count), 0.0f);
        if (count < 0 || count > key_count) {
            r.fail("setting count out of range");
        }
        r.seek(record + values);
        for (std::int32_t i = 0; i < count; ++i) {
            const auto key = r.i32();
            const auto value = r.f32();
            const auto weight = r.f32();
            if (key < 0 || key >= key_count) {
                r.fail("key index out of range");
            }
            e.values[static_cast<std::size_t>(key)] = value;
            e.weights[static_cast<std::size_t>(key)] = weight;
        }
        set.expressions.push_back(std::move(e));
    }
    return set;
}

} // namespace openbl::formats
