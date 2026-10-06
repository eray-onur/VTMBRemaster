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

struct Token {
    enum class Kind { String, Open, Close, End } kind = Kind::End;
    std::string text;
    std::size_t line = 0;
};

class Tokenizer {
public:
    Tokenizer(std::string_view text, std::vector<KvDiagnostic>& diagnostics)
        : text_(text), diagnostics_(diagnostics) {}

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
                line_ += text_[pos_] == '\n';
                ++pos_;
            }
            tok.text = text_.substr(start, pos_ - start);
            if (pos_ >= text_.size()) {
                diagnostics_.push_back({tok.line, "unterminated quoted string"});
            } else {
                ++pos_;
            }
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
    void skip_space_and_comments() {
        while (pos_ < text_.size()) {
            if (is_space(text_[pos_])) {
                line_ += text_[pos_] == '\n';
                ++pos_;
            } else if (text_.substr(pos_, 2) == "//") {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
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
};

class Parser {
public:
    Parser(std::string_view text, KvDocument& doc) : tokens_(text, doc.diagnostics), doc_(doc) {}

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

KvDocument parse_keyvalues(std::string_view text) {
    KvDocument doc;
    Parser(text, doc).run();
    return doc;
}

} // namespace openbl::formats
