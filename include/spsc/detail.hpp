#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace spsc::detail {

// C++17 has no std::bit_ceil (C++20 <bit>), so roll our own with the same name/semantics.
// Smear the highest set bit of n-1 into every lower bit, then add 1
// -> smallest power of two >= n.
//   e.g. n=5: 4=0b100 -> 0b111 -> 8
constexpr std::size_t bit_ceil(std::size_t n) noexcept {
    if (n <= 1) {
        return 1;
    }
    --n;
    for (std::size_t shift = 1; shift < std::numeric_limits<std::size_t>::digits; shift <<= 1) {
        n |= n >> shift;
    }
    return n + 1;  // overflows to 0 if n > 2^(digits-1); callers must guard
}

static_assert(bit_ceil(1) == 1);
static_assert(bit_ceil(2) == 2);
static_assert(bit_ceil(3) == 4);
static_assert(bit_ceil(5) == 8);
static_assert(bit_ceil(1000) == 1024);
static_assert(bit_ceil(1024) == 1024);

// Shared capacity validation + rounding used by the queue constructors.
inline std::size_t checked_capacity(std::size_t requested) {
    constexpr std::size_t kMax = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
    if (requested == 0) {
        throw std::invalid_argument("spsc: capacity must be > 0");
    }
    if (requested > kMax) {
        throw std::length_error("spsc: capacity too large");
    }
    return bit_ceil(requested);
}

}  // namespace spsc::detail
