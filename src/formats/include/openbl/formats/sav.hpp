#pragma once

// Save games (.sav, "JSAV" version 117): a header of named fields, then one
// compressed state file per visited map. See docs/formats/saves.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::formats {

struct SaveField {
    std::string name;              // From the save's token table, e.g. "mapName".
    std::vector<std::byte> data;

    std::optional<std::int32_t> as_int() const;
    std::string as_string() const; // Up to the first zero byte.
};

// A file stored inside the save: <map>.HL1 (entity state), .HL2 (more state),
// .HL3 (a short table). Decompressed.
struct SaveEmbeddedFile {
    std::string name;
    std::vector<std::byte> data;
};

struct SaveFile {
    std::int32_t version = 0; // 117.
    std::vector<SaveField> header; // In file order: GameHeader, mapCount, mapName, comment, userName, GLOBAL.
    std::vector<SaveEmbeddedFile> files;

    const SaveField* field(std::string_view name) const;
};

// Throws FormatError on malformed data.
SaveFile parse_sav(std::span<const std::byte> data);

} // namespace openbl::formats
