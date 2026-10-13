#include "openbl/formats/entity_io.hpp"

#include <charconv>
#include <vector>

namespace openbl::formats {

namespace {

template <typename T>
bool parse_number(std::string_view text, T& out) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        out = T{};
        return true;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc() && end == text.data() + text.size();
}

} // namespace

std::optional<EntityOutput> parse_entity_output(std::string_view value) {
    std::vector<std::string_view> fields;
    for (std::size_t start = 0;;) {
        const std::size_t comma = value.find(',', start);
        fields.push_back(value.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    if (fields.size() < 5) {
        return std::nullopt;
    }
    EntityOutput out;
    out.target = fields[0];
    out.input = fields[1];
    out.parameter = fields[2];
    if (!parse_number(fields[3], out.delay) || !parse_number(fields[4], out.times)) {
        return std::nullopt;
    }
    if (fields.size() == 6) {
        out.python = fields[5];
    } else if (fields.size() > 6) {
        // The Python statement is everything up to the empty last field; keep
        // any commas inside it.
        const std::size_t begin = static_cast<std::size_t>(fields[5].data() - value.data());
        const std::size_t end = static_cast<std::size_t>(fields.back().data() - value.data()) - 1;
        out.python = value.substr(begin, end - begin);
    }
    return out;
}

} // namespace openbl::formats
