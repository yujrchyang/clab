#ifndef COMMON_INTARITH_H
#define COMMON_INTARITH_H

#include <bit>
#include <concepts>
#include <type_traits>

#include "common_fwd.h"

namespace TOPNSPC {

// ==========================================
// Basic Mathematical Rounding Functions
// ==========================================

template <typename T, typename U>
constexpr inline std::make_unsigned_t<std::common_type_t<T, U>> div_round_up(
    T n, U d) {
    return n == 0 ? 0
                  : 1 + (static_cast<std::make_unsigned_t<std::common_type_t<T, U>>>(n) - 1) / d;
}

template <typename T, typename U>
constexpr inline std::make_unsigned_t<std::common_type_t<T, U>> round_down_to(
    T n, U d) {
    return n - n % d;
}

template <typename T, typename U>
constexpr inline std::make_unsigned_t<std::common_type_t<T, U>> round_up_to(
    T n, U d) {
    auto rem = n % d;
    return rem ? (n + (d - rem)) : n;
}

template <typename T, typename U>
constexpr inline std::make_unsigned_t<std::common_type_t<T, U>> shift_round_up(
    T x, U y) {
    using CommonType = std::make_unsigned_t<std::common_type_t<T, U>>;
    return (static_cast<CommonType>(x) + (static_cast<CommonType>(1) << y) -
            1) >>
        y;
}

// ==========================================
// Power-of-2 Alignment Functions
// ==========================================

template <typename T>
constexpr inline bool isp2(T x) {
    return x > 0 && (x & (x - 1)) == 0;
}

template <typename T>
constexpr inline T p2align(T x, T align) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<U>(x) & -static_cast<U>(align));
}

template <typename T>
constexpr inline T p2phase(T x, T align) {
    return x & (align - 1);
}

template <typename T>
constexpr inline T p2nphase(T x, T align) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(-static_cast<U>(x) & (static_cast<U>(align) - 1));
}

template <typename T>
constexpr inline T p2roundup(T x, T align) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>(-(-static_cast<U>(x) & -static_cast<U>(align)));
}

// ==========================================
// Bitwise Manipulation Functions
// ==========================================

template <std::integral T>
constexpr inline unsigned ctz(T v) {
    return static_cast<unsigned>(
        std::countr_zero(static_cast<std::make_unsigned_t<T>>(v)));
}

template <std::integral T>
constexpr inline unsigned clz(T v) {
    return static_cast<unsigned>(
        std::countl_zero(static_cast<std::make_unsigned_t<T>>(v)));
}

template <std::integral T>
constexpr inline unsigned cbits(T v) {
    if (v == 0)
        return 0;
    return static_cast<unsigned>(std::bit_width(
        static_cast<std::make_unsigned_t<T>>(v)));
}

template <std::integral T>
constexpr inline unsigned popcount(T v) {
    return static_cast<unsigned>(
        std::popcount(static_cast<std::make_unsigned_t<T>>(v)));
}

}  // namespace TOPNSPC

#endif  // COMMON_INTARITH_H
