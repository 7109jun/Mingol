// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <string_view>
#include <vector>

#include "mingol/core/hash.hpp"
#include "mingol_test.hpp"

using namespace mingol;

namespace {
std::vector<std::uint8_t> bytes(std::string_view s) { return {s.begin(), s.end()}; }
}  // namespace

TEST(Hash, Crc32KnownVectors) {
  CHECK_EQ(crc32(bytes("123456789")), 0xCBF43926u);  // standard CRC-32 check value
  CHECK_EQ(crc32({}), 0u);
  CHECK_EQ(crc32(bytes("a")), 0xE8B7BE43u);
  CHECK_EQ(crc32(bytes("The quick brown fox jumps over the lazy dog")), 0x414FA339u);
}

TEST(Hash, Crc32Incremental) {
  const auto whole = bytes("hello world, this is mingol");
  const auto a = std::vector<std::uint8_t>(whole.begin(), whole.begin() + 9);
  const auto b = std::vector<std::uint8_t>(whole.begin() + 9, whole.end());
  CHECK_EQ(crc32(b, crc32(a)), crc32(whole));
}

TEST(Hash, Crc32DetectsSingleBitFlip) {
  auto data = bytes("integrity matters");
  const auto original = crc32(data);
  data[3] ^= 0x01;
  CHECK(crc32(data) != original);
}

TEST(Hash, Fnv1aKnownVectorsAndConstexpr) {
  static_assert(fnv1a64("") == 0xcbf29ce484222325ull);
  static_assert(fnv1a64("a") == 0xaf63dc4c8601ec8cull);
  static_assert(fnv1a64("foobar") == 0x85944171f73967e8ull);
  CHECK_EQ(fnv1a64("a"), 0xaf63dc4c8601ec8cull);
}
