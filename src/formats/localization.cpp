#include "openbl/formats/localization.hpp"

#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/text_encoding.hpp"

namespace openbl::formats {
namespace {

bool starts_with(std::span<const std::byte> data, std::initializer_list<unsigned char> prefix) {
    if (data.size() < prefix.size()) {
        return false;
    }
    std::size_t i = 0;
    for (const unsigned char b : prefix) {
        if (std::to_integer<unsigned char>(data[i++]) != b) {
            return false;
        }
    }
    return true;
}

bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) {
            return false;
        }
        for (std::size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) {
                return false;
            }
        }
        i += n;
    }
    return true;
}

std::string expand_newlines(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += '\n';
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace

Localization parse_localization(std::span<const std::byte> data) {
    std::string text;
    if (starts_with(data, {0xFF, 0xFE})) {
        text = utf16le_to_utf8(data.subspan(2));
    } else {
        if (starts_with(data, {0xEF, 0xBB, 0xBF})) {
            data = data.subspan(3);
        }
        text.assign(reinterpret_cast<const char*>(data.data()), data.size());
        if (!valid_utf8(text)) {
            text = cp1252_to_utf8(text);
        }
    }

    Localization loc;
    const auto doc = parse_keyvalues(text);
    loc.diagnostics = doc.diagnostics;
    const KvNode* lang = doc.roots.empty() ? nullptr : &doc.roots.front();
    if (!lang || !iequals(lang->key, "lang") || !lang->is_block) {
        loc.diagnostics.push_back({1, "missing \"lang\" block"});
        return loc;
    }
    if (const KvNode* name = lang->find("Language")) {
        loc.language = name->value;
    }
    const KvNode* tokens = lang->find("Tokens");
    if (!tokens || !tokens->is_block) {
        loc.diagnostics.push_back({lang->line, "missing \"Tokens\" block"});
        return loc;
    }
    for (const auto& t : tokens->children) {
        if (t.is_block) {
            loc.diagnostics.push_back({t.line, "unexpected block in Tokens"});
            continue;
        }
        if (!loc.tokens.emplace(t.key, expand_newlines(t.value)).second) {
            loc.diagnostics.push_back({t.line, "duplicate token '" + t.key + "'"});
        }
    }
    return loc;
}

} // namespace openbl::formats
