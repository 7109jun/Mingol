// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <type_traits>

// Enables bitwise operators for an `enum class` flag type. Invoke at namespace scope, right after
// the enum, in the enum's own namespace:   MINGOL_ENUM_FLAGS(BufferUsage)
//   a | b, a & b, a |= b, hasFlag(value, flag) (all bits of `flag` set), anyFlag(value)
#define MINGOL_ENUM_FLAGS(E)                                                                       \
  constexpr E operator|(E a, E b) noexcept {                                                       \
    using U = std::underlying_type_t<E>;                                                           \
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));                                  \
  }                                                                                                \
  constexpr E operator&(E a, E b) noexcept {                                                       \
    using U = std::underlying_type_t<E>;                                                           \
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));                                  \
  }                                                                                                \
  constexpr E& operator|=(E& a, E b) noexcept {                                                    \
    a = a | b;                                                                                     \
    return a;                                                                                      \
  }                                                                                                \
  constexpr bool hasFlag(E value, E flag) noexcept {                                               \
    using U = std::underlying_type_t<E>;                                                           \
    return static_cast<U>(flag) != 0 && (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag); \
  }                                                                                                \
  constexpr bool anyFlag(E value) noexcept { return static_cast<std::underlying_type_t<E>>(value) != 0; }
