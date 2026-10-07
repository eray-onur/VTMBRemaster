#include "openbl/formats/script_text.hpp"

#include <charconv>

namespace openbl::formats {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Calls fn(line_number, line) for each line, without line endings.
template <typename Fn> void for_lines(std::string_view text, Fn&& fn) {
    std::size_t n = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        fn(++n, line);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    }
}

bool is_comment(std::string_view line) {
    return line.starts_with("//");
}

} // namespace

ExperienceTable parse_experience_table(std::string_view text) {
    ExperienceTable table;
    for_lines(text, [&](std::size_t n, std::string_view line) {
        const std::string_view t = trim(line);
        if (t.empty() || t.front() == '>') {
            return;
        }
        const std::size_t a = t.find('|');
        const std::size_t b = a == std::string_view::npos ? a : t.find('|', a + 1);
        if (b == std::string_view::npos) {
            table.diagnostics.push_back({n, "expected Title | Description | XP"});
            return;
        }
        ExperienceRow row;
        row.title = trim(t.substr(0, a));
        row.description = trim(t.substr(a + 1, b - a - 1));
        const std::string_view xp = trim(t.substr(b + 1));
        const auto [end, ec] = std::from_chars(xp.data(), xp.data() + xp.size(), row.experience);
        if (ec != std::errc() || end != xp.data() + xp.size()) {
            table.diagnostics.push_back({n, "experience value is not a number"});
            return;
        }
        table.rows.push_back(std::move(row));
    });
    return table;
}

std::vector<CreditsLine> parse_credits(std::string_view text) {
    std::vector<CreditsLine> out;
    for_lines(text, [&](std::size_t, std::string_view line) {
        if (line.starts_with('#')) {
            return;
        }
        CreditsLine l;
        const char c = line.empty() ? '\0' : line.front();
        switch (c) {
        case '@':
            l.kind = CreditsLine::Kind::Font;
            break;
        case '$':
            l.kind = CreditsLine::Kind::Color;
            break;
        case '%':
            l.kind = CreditsLine::Kind::Shadow;
            break;
        case '^':
            l.kind = CreditsLine::Kind::Box;
            break;
        default:
            l.kind = CreditsLine::Kind::Text;
        }
        l.value = l.kind == CreditsLine::Kind::Text ? std::string(trim(line)) : std::string(trim(line.substr(1)));
        out.push_back(std::move(l));
    });
    return out;
}

SentenceList parse_sentences(std::string_view text) {
    SentenceList list;
    for_lines(text, [&](std::size_t n, std::string_view line) {
        const std::string_view t = trim(line);
        if (t.empty() || is_comment(t)) {
            return;
        }
        const std::size_t sp = t.find_first_of(" \t");
        if (sp == std::string_view::npos) {
            list.diagnostics.push_back({n, "sentence without a sound"});
            return;
        }
        Sentence s;
        s.name = t.substr(0, sp);
        std::string_view rest = trim(t.substr(sp));
        if (const std::size_t brace = rest.find('{'); brace != std::string_view::npos) {
            const std::size_t close = rest.find('}', brace);
            s.options = trim(rest.substr(brace + 1, close == std::string_view::npos ? close : close - brace - 1));
            rest = trim(rest.substr(0, brace));
        }
        s.sound = rest;
        list.sentences.push_back(std::move(s));
    });
    return list;
}

TitleList parse_titles(std::string_view text) {
    TitleList list;
    std::vector<std::pair<std::string, std::string>> settings;
    std::string pending_name;
    bool in_block = false;
    std::string block;
    for_lines(text, [&](std::size_t n, std::string_view line) {
        const std::string_view t = trim(line);
        if (in_block) {
            if (t == "}") {
                list.titles.push_back({pending_name, block, settings});
                in_block = false;
                block.clear();
                pending_name.clear();
            } else {
                block += (block.empty() ? "" : "\n") + std::string(line);
            }
            return;
        }
        if (t.empty() || is_comment(t)) {
            return;
        }
        if (t.front() == '$') {
            const std::size_t sp = t.find_first_of(" \t");
            std::string name(t.substr(1, sp == std::string_view::npos ? sp : sp - 1));
            std::string value(sp == std::string_view::npos ? std::string_view() : trim(t.substr(sp)));
            bool replaced = false;
            for (auto& [k, v] : settings) {
                if (k == name) {
                    v = value;
                    replaced = true;
                }
            }
            if (!replaced) {
                settings.emplace_back(std::move(name), std::move(value));
            }
        } else if (t == "{") {
            if (pending_name.empty()) {
                list.diagnostics.push_back({n, "title block without a name"});
            }
            in_block = true;
        } else {
            pending_name = t;
        }
    });
    if (in_block) {
        list.diagnostics.push_back({0, "unterminated title block"});
    }
    return list;
}

DspPresetList parse_dsp_presets(std::string_view text) {
    DspPresetList list;
    // Tokenise: braces and whitespace-separated words, skipping // comments.
    struct Token {
        std::string_view text;
        std::size_t line;
    };
    std::vector<Token> tokens;
    for_lines(text, [&](std::size_t n, std::string_view line) {
        if (const std::size_t c = line.find("//"); c != std::string_view::npos) {
            line = line.substr(0, c);
        }
        std::size_t i = 0;
        while (i < line.size()) {
            if (is_space(line[i])) {
                ++i;
            } else if (line[i] == '{' || line[i] == '}') {
                tokens.push_back({line.substr(i, 1), n});
                ++i;
            } else {
                const std::size_t s = i;
                while (i < line.size() && !is_space(line[i]) && line[i] != '{' && line[i] != '}') {
                    ++i;
                }
                tokens.push_back({line.substr(s, i - s), n});
            }
        }
    });
    std::size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].text != "{") {
            // One diagnostic per line (dsp_presets.txt has an uncommented column header).
            if (list.diagnostics.empty() || list.diagnostics.back().line != tokens[i].line) {
                list.diagnostics.push_back({tokens[i].line, "text outside a preset ignored"});
            }
            ++i;
            continue;
        }
        ++i;
        DspPreset preset;
        bool closed = false;
        while (i < tokens.size()) {
            if (tokens[i].text == "}") {
                ++i;
                closed = true;
                break;
            }
            if (tokens[i].text == "{") {
                DspProcessor proc;
                ++i;
                while (i < tokens.size() && tokens[i].text != "}") {
                    proc.tokens.emplace_back(tokens[i++].text);
                }
                ++i;
                preset.processors.push_back(std::move(proc));
            } else {
                preset.header.emplace_back(tokens[i++].text);
            }
        }
        if (!closed) {
            list.diagnostics.push_back({tokens.empty() ? 0 : tokens.back().line, "unterminated preset"});
        }
        list.presets.push_back(std::move(preset));
    }
    return list;
}

} // namespace openbl::formats
