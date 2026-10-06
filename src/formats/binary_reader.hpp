#pragma once

// Internal helper: bounds-checked little-endian reads from a byte buffer.

#include "openbl/formats/error.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace openbl::formats::detail {

inline std::uint16_t load_u16(const std::byte* p) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8));
}

inline std::uint32_t load_u32(const std::byte* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

class BinaryReader {
public:
    // `what` names the data in error messages, e.g. a file path.
    BinaryReader(std::span<const std::byte> data, std::string what) : data_(data), what_(std::move(what)) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return data_.size(); }
    std::size_t remaining() const { return data_.size() - pos_; }

    void seek(std::size_t pos) {
        if (pos > data_.size()) {
            fail("seek past end of data");
        }
        pos_ = pos;
    }

    std::uint8_t u8() { return static_cast<std::uint8_t>(bytes(1)[0]); }
    std::uint16_t u16() { return load_u16(bytes(2).data()); }
    std::uint32_t u32() { return load_u32(bytes(4).data()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() { return std::bit_cast<float>(u32()); }

    std::span<const std::byte> bytes(std::size_t n) {
        if (n > remaining()) {
            fail("unexpected end of data at offset " + std::to_string(pos_));
        }
        auto out = data_.subspan(pos_, n);
        pos_ += n;
        return out;
    }

    [[noreturn]] void fail(const std::string& message) const { throw FormatError(what_ + ": " + message); }

private:
    std::span<const std::byte> data_;
    std::string what_;
    std::size_t pos_ = 0;
};

} // namespace openbl::formats::detail
