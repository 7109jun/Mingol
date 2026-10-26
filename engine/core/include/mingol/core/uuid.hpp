// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mingol {

// RFC 4122 version-4 (random) UUID. Used for stable asset and object identity.
struct Uuid {
  std::array<std::uint8_t, 16> bytes{};

  static Uuid generate();
  // Accepts "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (hex, case-insensitive).
  static std::optional<Uuid> parse(std::string_view text) noexcept;

  std::string toString() const;
  bool isNil() const noexcept;

  friend bool operator==(const Uuid&, const Uuid&) = default;
  friend auto operator<=>(const Uuid&, const Uuid&) = default;
};

}  // namespace mingol

template <>
struct std::hash<mingol::Uuid> {
  std::size_t operator()(const mingol::Uuid& u) const noexcept;
};
