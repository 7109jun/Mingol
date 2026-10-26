// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/hash.hpp"

#include <array>

namespace mingol {

namespace {

constexpr std::array<std::uint32_t, 256> makeCrcTable() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    table[i] = c;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = makeCrcTable();

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t seed) noexcept {
  std::uint32_t c = ~seed;
  for (const std::uint8_t byte : data) {
    c = kCrcTable[(c ^ byte) & 0xFFu] ^ (c >> 8);
  }
  return ~c;
}

}  // namespace mingol
