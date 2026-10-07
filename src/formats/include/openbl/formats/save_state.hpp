#pragma once

// Per-map state files inside a save (<map>.HL1, "VALV" version 117). The data
// is self-describing: every value is a record named by a token, grouped into
// structs headed by a {struct name, field count} record. See
// docs/formats/saves.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::formats {

struct SaveStruct;

struct SaveRecord {
    std::string name;            // e.g. "m_iHealth", "m_iVAttributesBase[ v_attribute_clan ]".
    std::vector<std::byte> data; // Raw value; empty for nested structs.
    std::vector<SaveStruct> nested; // When the value is itself a sequence of structs.

    std::optional<std::int32_t> as_int() const;
    std::optional<float> as_float() const;
    std::optional<std::array<float, 3>> as_vec3() const;
    std::string as_string() const; // Up to the first zero byte.
};

struct SaveStruct {
    std::string name; // Class or type name, e.g. "CBaseEntity", "CBasePlayer", "LIGHTSTYLE".
    std::vector<SaveRecord> fields; // Only fields that differ from their defaults are saved.

    const SaveRecord* field(std::string_view name) const;
};

struct SaveBlock {
    std::string name; // Entities, EventQueue, Physics, AI, Python.
    std::int32_t header = -1;
    std::int32_t body = -1;
};

struct SaveEntity {
    std::string classname; // Empty for free slots.
    std::int32_t id = -1, edict = -1, save_index = -1;
    std::int32_t location = -1, size = 0;
    std::vector<SaveStruct> data; // One struct per class in the hierarchy, base first.
    std::vector<std::byte> trailing; // Class-specific bytes after the last struct (NPCs, the player).

    // First field with this name in any of the entity's structs.
    const SaveRecord* field(std::string_view name) const;
};

struct SaveState {
    std::int32_t version = 0;
    std::vector<std::string> tokens; // The file's name table (hash slots; unused ones empty).
    std::vector<SaveBlock> blocks;
    std::vector<SaveStruct> globals;     // "Save Header", "ADJACENCY", "LIGHTSTYLE", ...
    std::vector<SaveEntity> entities;
    std::vector<SaveStruct> event_queue; // "EventQueue", "PEvent", ...
    std::vector<SaveStruct> physics;     // "PhysObjectHeader_t", ...
    std::vector<std::byte> ai;           // Raw AI block body.
    // Python block: protocol-0 pickles. The first holds the scripts' global
    // variables (G.*); the second is a smaller per-map dictionary.
    std::vector<std::vector<std::byte>> python_pickles;
};

// Throws FormatError on malformed data.
SaveState parse_save_state(std::span<const std::byte> data);

// Client-side state (<map>.HL2): the same record format as .HL1 with a longer
// header, holding client entities (C_BaseEntity) and the decals placed in the
// level (DECALLIST structs: position, flags, name, entityIndex).
struct ClientSaveState {
    SaveState state;                 // Blocks, entity table and client entities.
    std::int32_t decal_count = 0;    // As declared in the header.
    std::vector<SaveStruct> decals;
    std::vector<std::byte> trailing; // A length-prefixed string after the decals.
};
// Throws FormatError on malformed data.
ClientSaveState parse_client_state(std::span<const std::byte> data);

// Transition list (<map>.HL3): indices into the .HL1 entity table of the
// entities that travel with the player through a level change (the player and
// carried items). Throws FormatError on malformed data.
std::vector<std::int32_t> parse_transition_list(std::span<const std::byte> data);

} // namespace openbl::formats
