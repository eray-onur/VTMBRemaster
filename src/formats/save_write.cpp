#include "openbl/formats/save_write.hpp"

#include "openbl/formats/error.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace openbl::formats {
namespace {

constexpr std::int32_t kSaveVersion = 117;
constexpr std::size_t kNameSize = 260;
constexpr std::size_t kChunkSize = 512 * 1024;

void put_u16(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>(v & 0xFF));
    out.push_back(static_cast<std::byte>(v >> 8));
}

void put_i32(std::vector<std::byte>& out, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) {
        out.push_back(static_cast<std::byte>((u >> (8 * k)) & 0xFF));
    }
}

void put_record(std::vector<std::byte>& out, std::uint16_t token, const std::vector<std::byte>& data) {
    if (data.size() > 0xFFFF) {
        throw FormatError("SAVE WRITE: record larger than 65,535 bytes");
    }
    put_u16(out, static_cast<std::uint16_t>(data.size()));
    put_u16(out, token);
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<std::byte> deflate_chunk(std::span<const std::byte> in) {
    uLongf size = compressBound(static_cast<uLong>(in.size()));
    std::vector<std::byte> out(size);
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(in.data()),
                  static_cast<uLong>(in.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw FormatError("SAVE WRITE: zlib compression failed");
    }
    out.resize(size);
    return out;
}

} // namespace

std::uint32_t save_name_hash(std::string_view name) {
    std::uint32_t h = 0;
    for (const char c : name) {
        h = ((h >> 4) | (h << 28)) ^ static_cast<unsigned char>(c);
    }
    return h;
}

SaveTokenTable::SaveTokenTable() : slots_(kSlots) {}

SaveTokenTable::SaveTokenTable(std::vector<std::string> slots) : slots_(std::move(slots)) {
    if (slots_.empty() || slots_.size() > 0xFFFF) {
        throw FormatError("SAVE WRITE: name table size out of range");
    }
}

std::uint16_t SaveTokenTable::slot(std::string_view name) {
    if (name.empty()) {
        throw FormatError("SAVE WRITE: empty name");
    }
    const std::size_t n = slots_.size();
    std::size_t i = save_name_hash(name) % n;
    for (std::size_t probe = 0; probe < n; ++probe, i = (i + 1) % n) {
        if (slots_[i].empty()) {
            slots_[i] = name;
            return static_cast<std::uint16_t>(i);
        }
        if (slots_[i] == name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    throw FormatError("SAVE WRITE: name table full");
}

std::vector<std::byte> SaveTokenTable::bytes() const {
    std::vector<std::byte> out;
    for (const auto& s : slots_) {
        for (const char c : s) {
            out.push_back(static_cast<std::byte>(c));
        }
        out.push_back(std::byte{0});
    }
    return out;
}

std::vector<std::byte> encode_save_structs(const std::vector<SaveStruct>& structs, SaveTokenTable& tokens) {
    std::vector<std::byte> out;
    for (const SaveStruct& s : structs) {
        std::vector<std::byte> count;
        put_i32(count, static_cast<std::int32_t>(s.fields.size()));
        put_record(out, tokens.slot(s.name), count);
        for (const SaveRecord& f : s.fields) {
            put_record(out, tokens.slot(f.name), f.nested.empty() ? f.data : encode_save_structs(f.nested, tokens));
        }
    }
    return out;
}

std::vector<std::byte> write_sav(const SaveFile& save) {
    SaveTokenTable tokens;
    std::vector<std::byte> header;
    for (const SaveField& f : save.header) {
        put_record(header, tokens.slot(f.name), f.data);
    }
    const auto table = tokens.bytes();

    std::vector<std::byte> out;
    for (const char c : {'J', 'S', 'A', 'V'}) {
        out.push_back(static_cast<std::byte>(c));
    }
    put_i32(out, save.version ? save.version : kSaveVersion);
    put_i32(out, static_cast<std::int32_t>(header.size()));
    put_i32(out, static_cast<std::int32_t>(tokens.slots().size()));
    put_i32(out, static_cast<std::int32_t>(table.size()));
    out.insert(out.end(), table.begin(), table.end());
    out.insert(out.end(), header.begin(), header.end());

    for (const SaveEmbeddedFile& file : save.files) {
        if (file.name.size() >= kNameSize) {
            throw FormatError("SAVE WRITE: embedded file name too long");
        }
        std::vector<std::byte> name(kNameSize);
        std::memcpy(name.data(), file.name.data(), file.name.size());
        out.insert(out.end(), name.begin(), name.end());
        put_i32(out, static_cast<std::int32_t>(file.data.size()));
        if (file.data.empty()) {
            const auto chunk = deflate_chunk({});
            put_i32(out, static_cast<std::int32_t>(chunk.size()));
            out.insert(out.end(), chunk.begin(), chunk.end());
            continue;
        }
        // The first chunk's size sits in the entry header; later ones precede their chunk.
        for (std::size_t done = 0; done < file.data.size(); done += kChunkSize) {
            const std::size_t n = std::min(kChunkSize, file.data.size() - done);
            const auto chunk = deflate_chunk(std::span(file.data).subspan(done, n));
            put_i32(out, static_cast<std::int32_t>(chunk.size()));
            out.insert(out.end(), chunk.begin(), chunk.end());
        }
    }
    return out;
}

} // namespace openbl::formats
