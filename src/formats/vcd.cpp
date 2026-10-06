#include "openbl/formats/vcd.hpp"

#include <charconv>

namespace openbl::formats {
namespace {

struct Token {
    std::string_view text;
    std::size_t line = 0;
    bool quoted = false;
};

// Quoted strings (no escapes), braces, and bare words; "//" comments.
std::vector<Token> tokenize(std::string_view s) {
    std::vector<Token> out;
    std::size_t line = 1;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') {
                ++i;
            }
        } else if (c == '{' || c == '}') {
            out.push_back({s.substr(i, 1), line, false});
            ++i;
        } else if (c == '"') {
            const std::size_t start = ++i;
            const std::size_t start_line = line;
            while (i < s.size() && s[i] != '"') {
                line += s[i] == '\n';
                ++i;
            }
            out.push_back({s.substr(start, i - start), start_line, true});
            i += i < s.size();
        } else {
            const std::size_t start = i;
            while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n' && s[i] != '{' &&
                   s[i] != '}' && s[i] != '"') {
                ++i;
            }
            out.push_back({s.substr(start, i - start), line, false});
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

class Parser {
public:
    explicit Parser(std::string_view text) : tokens_(tokenize(text)) {}

    VcdScene run() {
        while (!done()) {
            const Token t = take();
            if (t.text == "actor") {
                VcdActor actor;
                actor.name = std::string(take().text);
                if (open()) {
                    parse_actor(actor);
                }
                scene_.actors.push_back(std::move(actor));
            } else if (t.text == "fps") {
                scene_.fps = static_cast<int>(number(take()));
            } else if (t.text == "snap") {
                scene_.snap = take().text == "on";
            } else {
                unknown(t);
            }
        }
        return std::move(scene_);
    }

private:
    bool done() const { return next_ >= tokens_.size(); }

    Token take() {
        if (done()) {
            warn(tokens_.empty() ? 0 : tokens_.back().line, "unexpected end of file");
            return {};
        }
        return tokens_[next_++];
    }

    bool at_close() const { return !done() && !tokens_[next_].quoted && tokens_[next_].text == "}"; }

    void warn(std::size_t line, std::string message) { scene_.diagnostics.push_back({line, std::move(message)}); }

    bool open() {
        const Token t = take();
        if (t.quoted || t.text != "{") {
            warn(t.line, "expected '{'");
            return false;
        }
        return true;
    }

    // Consumes the closing brace; reports an unterminated block at end of file.
    bool close() {
        if (at_close()) {
            ++next_;
            return true;
        }
        return false;
    }

    float number(const Token& t) {
        const auto v = to_float(t.text);
        if (!v) {
            warn(t.line, "expected a number, got '" + std::string(t.text) + "'");
        }
        return v.value_or(0.0f);
    }

    // Skips an unknown keyword and, if one follows, its block.
    void unknown(const Token& t) {
        warn(t.line, "unknown keyword '" + std::string(t.text) + "'");
        if (!done() && tokens_[next_].text == "{" && !tokens_[next_].quoted) {
            ++next_;
            for (int depth = 1; !done() && depth > 0;) {
                const Token& u = tokens_[next_++];
                if (!u.quoted) {
                    depth += (u.text == "{") - (u.text == "}");
                }
            }
        }
    }

    void parse_actor(VcdActor& actor) {
        while (!done() && !close()) {
            const Token t = take();
            if (t.text == "channel") {
                VcdChannel channel;
                channel.name = std::string(take().text);
                if (open()) {
                    parse_channel(channel);
                }
                actor.channels.push_back(std::move(channel));
            } else if (t.text == "active") {
                actor.active = take().text != "0";
            } else if (t.text == "bonerename") {
                std::string from(take().text);
                actor.bone_renames.emplace_back(std::move(from), std::string(take().text));
            } else if (t.text == "faceposermodel") {
                actor.faceposer_model = std::string(take().text);
            } else {
                unknown(t);
            }
        }
    }

    void parse_channel(VcdChannel& channel) {
        while (!done() && !close()) {
            const Token t = take();
            if (t.text == "event") {
                VcdEvent event;
                event.type = std::string(take().text);
                event.name = std::string(take().text);
                if (open()) {
                    parse_event(event);
                }
                channel.events.push_back(std::move(event));
            } else if (t.text == "active") {
                channel.active = take().text != "0";
            } else {
                unknown(t);
            }
        }
    }

    void parse_event(VcdEvent& event) {
        while (!done() && !close()) {
            const Token t = take();
            if (t.text == "time") {
                event.start = number(take());
                event.end = number(take());
            } else if (t.text == "param") {
                event.param = std::string(take().text);
            } else if (t.text == "param2") {
                event.param2 = std::string(take().text);
            } else if (t.text == "fixedlength") {
                event.fixed_length = true;
            } else if (t.text == "sequenceduration") {
                event.sequence_duration = number(take());
            } else if (t.text == "event_ramp") {
                if (open()) {
                    while (!done() && !close()) {
                        const float time = number(take());
                        event.ramp.push_back({time, number(take())});
                    }
                }
            } else {
                unknown(t);
            }
        }
    }

    std::vector<Token> tokens_;
    std::size_t next_ = 0;
    VcdScene scene_;
};

} // namespace

VcdScene parse_vcd(std::string_view text) {
    return Parser(text).run();
}

} // namespace openbl::formats
