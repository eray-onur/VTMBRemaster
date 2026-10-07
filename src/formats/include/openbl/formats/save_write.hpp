#pragma once

// Writing save files in the original's format. See docs/formats/saves.md.
//
// Names in saves live in a 16,383-slot hash table: a name goes to slot
// hash(name) % 16383, or the next free slot after it. Records refer to names
// by slot index, so a writer must reproduce the table, not just the strings.

#include "openbl/formats/sav.hpp"
#include "openbl/formats/save_state.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::formats {

// The name hash: rotate the 32-bit value right by 4 and XOR in each byte.
std::uint32_t save_name_hash(std::string_view name);

class SaveTokenTable {
public:
    static constexpr std::size_t kSlots = 16383;

    SaveTokenTable();
    // Wraps an existing table (e.g. SaveState::tokens) to reuse its slots.
    explicit SaveTokenTable(std::vector<std::string> slots);

    // Slot of `name`, inserting it (with linear probing) if it is new.
    // Throws FormatError when the table is full.
    std::uint16_t slot(std::string_view name);

    const std::vector<std::string>& slots() const { return slots_; }
    // The table as written in files: every slot's string, zero-terminated.
    std::vector<std::byte> bytes() const;

private:
    std::vector<std::string> slots_;
};

// Encodes structs as records: {u16 4, u16 name, i32 field count} then each
// field {u16 size, u16 name, data}; a field with nested structs is written as
// their encoding. The inverse of how parse_save_state reads struct sequences.
std::vector<std::byte> encode_save_structs(const std::vector<SaveStruct>& structs, SaveTokenTable& tokens);

// Writes a .sav container: header fields, then each embedded file compressed
// in 512 KiB zlib chunks. parse_sav reads the result back unchanged.
std::vector<std::byte> write_sav(const SaveFile& save);

} // namespace openbl::formats
