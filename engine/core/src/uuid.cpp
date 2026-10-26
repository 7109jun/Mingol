// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/uuid.hpp"

#include <algorithm>
#include <mutex>
#include <random>

#include "mingol/core/hash.hpp"

namespace mingol {

namespace {

int hexValue(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Uuid Uuid::generate() {
  static std::mutex mutex;
  static std::random_device device;  // OS entropy source
  Uuid u;
  {
    std::lock_guard lock(mutex);
    for (std::size_t i = 0; i < 16; i += 4) {
      const std::uint32_t r = device();
      for (std::size_t k = 0; k < 4; ++k) {
        u.bytes[i + k] = static_cast<std::uint8_t>(r >> (8 * k));
      }
    }
  }
  u.bytes[6] = static_cast<std::uint8_t>((u.bytes[6] & 0x0F) | 0x40);  // version 4
  u.bytes[8] = static_cast<std::uint8_t>((u.bytes[8] & 0x3F) | 0x80);  // RFC 4122 variant
  return u;
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept {
  if (text.size() != 36) return std::nullopt;
  Uuid u;
  std::size_t byteIndex = 0;
  for (std::size_t i = 0; i < text.size();) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') return std::nullopt;
      ++i;
      continue;
    }
    const int hi = hexValue(text[i]);
    const int lo = hexValue(text[i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    u.bytes[byteIndex++] = static_cast<std::uint8_t>((hi << 4) | lo);
    i += 2;
  }
  return u;
}

std::string Uuid::toString() const {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(36);
  for (std::size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) s.push_back('-');
    s.push_back(kHex[bytes[i] >> 4]);
    s.push_back(kHex[bytes[i] & 0x0F]);
  }
  return s;
}

bool Uuid::isNil() const noexcept {
  return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0; });
}

}  // namespace mingol

std::size_t std::hash<mingol::Uuid>::operator()(const mingol::Uuid& u) const noexcept {
  return static_cast<std::size_t>(
      mingol::fnv1a64(std::string_view(reinterpret_cast<const char*>(u.bytes.data()), u.bytes.size())));
}
