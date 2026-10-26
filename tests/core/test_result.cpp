// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#include "mingol/core/error.hpp"
#include "mingol_test.hpp"

using namespace mingol;

namespace {

Result<int> parsePositive(int v) {
  if (v <= 0) return makeError(ErrorCode::InvalidArgument, "value {} is not positive", v);
  return v;
}

Result<int> doubled(int v) {
  MINGOL_TRY_ASSIGN(int p, parsePositive(v));
  return p * 2;
}

Result<void> check(int v) {
  MINGOL_TRY(parsePositive(v));
  return {};
}

}  // namespace

TEST(Result, ValueAndError) {
  auto ok = parsePositive(3);
  REQUIRE(ok.ok());
  CHECK_EQ(ok.value(), 3);

  auto bad = parsePositive(-1);
  REQUIRE(!bad.ok());
  CHECK(bad.error().code == ErrorCode::InvalidArgument);
  CHECK(bad.error().message == "value -1 is not positive");
  CHECK_EQ(bad.valueOr(42), 42);
}

TEST(Result, Propagation) {
  CHECK_EQ(doubled(4).value(), 8);
  auto bad = doubled(0);
  REQUIRE(!bad.ok());
  CHECK(bad.error().code == ErrorCode::InvalidArgument);

  CHECK(check(1).ok());
  CHECK(!check(-5).ok());
}

TEST(Result, MoveOnlyAndStringPayload) {
  Result<std::string> r = std::string("hello");
  REQUIRE(r.ok());
  std::string taken = std::move(r).value();
  CHECK(taken == "hello");
}

TEST(Result, ErrorDescribe) {
  Error e(ErrorCode::Corrupt, "bad header");
  CHECK(e.describe() == "Corrupt: bad header");
  CHECK(std::string(toString(ErrorCode::AuthFailed)) == "AuthFailed");
}
