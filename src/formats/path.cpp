#include "openbl/formats/path.hpp"

namespace openbl::formats {

std::string normalize_game_path(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') {
            c = '/';
        } else if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c == '/' && (out.empty() || out.back() == '/')) {
            continue;
        }
        out.push_back(c);
    }
    while (out.starts_with("./")) {
        out.erase(0, 2);
    }
    if (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

bool is_safe_relative_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string_view::npos) {
        return false;
    }
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        if (path.substr(start, end - start) == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

} // namespace openbl::formats
