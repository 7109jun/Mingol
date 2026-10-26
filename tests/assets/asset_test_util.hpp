// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "mingol/core/fs.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

inline std::vector<std::uint8_t> fixture(const std::string& name) {
  auto r = fs::readFile(std::string(MINGOL_TEST_DATA_DIR) + "/" + name);
  if (!r.ok()) {
    fail(__FILE__, __LINE__, "cannot read fixture " + name + ": " + r.error().describe());
    return {};
  }
  return std::move(r).value();
}

// Deterministic generator for corruption tests (no dependence on libc rand()).
struct Lcg {
  std::uint64_t s;
  explicit Lcg(std::uint64_t seed) : s(seed) {}
  std::uint32_t next() {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>(s >> 33);
  }
};

// Applies 1-4 random byte edits / truncations to a copy of `src`.
inline std::vector<std::uint8_t> mutate(const std::vector<std::uint8_t>& src, Lcg& rng) {
  std::vector<std::uint8_t> v = src;
  if (v.empty()) return v;
  const int edits = 1 + static_cast<int>(rng.next() % 4);
  for (int i = 0; i < edits; ++i) {
    const std::size_t pos = rng.next() % v.size();
    switch (rng.next() % 4) {
      case 0: v[pos] = static_cast<std::uint8_t>(rng.next()); break;
      case 1: v[pos] ^= static_cast<std::uint8_t>(1u << (rng.next() % 8)); break;
      case 2: v.resize(pos); break;  // truncate
      default: v[pos] = 0xFF; break;
    }
    if (v.empty()) break;
  }
  return v;
}

}  // namespace mingol::test
