#pragma once

// Internal: float -> IEEE 754 half conversion (round toward zero, clamped).

#include <algorithm>
#include <bit>
#include <cstdint>

namespace openbl::render::detail {

inline std::uint16_t to_half(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(std::min(f, 65504.0f));
    const std::uint32_t sign = (bits >> 16) & 0x8000;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFF;
    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa = (mantissa | 0x800000) >> (1 - exponent);
        return static_cast<std::uint16_t>(sign | (mantissa >> 13));
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7BFF);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
}

} // namespace openbl::render::detail
