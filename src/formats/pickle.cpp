#include "openbl/formats/pickle.hpp"

#include "openbl/formats/error.hpp"

#include <charconv>
#include <cstring>
#include <map>

namespace openbl::formats {
namespace {

constexpr std::size_t kMaxStack = 1 << 16;
constexpr std::size_t kMaxItems = 1 << 22;

using Kind = PickleValue::Kind;

[[noreturn]] void fail(const std::string& what) {
    throw FormatError("pickle: " + what);
}

// Python 2 string repr: 'text' or "text" with backslash escapes.
std::string unrepr(std::string_view s) {
    if (s.size() < 2 || (s.front() != '\'' && s.front() != '"') || s.back() != s.front()) {
        fail("malformed string literal");
    }
    s = s.substr(1, s.size() - 2);
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out += s[i];
            continue;
        }
        if (++i >= s.size()) {
            fail("dangling escape");
        }
        const char c = s[i];
        switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'v': out += '\v'; break;
        case '\\': case '\'': case '"': out += c; break;
        case 'x': {
            if (i + 2 >= s.size()) {
                fail("short \\x escape");
            }
            unsigned v = 0;
            const auto r = std::from_chars(s.data() + i + 1, s.data() + std::min(i + 3, s.size()), v, 16);
            if (r.ec != std::errc() || r.ptr != s.data() + i + 3) {
                fail("bad \\x escape");
            }
            out += static_cast<char>(v);
            i += 2;
            break;
        }
        default:
            if (c >= '0' && c <= '7') {
                unsigned v = 0;
                std::size_t n = 0;
                while (n < 3 && i + n < s.size() && s[i + n] >= '0' && s[i + n] <= '7') {
                    v = v * 8 + static_cast<unsigned>(s[i + n] - '0');
                    ++n;
                }
                out += static_cast<char>(v & 0xff);
                i += n - 1;
            } else {
                out += '\\';
                out += c;
            }
        }
    }
    return out;
}

// raw-unicode-escape (protocol 0 UNICODE): bytes are Latin-1 except \uXXXX.
std::string raw_unicode(std::string_view s) {
    std::string out;
    auto put = [&](std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 6 <= s.size() && s[i + 1] == 'u') {
            unsigned v = 0;
            const auto r = std::from_chars(s.data() + i + 2, s.data() + i + 6, v, 16);
            if (r.ec == std::errc() && r.ptr == s.data() + i + 6) {
                put(v);
                i += 5;
                continue;
            }
        }
        put(static_cast<unsigned char>(s[i]));
    }
    return out;
}

class Reader {
public:
    explicit Reader(std::span<const std::byte> d) : d_(d) {}

    PickleValue run() {
        while (true) {
            const char op = static_cast<char>(byte());
            switch (op) {
            case '(': push({.kind = Kind::Mark}); break;
            case '.': {
                if (stack_.size() != 1) {
                    fail("stack not balanced at STOP");
                }
                return std::move(stack_.back());
            }
            case '0': pop(); break;
            case '1': pop_mark(); break;
            case '2': push(top()); break;
            case 'N': push({}); break;
            case 'I': {
                const std::string_view l = line();
                if (l == "00" || l == "01") {
                    push({.kind = Kind::Bool, .boolean = l == "01"});
                } else {
                    push(int_value(l));
                }
                break;
            }
            case 'L': {
                std::string_view l = line();
                if (!l.empty() && l.back() == 'L') {
                    l.remove_suffix(1);
                }
                push(int_value(l));
                break;
            }
            case 'F': {
                const std::string l(line());
                char* end = nullptr;
                const double v = std::strtod(l.c_str(), &end);
                if (end == l.c_str()) {
                    fail("bad float");
                }
                push({.kind = Kind::Float, .real = v});
                break;
            }
            case 'S': push({.kind = Kind::String, .text = unrepr(line())}); break;
            case 'V': push({.kind = Kind::Unicode, .text = raw_unicode(line())}); break;
            case 'J': push(int64(static_cast<std::int32_t>(u32()))); break;
            case 'K': push(int64(byte())); break;
            case 'M': push(int64(u16())); break;
            case 'G': {
                std::uint64_t bits = 0;
                for (int k = 0; k < 8; ++k) {
                    bits = (bits << 8) | byte();
                }
                double v;
                std::memcpy(&v, &bits, 8);
                push({.kind = Kind::Float, .real = v});
                break;
            }
            case 'T': push({.kind = Kind::String, .text = bytes(u32())}); break;
            case 'U': push({.kind = Kind::String, .text = bytes(byte())}); break;
            case 'X': push({.kind = Kind::Unicode, .text = bytes(u32())}); break;
            case '}': push({.kind = Kind::Dict}); break;
            case ']': push({.kind = Kind::List}); break;
            case ')': push({.kind = Kind::Tuple}); break;
            case 'd': {
                auto items = pop_mark();
                PickleValue d{.kind = Kind::Dict};
                if (items.size() % 2) {
                    fail("odd dict item count");
                }
                for (std::size_t k = 0; k < items.size(); k += 2) {
                    d.dict.emplace_back(std::move(items[k]), std::move(items[k + 1]));
                }
                push(std::move(d));
                break;
            }
            case 'l': push({.kind = Kind::List, .items = pop_mark()}); break;
            case 't': push({.kind = Kind::Tuple, .items = pop_mark()}); break;
            case 'a': {
                PickleValue v = pop();
                list_target().items.push_back(std::move(v));
                break;
            }
            case 'e': {
                auto items = pop_mark();
                auto& l = list_target();
                for (auto& v : items) {
                    l.items.push_back(std::move(v));
                }
                break;
            }
            case 's': {
                PickleValue v = pop();
                PickleValue k = pop();
                dict_target().dict.emplace_back(std::move(k), std::move(v));
                break;
            }
            case 'u': {
                auto items = pop_mark();
                if (items.size() % 2) {
                    fail("odd SETITEMS count");
                }
                auto& d = dict_target();
                for (std::size_t k = 0; k < items.size(); k += 2) {
                    d.dict.emplace_back(std::move(items[k]), std::move(items[k + 1]));
                }
                break;
            }
            case 'p': memo_[std::string(line())] = top(); break;
            case 'q': memo_[std::to_string(byte())] = top(); break;
            case 'r': memo_[std::to_string(u32())] = top(); break;
            case 'g': push(memo(std::string(line()))); break;
            case 'h': push(memo(std::to_string(byte()))); break;
            case 'j': push(memo(std::to_string(u32()))); break;
            case 'c': {
                std::string module(line());
                const std::string name(line());
                push({.kind = Kind::Global, .text = module + "." + name});
                break;
            }
            case 'i': {
                std::string module(line());
                const std::string name(line());
                push({.kind = Kind::Object, .text = module + "." + name, .items = pop_mark()});
                break;
            }
            case 'o': {
                auto items = pop_mark();
                if (items.empty()) {
                    fail("OBJ without a class");
                }
                PickleValue obj{.kind = Kind::Object, .text = items.front().text};
                obj.items.assign(std::make_move_iterator(items.begin() + 1), std::make_move_iterator(items.end()));
                push(std::move(obj));
                break;
            }
            case 'R': {
                PickleValue args = pop();
                PickleValue callable = pop();
                push({.kind = Kind::Object, .text = callable.text, .items = std::move(args.items)});
                break;
            }
            case 'b': {
                PickleValue state = pop();
                PickleValue& obj = top_ref();
                if (state.kind == Kind::Dict) {
                    obj.dict = std::move(state.dict);
                } else {
                    obj.dict.emplace_back(PickleValue{.kind = Kind::String, .text = "__state__"}, std::move(state));
                }
                break;
            }
            default:
                fail(std::string("unsupported opcode 0x") + "0123456789abcdef"[(op >> 4) & 15] +
                     "0123456789abcdef"[op & 15]);
            }
        }
    }

private:
    std::uint8_t byte() {
        if (pos_ >= d_.size()) {
            fail("unexpected end of data");
        }
        return static_cast<std::uint8_t>(d_[pos_++]);
    }
    std::uint16_t u16() {
        const auto lo = byte();
        return static_cast<std::uint16_t>(lo | (byte() << 8));
    }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            v |= std::uint32_t{byte()} << (8 * k);
        }
        return v;
    }
    std::string bytes(std::size_t n) {
        if (n > d_.size() - pos_) {
            fail("string runs past the end");
        }
        std::string s(reinterpret_cast<const char*>(d_.data() + pos_), n);
        pos_ += n;
        return s;
    }
    std::string_view line() {
        const auto* base = reinterpret_cast<const char*>(d_.data());
        const std::size_t start = pos_;
        while (pos_ < d_.size() && base[pos_] != '\n') {
            ++pos_;
        }
        if (pos_ >= d_.size()) {
            fail("unterminated line");
        }
        std::string_view l(base + start, pos_ - start);
        ++pos_;
        if (!l.empty() && l.back() == '\r') {
            l.remove_suffix(1);
        }
        return l;
    }
    static PickleValue int64(std::int64_t v) { return {.kind = Kind::Int, .integer = v}; }
    static PickleValue int_value(std::string_view s) {
        std::int64_t v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc() || r.ptr != s.data() + s.size()) {
            fail("bad or oversized integer");
        }
        return int64(v);
    }
    void push(PickleValue v) {
        if (stack_.size() >= kMaxStack) {
            fail("stack too deep");
        }
        stack_.push_back(std::move(v));
    }
    PickleValue pop() {
        if (stack_.empty() || stack_.back().kind == Kind::Mark) {
            fail("stack underflow");
        }
        PickleValue v = std::move(stack_.back());
        stack_.pop_back();
        return v;
    }
    PickleValue& top_ref() {
        if (stack_.empty() || stack_.back().kind == Kind::Mark) {
            fail("stack underflow");
        }
        return stack_.back();
    }
    const PickleValue& top() { return top_ref(); }
    std::vector<PickleValue> pop_mark() {
        std::size_t m = stack_.size();
        while (m > 0 && stack_[m - 1].kind != Kind::Mark) {
            --m;
        }
        if (m == 0) {
            fail("no mark");
        }
        if (stack_.size() - m > kMaxItems) {
            fail("too many items");
        }
        std::vector<PickleValue> items(std::make_move_iterator(stack_.begin() + static_cast<std::ptrdiff_t>(m)),
                                       std::make_move_iterator(stack_.end()));
        stack_.resize(m - 1);
        return items;
    }
    PickleValue& list_target() {
        PickleValue& l = top_ref();
        if (l.kind != Kind::List) {
            fail("append to a non-list");
        }
        return l;
    }
    PickleValue& dict_target() {
        PickleValue& d = top_ref();
        if (d.kind != Kind::Dict) {
            fail("setitem on a non-dict");
        }
        return d;
    }
    PickleValue memo(const std::string& key) {
        const auto it = memo_.find(key);
        if (it == memo_.end()) {
            fail("memo key " + key + " not found");
        }
        return it->second;
    }

    std::span<const std::byte> d_;
    std::size_t pos_ = 0;
    std::vector<PickleValue> stack_;
    std::map<std::string, PickleValue> memo_;
};

} // namespace

const PickleValue* PickleValue::get(std::string_view key) const {
    if (kind != Kind::Dict) {
        return nullptr;
    }
    for (const auto& [k, v] : dict) {
        if ((k.kind == Kind::String || k.kind == Kind::Unicode) && k.text == key) {
            return &v;
        }
    }
    return nullptr;
}

PickleValue parse_pickle(std::span<const std::byte> data) {
    return Reader(data).run();
}

} // namespace openbl::formats
