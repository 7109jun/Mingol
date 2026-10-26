// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace mingol {

// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Pass the previous result as `seed` to continue
// a running checksum over several buffers.
std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t seed = 0) noexcept;

// 64-bit FNV-1a; usable at compile time for name hashing.
constexpr std::uint64_t fnv1a64(std::string_view text) noexcept {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (const char c : text) {
    h ^= static_cast<std::uint8_t>(c);
    h *= 0x100000001b3ull;
  }
  return h;
}

}  // namespace mingol
