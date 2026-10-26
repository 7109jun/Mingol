// SPDX-License-Identifier: GPL-2.0-only
#include <set>
#include <unordered_set>

#include "mingol/core/uuid.hpp"
#include "mingol_test.hpp"

using namespace mingol;

TEST(Uuid, GenerateIsVersion4AndUnique) {
  std::set<Uuid> seen;
  for (int i = 0; i < 1000; ++i) {
    const Uuid u = Uuid::generate();
    CHECK_EQ(u.bytes[6] >> 4, 4);
    CHECK_EQ(u.bytes[8] >> 6, 2);
    CHECK(!u.isNil());
    seen.insert(u);
  }
  CHECK_EQ(seen.size(), std::size_t{1000});
}

TEST(Uuid, StringRoundTrip) {
  const Uuid u = Uuid::generate();
  const std::string s = u.toString();
  CHECK_EQ(s.size(), std::size_t{36});
  auto parsed = Uuid::parse(s);
  REQUIRE(parsed.has_value());
  CHECK(*parsed == u);
}

TEST(Uuid, ParseKnownValueAndCase) {
  auto u = Uuid::parse("123E4567-e89b-12D3-A456-426614174000");
  REQUIRE(u.has_value());
  CHECK(u->toString() == "123e4567-e89b-12d3-a456-426614174000");
  CHECK_EQ(u->bytes[0], 0x12);
  CHECK_EQ(u->bytes[15], 0x00);
}

TEST(Uuid, ParseRejectsMalformedInput) {
  CHECK(!Uuid::parse("").has_value());
  CHECK(!Uuid::parse("123e4567-e89b-12d3-a456-42661417400").has_value());    // too short
  CHECK(!Uuid::parse("123e4567-e89b-12d3-a456-4266141740000").has_value());  // too long
  CHECK(!Uuid::parse("123e4567ee89b-12d3-a456-426614174000").has_value());   // missing dash
  CHECK(!Uuid::parse("123e4567-e89b-12d3-a456-42661417400g").has_value());   // bad hex digit
}

TEST(Uuid, HashableAndNil) {
  std::unordered_set<Uuid> set;
  const Uuid u = Uuid::generate();
  set.insert(u);
  set.insert(u);
  CHECK_EQ(set.size(), std::size_t{1});
  CHECK(Uuid{}.isNil());
}
