#include "openbl/formats/console_script.hpp"

namespace openbl::formats {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Tokens of a brace-structured script: quoted strings, bare words, '{' and '}'.
struct Token {
    std::string text;
    bool quoted = false;
    std::size_t line = 0;
};

std::vector<Token> tokenize_braces(std::string_view text) {
    std::vector<Token> out;
    std::size_t line = 1;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (is_space(c)) {
            ++i;
        } else if (text.substr(i, 2) == "//") {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
        } else if (c == '{' || c == '}') {
            out.push_back({std::string(1, c), false, line});
            ++i;
        } else if (c == '"') {
            const std::size_t end = text.find_first_of("\"\n", i + 1);
            const std::size_t stop = end == std::string_view::npos ? text.size() : end;
            out.push_back({std::string(text.substr(i + 1, stop - i - 1)), true, line});
            i = stop < text.size() && text[stop] == '"' ? stop + 1 : stop;
        } else {
            const std::size_t start = i;
            while (i < text.size() && !is_space(text[i]) && text[i] != '{' && text[i] != '}' && text[i] != '"') {
                ++i;
            }
            out.push_back({std::string(text.substr(start, i - start)), false, line});
        }
    }
    return out;
}

} // namespace

ConsoleScript parse_console_script(std::string_view text) {
    ConsoleScript script;
    ConsoleCommand current;
    std::size_t line = 1;
    auto finish = [&] {
        if (!current.tokens.empty()) {
            script.commands.push_back(std::move(current));
        }
        current = {};
    };
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\n' || c == ';') {
            finish();
            line += c == '\n';
            ++i;
        } else if (is_space(c)) {
            ++i;
        } else if (text.substr(i, 2) == "//") {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
        } else {
            if (current.tokens.empty()) {
                current.line = line;
            }
            if (c == '"') {
                const std::size_t end = text.find_first_of("\"\n", i + 1);
                const std::size_t stop = end == std::string_view::npos ? text.size() : end;
                current.tokens.emplace_back(text.substr(i + 1, stop - i - 1));
                if (stop >= text.size() || text[stop] != '"') {
                    script.diagnostics.push_back({line, "unterminated quoted string"});
                    i = stop;
                } else {
                    i = stop + 1;
                }
            } else {
                const std::size_t start = i;
                while (i < text.size() && !is_space(text[i]) && text[i] != ';' && text[i] != '"' &&
                       text.substr(i, 2) != "//") {
                    ++i;
                }
                current.tokens.emplace_back(text.substr(start, i - start));
            }
        }
    }
    finish();
    return script;
}

SettingsScript parse_settings_script(std::string_view text) {
    SettingsScript out;
    const auto tokens = tokenize_braces(text);
    std::size_t i = 0;
    auto at = [&](std::size_t k) -> const Token* { return k < tokens.size() ? &tokens[k] : nullptr; };
    auto is = [&](std::size_t k, std::string_view s) { return at(k) && !at(k)->quoted && at(k)->text == s; };
    auto fail = [&](std::string message) {
        out.diagnostics.push_back({at(i) ? at(i)->line : 0, std::move(message)});
    };
    while (i < tokens.size()) {
        if (is(i, "VERSION") && at(i + 1)) {
            out.version = tokens[i + 1].text;
            i += 2;
        } else if (is(i, "DESCRIPTION") && at(i + 1) && is(i + 2, "{")) {
            out.description = tokens[i + 1].text;
            i += 3;
            while (i < tokens.size() && !is(i, "}")) {
                // "cvar" { "prompt" { TYPE info... } { "default" } }
                SettingsOption o;
                o.cvar = tokens[i].text;
                if (!is(i + 1, "{") || !at(i + 2) || !is(i + 3, "{") || !at(i + 4)) {
                    fail("malformed option");
                    return out;
                }
                o.prompt = tokens[i + 2].text;
                o.type = tokens[i + 4].text;
                i += 5;
                while (i < tokens.size() && !is(i, "}")) {
                    if (o.type == "LIST" && at(i + 1) && !is(i + 1, "}")) {
                        o.list.emplace_back(tokens[i].text, tokens[i + 1].text);
                        i += 2;
                    } else {
                        o.info.push_back(tokens[i++].text);
                    }
                }
                ++i; // Close the type block.
                if (!is(i, "{") || !at(i + 1)) {
                    fail("option without a default");
                    return out;
                }
                o.default_value = is(i + 1, "}") ? std::string() : tokens[i + 1].text;
                i += is(i + 1, "}") ? 2 : 3; // Default block.
                if (!is(i, "}")) {
                    fail("unterminated option");
                    return out;
                }
                ++i; // Close the option.
                out.options.push_back(std::move(o));
            }
            ++i; // Close DESCRIPTION.
        } else {
            fail("unexpected '" + tokens[i].text + "'");
            ++i;
        }
    }
    return out;
}

} // namespace openbl::formats
