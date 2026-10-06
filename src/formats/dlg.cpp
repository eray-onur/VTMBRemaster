#include "openbl/formats/dlg.hpp"

#include <algorithm>
#include <charconv>

namespace openbl::formats {
namespace {

constexpr std::size_t kColumns = 13;

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

// Some files were exported through a spreadsheet, which wrapped fields
// containing quotes or line breaks in quotes and doubled the inner quotes.
// Only undo that when the field shows those signs, so that ordinary text
// that happens to start and end with a quote is left alone.
std::string clean_field(std::string_view raw) {
    std::string_view s = trim(raw);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        const std::string_view inner = s.substr(1, s.size() - 2);
        const bool spreadsheet = inner.find("\"\"") != std::string_view::npos ||
                                 inner.find('\n') != std::string_view::npos;
        if (spreadsheet) {
            std::string out;
            for (std::size_t i = 0; i < inner.size(); ++i) {
                out += inner[i];
                if (inner[i] == '"' && i + 1 < inner.size() && inner[i + 1] == '"') {
                    ++i;
                }
            }
            return std::string(trim(out));
        }
    }
    return std::string(s);
}

std::optional<int> to_int(std::string_view s) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size() || s.empty()) {
        return std::nullopt;
    }
    return v;
}

} // namespace

bool DlgLine::is_placeholder() const {
    return male_text.empty() && female_text.empty() && next.empty() && condition.empty() && action.empty() &&
           malkavian_text.empty();
}

std::optional<int> DlgLine::number() const {
    return to_int(index);
}

std::optional<int> DlgLine::next_number() const {
    return to_int(next);
}

DlgFile parse_dlg(std::string_view text) {
    DlgFile file;
    std::size_t pos = 0;
    std::size_t line = 1;
    auto advance_to = [&](std::size_t to) {
        for (; pos < to; ++pos) {
            line += text[pos] == '\n';
        }
    };

    while (pos < text.size()) {
        // Skip to the next record.
        const std::size_t open = text.find('{', pos);
        const std::string_view gap = text.substr(pos, open == std::string_view::npos ? text.npos : open - pos);
        if (const std::string_view stray = trim(gap); !stray.empty()) {
            advance_to(static_cast<std::size_t>(stray.data() - text.data()));
            file.diagnostics.push_back({line, "text outside a record ignored"});
        }
        if (open == std::string_view::npos) {
            break;
        }
        advance_to(open);

        // A record is a run of {fields} separated only by spaces and tabs.
        const std::size_t record_line = line;
        std::vector<std::string> fields;
        bool unterminated = false;
        while (pos < text.size() && text[pos] == '{') {
            const std::size_t close = text.find_first_of("{}", pos + 1);
            if (close == std::string_view::npos) {
                unterminated = true;
                advance_to(text.size());
                break;
            }
            const std::string_view raw = text.substr(pos + 1, close - pos - 1);
            fields.push_back(clean_field(raw));
            if (text[close] == '{') {
                // Missing '}' (two shipped files): the next '{' closes the field.
                // If the field ran past a line break, the record ended there too.
                file.diagnostics.push_back({line, "missing '}'"});
                advance_to(close);
                if (raw.find('\n') != std::string_view::npos) {
                    break;
                }
                continue;
            }
            advance_to(close + 1);
            std::size_t next = pos;
            while (next < text.size() && (text[next] == ' ' || text[next] == '\t')) {
                ++next;
            }
            if (next < text.size() && text[next] == '{') {
                advance_to(next);
            }
        }
        if (unterminated) {
            file.diagnostics.push_back({record_line, "unterminated field; rest of file ignored"});
            if (fields.empty()) {
                break;
            }
        }
        // 12 fields: the Malkavian column was left off.
        if (fields.size() != kColumns && fields.size() != kColumns - 1) {
            file.diagnostics.push_back(
                {record_line, "record has " + std::to_string(fields.size()) + " fields, expected 13"});
        }
        fields.resize(std::max(fields.size(), kColumns));

        DlgLine l;
        l.source_line = record_line;
        l.index = std::move(fields[0]);
        l.male_text = std::move(fields[1]);
        l.female_text = std::move(fields[2]);
        l.next = std::move(fields[3]);
        l.condition = std::move(fields[4]);
        l.action = std::move(fields[5]);
        for (std::size_t i = 0; i < l.unused.size(); ++i) {
            l.unused[i] = std::move(fields[6 + i]);
        }
        l.malkavian_text = std::move(fields[12]);
        if (l.index != std::to_string(file.lines.size() + 1)) {
            file.diagnostics.push_back({record_line, "index '" + l.index + "' does not match the record's position " +
                                                         std::to_string(file.lines.size() + 1)});
        }
        file.lines.push_back(std::move(l));
    }
    return file;
}

} // namespace openbl::formats
