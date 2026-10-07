#include "openbl/formats/save_state.hpp"

#include "binary_reader.hpp"

#include <algorithm>
#include <cstring>

namespace openbl::formats {
namespace {

constexpr std::int32_t kStateVersion = 117;
constexpr std::int32_t kMaxTokens = 1 << 20;
constexpr std::int32_t kMaxFields = 1 << 16;
constexpr int kMaxDepth = 16;

class Reader {
public:
    Reader(std::span<const std::byte> data, std::vector<std::string> tokens)
        : d_(data), tokens_(std::move(tokens)), r_(data, "SAVE STATE") {}

    detail::BinaryReader& r() { return r_; }

    // A struct: {u16 4, u16 name token, i32 count}, then `count` field records
    // {u16 size, u16 token, data}. Returns nullopt if the bytes at `at` are not
    // a well-formed struct inside [at, end).
    std::optional<std::pair<SaveStruct, std::size_t>> try_struct(std::size_t at, std::size_t end, int depth) const {
        if (depth > kMaxDepth || at + 8 > end) {
            return std::nullopt;
        }
        const auto size = load16(at);
        const auto token = load16(at + 2);
        if (size != 4 || !named(token)) {
            return std::nullopt;
        }
        std::int32_t count = 0;
        std::memcpy(&count, d_.data() + at + 4, 4);
        if (count < 0 || count > kMaxFields) {
            return std::nullopt;
        }
        SaveStruct s;
        s.name = tokens_[token];
        std::size_t p = at + 8;
        for (std::int32_t i = 0; i < count; ++i) {
            if (p + 4 > end) {
                return std::nullopt;
            }
            const auto fsize = load16(p);
            const auto ftoken = load16(p + 2);
            if (!named(ftoken) || p + 4 + fsize > end) {
                return std::nullopt;
            }
            SaveRecord field;
            field.name = tokens_[ftoken];
            // A value that is exactly a sequence of structs is kept as nested structs.
            auto nested = try_structs(p + 4, p + 4 + fsize, depth + 1);
            if (nested && fsize > 0) {
                field.nested = std::move(*nested);
            } else {
                field.data.assign(d_.begin() + static_cast<std::ptrdiff_t>(p + 4),
                                  d_.begin() + static_cast<std::ptrdiff_t>(p + 4 + fsize));
            }
            s.fields.push_back(std::move(field));
            p += 4 + fsize;
        }
        return std::make_pair(std::move(s), p);
    }

    // Structs filling [at, end) exactly, or nullopt.
    std::optional<std::vector<SaveStruct>> try_structs(std::size_t at, std::size_t end, int depth) const {
        std::vector<SaveStruct> out;
        while (at < end) {
            auto s = try_struct(at, end, depth);
            if (!s) {
                return std::nullopt;
            }
            out.push_back(std::move(s->first));
            at = s->second;
        }
        return out;
    }

    // Structs from `at` while they parse; stops at the first non-struct.
    std::vector<SaveStruct> structs_from(std::size_t& at, std::size_t end) const {
        std::vector<SaveStruct> out;
        while (auto s = try_struct(at, end, 0)) {
            out.push_back(std::move(s->first));
            at = s->second;
        }
        return out;
    }

    SaveStruct expect_struct(std::size_t& at, std::size_t end, const char* what) {
        auto s = try_struct(at, end, 0);
        if (!s) {
            r_.fail(std::string("expected ") + what + " at offset " + std::to_string(at));
        }
        at = s->second;
        return std::move(s->first);
    }

    std::int32_t i32_at(std::size_t at) {
        r_.seek(at);
        return r_.i32();
    }

private:
    std::uint16_t load16(std::size_t at) const { return detail::load_u16(d_.data() + at); }
    bool named(std::uint16_t token) const { return token < tokens_.size() && !tokens_[token].empty(); }

    std::span<const std::byte> d_;
    std::vector<std::string> tokens_;
    detail::BinaryReader r_;
};

} // namespace

std::optional<std::int32_t> SaveRecord::as_int() const {
    if (data.size() != 4) {
        return std::nullopt;
    }
    std::int32_t v = 0;
    std::memcpy(&v, data.data(), 4);
    return v;
}

std::optional<float> SaveRecord::as_float() const {
    if (data.size() != 4) {
        return std::nullopt;
    }
    float v = 0;
    std::memcpy(&v, data.data(), 4);
    return v;
}

std::optional<std::array<float, 3>> SaveRecord::as_vec3() const {
    if (data.size() != 12) {
        return std::nullopt;
    }
    std::array<float, 3> v{};
    std::memcpy(v.data(), data.data(), 12);
    return v;
}

std::string SaveRecord::as_string() const {
    std::string out;
    for (const std::byte b : data) {
        if (b == std::byte{0}) {
            break;
        }
        out.push_back(static_cast<char>(b));
    }
    return out;
}

const SaveRecord* SaveStruct::field(std::string_view field_name) const {
    for (const auto& f : fields) {
        if (f.name == field_name) {
            return &f;
        }
    }
    return nullptr;
}

const SaveRecord* SaveEntity::field(std::string_view field_name) const {
    for (const auto& s : data) {
        if (const auto* f = s.field(field_name)) {
            return f;
        }
    }
    return nullptr;
}

// Layout:
//   "VALV", i32 version, i32 token table size, i32 token count,
//   i32 header section size, i32 body section size
//   token table (zero-terminated strings; a hash table, mostly empty)
//   header section: i32, i32, i32 block count, then per block a {uv, elems}
//     struct pair wrapping SaveRestoreBlockHeader_t {szName, locHeader, locBody}.
//     Block headers sit at section + locHeader; the Entities header is an
//     i32 entity count followed by ETABLE structs {id, edictindex,
//     saveentityindex, location, size, flags, classname}.
//   body section: global structs (Save Header, ADJACENCY, LIGHTSTYLE...), then
//     entity data at body + location; other block bodies follow at
//     (start of entity data) + locBody.
SaveState parse_save_state(std::span<const std::byte> data) {
    detail::BinaryReader in(data, "SAVE STATE");
    const auto magic = in.bytes(4);
    if (magic[0] != std::byte{'V'} || magic[1] != std::byte{'A'} || magic[2] != std::byte{'L'} ||
        magic[3] != std::byte{'V'}) {
        in.fail("missing VALV signature");
    }
    SaveState state;
    state.version = in.i32();
    if (state.version != kStateVersion) {
        in.fail("unsupported version " + std::to_string(state.version));
    }
    const std::int32_t token_bytes = in.i32();
    const std::int32_t token_count = in.i32();
    const std::int32_t header_size = in.i32();
    const std::int32_t body_size = in.i32();
    if (token_bytes < 0 || token_count < 0 || token_count > kMaxTokens || header_size < 0 || body_size < 0 ||
        static_cast<std::uint64_t>(token_bytes) + static_cast<std::uint64_t>(header_size) +
                static_cast<std::uint64_t>(body_size) !=
            in.remaining()) {
        in.fail("section sizes do not match the file size");
    }
    std::vector<std::string> tokens;
    {
        std::string current;
        for (const std::byte b : in.bytes(static_cast<std::size_t>(token_bytes))) {
            if (b == std::byte{0}) {
                tokens.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(static_cast<char>(b));
            }
        }
        if (tokens.size() != static_cast<std::size_t>(token_count)) {
            in.fail("token table does not hold the declared number of tokens");
        }
    }

    state.tokens = tokens;
    Reader rd(data, std::move(tokens));
    const std::size_t header = 24 + static_cast<std::size_t>(token_bytes);
    const std::size_t header_end = header + static_cast<std::size_t>(header_size);
    const std::size_t body = header_end;
    const std::size_t end = data.size();

    // Block directory.
    const std::int32_t block_count = rd.i32_at(header + 8);
    if (block_count < 1 || block_count > 64) {
        rd.r().fail("implausible block count");
    }
    std::size_t at = header + 12;
    for (std::int32_t b = 0; b < block_count; ++b) {
        const SaveStruct wrapper = rd.expect_struct(at, header_end, "block header");
        const SaveRecord* elems = wrapper.field("elems");
        if (!elems || elems->nested.empty()) {
            rd.r().fail("block header without elements");
        }
        const SaveStruct& h = elems->nested.front();
        SaveBlock block;
        if (const auto* n = h.field("szName")) {
            block.name = n->as_string();
        }
        if (const auto* l = h.field("locHeader")) {
            block.header = l->as_int().value_or(-1);
        }
        if (const auto* l = h.field("locBody")) {
            block.body = l->as_int().value_or(-1);
        }
        state.blocks.push_back(std::move(block));
    }
    if (state.blocks.front().name != "Entities" || state.blocks.front().header < 0) {
        rd.r().fail("first block is not Entities");
    }

    // Entity table.
    std::size_t table = header + static_cast<std::size_t>(state.blocks.front().header);
    if (table + 4 > header_end) {
        rd.r().fail("entity table out of range");
    }
    const std::int32_t entity_count = rd.i32_at(table);
    if (entity_count < 0 || entity_count > kMaxFields) {
        rd.r().fail("implausible entity count");
    }
    table += 4;
    std::int32_t first_location = -1;
    for (std::int32_t e = 0; e < entity_count; ++e) {
        const SaveStruct row = rd.expect_struct(table, header_end, "ETABLE");
        SaveEntity entity;
        auto get = [&](const char* name, std::int32_t fallback) {
            const auto* f = row.field(name);
            return f ? f->as_int().value_or(fallback) : fallback;
        };
        entity.id = get("id", -1);
        entity.edict = get("edictindex", -1);
        entity.save_index = get("saveentityindex", -1);
        entity.location = get("location", 0); // Omitted when 0 (fields at their defaults are not saved).
        entity.size = get("size", 0);
        if (const auto* c = row.field("classname")) {
            entity.classname = c->as_string();
        }
        if (entity.size > 0) {
            if (entity.location < 0 ||
                static_cast<std::uint64_t>(entity.location) + static_cast<std::uint64_t>(entity.size) >
                    static_cast<std::uint64_t>(body_size)) {
                rd.r().fail("entity data out of range");
            }
            const std::size_t from = body + static_cast<std::size_t>(entity.location);
            // Structs, then (for a few classes: NPCs 1 byte, the player 29)
            // data written outside the struct system.
            const std::size_t to = from + static_cast<std::size_t>(entity.size);
            std::size_t p = from;
            entity.data = rd.structs_from(p, to);
            entity.trailing.assign(data.begin() + static_cast<std::ptrdiff_t>(p), data.begin() + static_cast<std::ptrdiff_t>(to));
            first_location = first_location < 0 ? entity.location : std::min(first_location, entity.location);
        }
        state.entities.push_back(std::move(entity));
    }

    // Global structs fill the body up to the first entity's data.
    const std::size_t entity_data = body + static_cast<std::size_t>(std::max(first_location, 0));
    {
        std::size_t p = body;
        state.globals = rd.structs_from(p, entity_data);
    }

    // Other blocks' bodies, relative to the start of the entity data.
    std::vector<std::pair<std::size_t, const SaveBlock*>> bodies;
    for (const auto& b : state.blocks) {
        if (&b != &state.blocks.front() && b.body >= 0) {
            bodies.emplace_back(entity_data + static_cast<std::size_t>(b.body), &b);
        }
    }
    std::sort(bodies.begin(), bodies.end());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const std::size_t from = bodies[i].first;
        const std::size_t to = i + 1 < bodies.size() ? bodies[i + 1].first : end;
        if (from > to || to > end) {
            rd.r().fail("block body out of range");
        }
        const std::string& name = bodies[i].second->name;
        if (name == "EventQueue" || name == "Physics") {
            std::size_t p = from;
            auto structs = rd.structs_from(p, to);
            (name == "EventQueue" ? state.event_queue : state.physics) = std::move(structs);
        } else if (name == "AI") {
            state.ai.assign(data.begin() + static_cast<std::ptrdiff_t>(from), data.begin() + static_cast<std::ptrdiff_t>(to));
        } else if (name == "Python") {
            // {u8 1, i32 length, pickle} entries.
            std::size_t p = from;
            while (p + 5 <= to) {
                if (data[p] != std::byte{1}) {
                    rd.r().fail("unexpected entry in the Python block");
                }
                const std::int32_t n = rd.i32_at(p + 1);
                if (n < 0 || p + 5 + static_cast<std::size_t>(n) > to) {
                    rd.r().fail("Python block entry out of range");
                }
                state.python_pickles.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(p + 5),
                                                  data.begin() + static_cast<std::ptrdiff_t>(p + 5 + static_cast<std::size_t>(n)));
                p += 5 + static_cast<std::size_t>(n);
            }
        }
    }
    return state;
}

ClientSaveState parse_client_state(std::span<const std::byte> data) {
    detail::BinaryReader in(data, "CLIENT SAVE STATE");
    const auto magic = in.bytes(4);
    if (magic[0] != std::byte{'V'} || magic[1] != std::byte{'A'} || magic[2] != std::byte{'L'} ||
        magic[3] != std::byte{'V'}) {
        in.fail("missing VALV signature");
    }
    ClientSaveState out;
    const std::int32_t version = in.i32();
    const std::int32_t entity_bytes = in.i32(); // Client entity data.
    const std::int32_t header_size = in.i32();  // Header section (block directory, entity table).
    const std::int32_t decal_bytes = in.i32();
    const std::int32_t token_bytes = in.i32();
    out.decal_count = in.i32();
    const std::int32_t token_count = in.i32();
    if (version != kStateVersion) {
        in.fail("unsupported version " + std::to_string(version));
    }
    if (header_size < 0 || entity_bytes < 0 || decal_bytes < 0 || token_bytes < 0 || out.decal_count < 0 ||
        static_cast<std::uint64_t>(token_bytes) + static_cast<std::uint64_t>(header_size) +
                static_cast<std::uint64_t>(entity_bytes) + static_cast<std::uint64_t>(decal_bytes) >
            in.remaining()) {
        in.fail("section sizes do not match the file size");
    }
    // After its longer header the layout is that of a .HL1 file, so rebuild an
    // .HL1 header around it; the decals and the closing string count as body.
    const std::size_t rest = in.remaining() - static_cast<std::size_t>(token_bytes);
    const std::int32_t hl1_header = header_size;
    const auto hl1_body = static_cast<std::int32_t>(rest - static_cast<std::size_t>(hl1_header));
    std::vector<std::byte> hl1(24);
    std::memcpy(hl1.data(), data.data(), 8); // "VALV", version.
    const std::int32_t fields[] = {token_bytes, token_count, hl1_header, hl1_body};
    std::memcpy(hl1.data() + 8, fields, sizeof(fields));
    hl1.insert(hl1.end(), data.begin() + 32, data.end());

    out.state = parse_save_state(hl1);

    std::vector<std::string> tokens;
    {
        std::string current;
        for (std::size_t i = 24; i < 24 + static_cast<std::size_t>(token_bytes); ++i) {
            if (hl1[i] == std::byte{0}) {
                tokens.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(static_cast<char>(hl1[i]));
            }
        }
    }
    Reader rd(hl1, std::move(tokens));
    const std::size_t decals = 24 + static_cast<std::size_t>(token_bytes) + static_cast<std::size_t>(hl1_header) +
                               static_cast<std::size_t>(entity_bytes);
    const std::size_t decals_end = decals + static_cast<std::size_t>(decal_bytes);
    std::size_t p = decals;
    out.decals = rd.structs_from(p, decals_end);
    if (p != decals_end) {
        rd.r().fail("decal section is not a sequence of structs");
    }
    out.trailing.assign(hl1.begin() + static_cast<std::ptrdiff_t>(decals_end), hl1.end());
    return out;
}

std::vector<std::int32_t> parse_transition_list(std::span<const std::byte> data) {
    detail::BinaryReader in(data, "TRANSITION LIST");
    const std::int32_t n = in.i32();
    if (n < 0 || static_cast<std::uint64_t>(n) * 4 != in.remaining()) {
        in.fail("count does not match the file size");
    }
    std::vector<std::int32_t> out;
    for (std::int32_t i = 0; i < n; ++i) {
        out.push_back(in.i32());
    }
    return out;
}

} // namespace openbl::formats
