#include "openbl/formats/sav.hpp"

#include "binary_reader.hpp"

#include <zlib.h>

#include <cstring>

namespace openbl::formats {
namespace {

constexpr std::int32_t kSaveVersion = 117;
constexpr std::size_t kNameSize = 260;
constexpr std::size_t kChunkSize = 512 * 1024;      // Uncompressed size of every chunk but the last.
constexpr std::size_t kMaxFileSize = 256u << 20;     // Sanity limit for one embedded file.
constexpr std::int32_t kMaxTokens = 1 << 20;

// Decompresses one zlib stream that must produce exactly `expected` bytes.
void inflate_into(std::span<const std::byte> in, std::byte* out, std::size_t expected, detail::BinaryReader& r) {
    z_stream z{};
    if (inflateInit(&z) != Z_OK) {
        r.fail("zlib initialisation failed");
    }
    z.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    z.next_out = reinterpret_cast<Bytef*>(out);
    z.avail_out = static_cast<uInt>(expected);
    const int result = inflate(&z, Z_FINISH);
    const std::size_t produced = expected - z.avail_out;
    inflateEnd(&z);
    if (result != Z_STREAM_END || produced != expected) {
        r.fail("corrupt compressed data");
    }
}

} // namespace

std::optional<std::int32_t> SaveField::as_int() const {
    if (data.size() != 4) {
        return std::nullopt;
    }
    std::int32_t v = 0;
    std::memcpy(&v, data.data(), 4);
    return v;
}

std::string SaveField::as_string() const {
    std::string out;
    for (const std::byte b : data) {
        if (b == std::byte{0}) {
            break;
        }
        out.push_back(static_cast<char>(b));
    }
    return out;
}

const SaveField* SaveFile::field(std::string_view name) const {
    for (const auto& f : header) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

// Layout:
//   "JSAV", i32 version, i32 header block size, i32 token count, i32 token table size
//   token table: token-count zero-terminated strings (a hash table; unused slots are empty)
//   header block: records {u16 size, u16 token} + size bytes
//   embedded files until the end: char name[260], i32 size, i32 first chunk's
//   compressed size, the first zlib chunk, then {i32 compressed size, chunk}
//   for each further 512 KiB of data.
SaveFile parse_sav(std::span<const std::byte> data) {
    detail::BinaryReader r(data, "SAV");
    const auto magic = r.bytes(4);
    if (magic[0] != std::byte{'J'} || magic[1] != std::byte{'S'} || magic[2] != std::byte{'A'} ||
        magic[3] != std::byte{'V'}) {
        r.fail("missing JSAV signature");
    }
    SaveFile save;
    save.version = r.i32();
    if (save.version != kSaveVersion) {
        r.fail("unsupported version " + std::to_string(save.version));
    }
    const std::int32_t header_size = r.i32();
    const std::int32_t token_count = r.i32();
    const std::int32_t token_bytes = r.i32();
    if (header_size < 0 || token_count < 0 || token_count > kMaxTokens || token_bytes < 0 ||
        static_cast<std::uint64_t>(token_bytes) + static_cast<std::uint64_t>(header_size) > r.remaining()) {
        r.fail("header sizes out of range");
    }

    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(token_count));
    {
        const auto table = r.bytes(static_cast<std::size_t>(token_bytes));
        std::string current;
        for (const std::byte b : table) {
            if (b == std::byte{0}) {
                tokens.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(static_cast<char>(b));
            }
        }
        if (tokens.size() != static_cast<std::size_t>(token_count)) {
            r.fail("token table does not hold the declared number of tokens");
        }
    }

    const std::size_t header_end = r.pos() + static_cast<std::size_t>(header_size);
    while (r.pos() < header_end) {
        const std::uint16_t size = r.u16();
        const std::uint16_t token = r.u16();
        if (token >= tokens.size() || r.pos() + size > header_end) {
            r.fail("header field out of range");
        }
        const auto bytes = r.bytes(size);
        save.header.push_back({tokens[token], {bytes.begin(), bytes.end()}});
    }

    while (r.remaining() > 0) {
        const auto raw_name = r.bytes(kNameSize);
        SaveEmbeddedFile file;
        for (const std::byte b : raw_name) {
            if (b == std::byte{0}) {
                break;
            }
            file.name.push_back(static_cast<char>(b));
        }
        const std::int32_t size = r.i32();
        std::int32_t compressed = r.i32();
        if (size < 0 || static_cast<std::size_t>(size) > kMaxFileSize) {
            r.fail("embedded file size out of range");
        }
        file.data.resize(static_cast<std::size_t>(size));
        if (size == 0) {
            if (compressed < 0) {
                r.fail("negative chunk size");
            }
            r.bytes(static_cast<std::size_t>(compressed));
        }
        for (std::size_t done = 0; done < file.data.size();) {
            if (compressed < 0) {
                r.fail("negative chunk size");
            }
            const std::size_t chunk = std::min(kChunkSize, file.data.size() - done);
            inflate_into(r.bytes(static_cast<std::size_t>(compressed)), file.data.data() + done, chunk, r);
            done += chunk;
            if (done >= file.data.size()) {
                break;
            }
            compressed = r.i32();
        }
        save.files.push_back(std::move(file));
    }
    return save;
}

} // namespace openbl::formats
