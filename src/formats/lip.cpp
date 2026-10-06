#include "openbl/formats/lip.hpp"

#include "openbl/formats/text_encoding.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <span>

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

std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_space(s[i])) {
            ++i;
        }
        const std::size_t start = i;
        while (i < s.size() && !is_space(s[i])) {
            ++i;
        }
        if (i > start) {
            out.push_back(s.substr(start, i - start));
        }
    }
    return out;
}

template <typename T>
std::optional<T> parse_number(std::string_view s) {
    T v{};
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

class Parser {
public:
    explicit Parser(std::string_view data) : data_(data) {}

    LipFile run() {
        const auto version = split(next_line());
        if (version.size() == 2 && version[0] == "VERSION") {
            file_.version = std::string(version[1]);
        } else {
            warn("missing VERSION line");
        }
        while (!eof()) {
            const std::string_view header = next_line();
            if (header.empty()) {
                continue;
            }
            if (!expect_open()) {
                continue;
            }
            if (header == "PLAINTEXT") {
                plaintext();
            } else if (header == "WORDS") {
                words();
            } else if (header == "EMPHASIS") {
                block_lines(file_.emphasis);
            } else if (header == "CLOSECAPTION") {
                captions();
            } else if (header == "OPTIONS") {
                options();
            } else {
                warn("unknown section '" + std::string(header) + "' skipped");
                skip_block();
            }
        }
        return std::move(file_);
    }

private:
    bool eof() const { return pos_ >= data_.size(); }

    // Next line without its line ending, trimmed.
    std::string_view next_line() {
        const std::size_t end = data_.find('\n', pos_);
        const std::size_t stop = end == std::string_view::npos ? data_.size() : end;
        const std::string_view line = data_.substr(pos_, stop - pos_);
        pos_ = end == std::string_view::npos ? data_.size() : end + 1;
        ++line_;
        return trim(line);
    }

    void warn(std::string message) { file_.diagnostics.push_back({line_, std::move(message)}); }

    bool expect_open() {
        while (!eof()) {
            const std::string_view l = next_line();
            if (l.empty()) {
                continue;
            }
            if (l == "{") {
                return true;
            }
            warn("expected '{'");
            return false;
        }
        warn("unexpected end of file");
        return false;
    }

    void skip_block() {
        int depth = 1;
        while (!eof() && depth > 0) {
            const std::string_view l = next_line();
            depth += (l == "{") - (l == "}");
        }
    }

    void block_lines(std::vector<std::string>& out) {
        while (!eof()) {
            const std::string_view l = next_line();
            if (l == "}") {
                return;
            }
            if (!l.empty()) {
                out.emplace_back(l);
            }
        }
        warn("unterminated block");
    }

    void plaintext() {
        std::vector<std::string> lines;
        block_lines(lines);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            file_.plaintext += (i ? "\n" : "") + lines[i];
        }
    }

    void words() {
        while (!eof()) {
            const std::string_view l = next_line();
            if (l == "}") {
                return;
            }
            if (l.empty()) {
                continue;
            }
            if (l == "{") {
                skip_block(); // Phonemes of a malformed WORD line.
                continue;
            }
            const auto t = split(l);
            const auto start = t.size() == 4 ? parse_number<float>(t[2]) : std::nullopt;
            const auto end = t.size() == 4 ? parse_number<float>(t[3]) : std::nullopt;
            if (t[0] != "WORD" || !start || !end) {
                warn("malformed WORD line");
                continue;
            }
            LipWord word{std::string(t[1]), *start, *end, {}};
            if (expect_open()) {
                phonemes(word);
            }
            file_.words.push_back(std::move(word));
        }
        warn("unterminated WORDS block");
    }

    void phonemes(LipWord& word) {
        while (!eof()) {
            const std::string_view l = next_line();
            if (l == "}") {
                return;
            }
            if (l.empty()) {
                continue;
            }
            // code name start end weight [flags]
            const auto t = split(l);
            if (t.size() != 5 && t.size() != 6) {
                warn("malformed phoneme line");
                continue;
            }
            const auto code = parse_number<std::uint32_t>(t[0]);
            const auto start = parse_number<float>(t[2]);
            const auto end = parse_number<float>(t[3]);
            const auto weight = parse_number<float>(t[4]);
            const auto flags = t.size() == 6 ? parse_number<int>(t[5]) : std::optional<int>(0);
            if (!code || !start || !end || !weight || !flags) {
                warn("malformed phoneme line");
                continue;
            }
            word.phonemes.push_back({static_cast<char32_t>(*code), std::string(t[1]), *start, *end, *weight, *flags});
        }
        warn("unterminated WORD block");
    }

    void captions() {
        while (!eof()) {
            const std::string_view language = next_line();
            if (language == "}") {
                return;
            }
            if (language.empty()) {
                continue;
            }
            if (!expect_open()) {
                continue;
            }
            phrases(std::string(language));
        }
        warn("unterminated CLOSECAPTION block");
    }

    // PHRASE <encoding> <byte count> <bytes> <start> <end>. The text is raw
    // UTF-16LE and may contain any byte, including line breaks.
    void phrases(const std::string& language) {
        while (!eof()) {
            std::size_t p = pos_;
            while (p < data_.size() && is_space(data_[p])) {
                line_ += data_[p] == '\n';
                ++p;
            }
            pos_ = p;
            constexpr std::string_view kPhrase = "PHRASE ";
            if (!data_.substr(pos_).starts_with(kPhrase)) {
                const std::string_view l = next_line();
                if (l == "}") {
                    return;
                }
                warn("expected PHRASE");
                continue;
            }
            std::size_t q = pos_ + kPhrase.size();
            auto token = [&] {
                const std::size_t s = q;
                while (q < data_.size() && data_[q] != ' ' && data_[q] != '\n') {
                    ++q;
                }
                const std::string_view tok = data_.substr(s, q - s);
                if (q < data_.size() && data_[q] == ' ') {
                    ++q;
                }
                return tok;
            };
            const std::string_view encoding = token();
            const auto count = parse_number<std::size_t>(token());
            if (encoding != "unicode" || !count || q + *count > data_.size()) {
                warn("unsupported or malformed PHRASE");
                next_line();
                continue;
            }
            LipCaption caption;
            caption.language = language;
            caption.text = utf16le_to_utf8(std::as_bytes(std::span(data_.data() + q, *count)));
            for (std::size_t i = q; i < q + *count; ++i) {
                line_ += data_[i] == '\n';
            }
            pos_ = q + *count;
            const auto t = split(next_line());
            const auto start = t.size() == 2 ? parse_number<float>(t[0]) : std::nullopt;
            const auto end = t.size() == 2 ? parse_number<float>(t[1]) : std::nullopt;
            if (!start || !end) {
                warn("malformed PHRASE timing");
                continue;
            }
            caption.start = *start;
            caption.end = *end;
            file_.captions.push_back(std::move(caption));
        }
        warn("unterminated caption block");
    }

    void options() {
        while (!eof()) {
            const std::string_view l = next_line();
            if (l == "}") {
                return;
            }
            if (l.empty()) {
                continue;
            }
            const std::size_t space = l.find_first_of(" \t");
            const std::string_view key = l.substr(0, space);
            const std::string_view value = space == std::string_view::npos ? std::string_view() : trim(l.substr(space));
            file_.options.emplace_back(key, value);
        }
        warn("unterminated OPTIONS block");
    }

    std::string_view data_;
    std::size_t pos_ = 0;
    std::size_t line_ = 0;
    LipFile file_;
};

} // namespace

LipFile parse_lip(std::string_view data) {
    return Parser(data).run();
}

} // namespace openbl::formats
