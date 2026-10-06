#include "openbl/formats/keyvalues.hpp"

#include <algorithm>

namespace openbl::formats {

namespace {

constexpr int kMaxDepth = 64;

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

bool is_word_char(char c) {
    return !is_space(c) && c != '"' && c != '{' && c != '}' && c != '/';
}

struct Token {
    enum class Kind { String, Open, Close, End } kind = Kind::End;
    std::string text;
    std::size_t line = 0;
};

class Tokenizer {
public:
    Tokenizer(std::string_view text, const KvOptions& options, std::vector<KvDiagnostic>& diagnostics)
        : text_(text), options_(options), diagnostics_(diagnostics) {}

    Token next() {
        skip_space_and_comments();
        Token tok;
        tok.line = line_;
        if (pos_ >= text_.size()) {
            return tok;
        }
        const char c = text_[pos_];
        if (c == '{' || c == '}') {
            ++pos_;
            tok.kind = c == '{' ? Token::Kind::Open : Token::Kind::Close;
            return tok;
        }
        tok.kind = Token::Kind::String;
        if (c == '"') {
            const std::size_t start = ++pos_;
            while (pos_ < text_.size() && text_[pos_] != '"') {
                if (options_.escaped_quotes && text_[pos_] == '\\' && pos_ + 1 < text_.size() &&
                    text_[pos_ + 1] == '"') {
                    ++pos_;
                }
                line_ += text_[pos_] == '\n';
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                tok.text = text_.substr(start);
                diagnostics_.push_back({tok.line, "unterminated quoted string"});
                return tok;
            }
            // A closing quote directly followed by a word character was really
            // the opening quote of the next token: this string lost its closing
            // quote. It ends at the last line break before that quote if only
            // whitespace follows the break (`"buoy.wav`, unclosed multi-line
            // journal text), else at its first tab (`"MinBlinkInterval  "2.5"`).
            if (pos_ + 1 < text_.size() && is_word_char(text_[pos_ + 1])) {
                const std::string_view body = text_.substr(start, pos_ - start);
                std::size_t cut = std::string_view::npos;
                const std::size_t last_break = body.rfind('\n');
                if (last_break != std::string_view::npos &&
                    body.find_first_not_of(" \t\r", last_break + 1) == std::string_view::npos) {
                    cut = last_break;
                } else if (last_break == std::string_view::npos) {
                    cut = body.find('\t');
                }
                if (cut != std::string_view::npos) {
                    std::string_view s = body.substr(0, cut);
                    while (!s.empty() && is_space(s.back())) {
                        s.remove_suffix(1);
                    }
                    tok.text = s;
                    pos_ = start + cut;
                    line_ = tok.line + static_cast<std::size_t>(std::count(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(cut), '\n'));
                    diagnostics_.push_back({tok.line, "missing closing quote"});
                    return tok;
                }
            }
            tok.text = body_unescaped(start, pos_);
            ++pos_;
            return tok;
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !is_space(text_[pos_]) && text_[pos_] != '"' && text_[pos_] != '{' &&
               text_[pos_] != '}') {
            ++pos_;
        }
        tok.text = text_.substr(start, pos_ - start);
        return tok;
    }

private:
    std::string body_unescaped(std::size_t begin, std::size_t end) const {
        std::string out(text_.substr(begin, end - begin));
        if (options_.escaped_quotes) {
            for (std::size_t i = 0; (i = out.find("\\\"", i)) != std::string::npos; ++i) {
                out.erase(i, 1);
            }
        }
        return out;
    }

    void skip_space_and_comments() {
        while (pos_ < text_.size()) {
            if (is_space(text_[pos_])) {
                line_ += text_[pos_] == '\n';
                ++pos_;
            } else if (text_.substr(pos_, 2) == "//") {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
            } else if (text_.substr(pos_, 2) == "/*") {
                // Block comments: used in particle files to disable whole sections.
                const std::size_t start_line = line_;
                const std::size_t end = text_.find("*/", pos_ + 2);
                const std::size_t stop = end == std::string_view::npos ? text_.size() : end + 2;
                line_ += static_cast<std::size_t>(std::count(text_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                                             text_.begin() + static_cast<std::ptrdiff_t>(stop), '\n'));
                pos_ = stop;
                if (end == std::string_view::npos) {
                    diagnostics_.push_back({start_line, "unterminated block comment"});
                }
            } else {
                break;
            }
        }
    }

    std::string_view text_;
    std::vector<KvDiagnostic>& diagnostics_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    KvOptions options_;
};

class Parser {
public:
    Parser(std::string_view text, const KvOptions& options, KvDocument& doc)
        : tokens_(text, options, doc.diagnostics), doc_(doc) {}

    void run() { parse_block(doc_.roots, 0, {}); }

private:
    void diag(std::size_t line, std::string message) { doc_.diagnostics.push_back({line, std::move(message)}); }

    // Parses items until the block's closing '}' (consumed) or end of input.
    // Returns false if parsing must stop (end of input or depth limit).
    bool parse_block(std::vector<KvNode>& out, int depth, const KvNode* owner) {
        for (;;) {
            Token tok = tokens_.next();
            switch (tok.kind) {
            case Token::Kind::End:
                if (owner) {
                    diag(owner->line, "block '" + owner->key + "' is not closed");
                }
                return false;
            case Token::Kind::Close:
                if (depth == 0) {
                    diag(tok.line, "unmatched '}'");
                    continue;
                }
                return true;
            case Token::Kind::Open: {
                diag(tok.line, "block without a key");
                KvNode node;
                node.is_block = true;
                node.line = tok.line;
                if (!open_block(out, std::move(node), depth)) {
                    return false;
                }
                continue;
            }
            case Token::Kind::String:
                break;
            }

            KvNode node;
            node.key = std::move(tok.text);
            node.line = tok.line;
            Token value = tokens_.next();
            switch (value.kind) {
            case Token::Kind::String:
                node.value = std::move(value.text);
                out.push_back(std::move(node));
                break;
            case Token::Kind::Open:
                node.is_block = true;
                if (!open_block(out, std::move(node), depth)) {
                    return false;
                }
                break;
            case Token::Kind::Close:
                diag(node.line, "key '" + node.key + "' has no value");
                out.push_back(std::move(node));
                if (depth > 0) {
                    return true;
                }
                break;
            case Token::Kind::End:
                diag(node.line, "key '" + node.key + "' has no value");
                out.push_back(std::move(node));
                if (owner) {
                    diag(owner->line, "block '" + owner->key + "' is not closed");
                }
                return false;
            }
        }
    }

    bool open_block(std::vector<KvNode>& out, KvNode node, int depth) {
        if (depth + 1 >= kMaxDepth) {
            diag(node.line, "blocks nested too deeply");
            return false;
        }
        out.push_back(std::move(node));
        // Children are parsed into a local vector: `out` may reallocate while we recurse.
        std::vector<KvNode> children;
        const KvNode owner_info{out.back().key, {}, {}, true, out.back().line};
        const bool keep_going = parse_block(children, depth + 1, &owner_info);
        out.back().children = std::move(children);
        return keep_going;
    }

    Tokenizer tokens_;
    KvDocument& doc_;
};

} // namespace

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}

const KvNode* KvNode::find(std::string_view child_key) const {
    for (const KvNode& child : children) {
        if (iequals(child.key, child_key)) {
            return &child;
        }
    }
    return nullptr;
}

KvDocument parse_keyvalues(std::string_view text, const KvOptions& options) {
    KvDocument doc;
    Parser(text, options, doc).run();
    return doc;
}

} // namespace openbl::formats
