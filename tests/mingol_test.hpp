// SPDX-License-Identifier: GPL-2.0-only
// Minimal self-contained test framework (no external dependency).
//   TEST(Suite, Name) { CHECK(x == 1); REQUIRE(ptr != nullptr); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
#pragma once

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace mingol::test {

struct Case {
  const char* suite;
  const char* name;
  void (*fn)();
};

std::vector<Case>& registry();

struct Registrar {
  Registrar(const char* suite, const char* name, void (*fn)()) { registry().push_back({suite, name, fn}); }
};

// Records a failure against the currently running test.
void fail(const char* file, int line, const std::string& message);

// Marks the current test as skipped (reported separately; never counts as a pass).
void skip(const std::string& reason);

template <class T>
std::string str(const T& v) {
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(v));
  } else if constexpr (requires(std::ostringstream& o, const T& t) { o << t; }) {
    std::ostringstream o;
    o << v;
    return o.str();
  } else {
    return "<unprintable>";
  }
}

}  // namespace mingol::test

#define MT_CONCAT_(a, b) a##b
#define MT_CONCAT(a, b) MT_CONCAT_(a, b)

#define TEST(suite, name)                                                                          \
  static void MT_CONCAT(mt_fn_##suite##_, name)();                                                 \
  static ::mingol::test::Registrar MT_CONCAT(mt_reg_##suite##_, name)(#suite, #name,               \
                                                                      &MT_CONCAT(mt_fn_##suite##_, name)); \
  static void MT_CONCAT(mt_fn_##suite##_, name)()

// Skips the rest of the test; use directly inside the TEST body.
#define SKIP_TEST(reason)                \
  do {                                   \
    ::mingol::test::skip(reason);        \
    return;                              \
  } while (false)

#define CHECK(expr)                                                    \
  do {                                                                   \
    if (!(expr)) ::mingol::test::fail(__FILE__, __LINE__, "CHECK(" #expr ")"); \
  } while (false)

// CHECK with an explanatory message (any std::string expression) shown on failure.
#define CHECK_MSG(expr, msg)                                                                     \
  do {                                                                                           \
    if (!(expr)) ::mingol::test::fail(__FILE__, __LINE__, std::string("CHECK(" #expr ") - ") + (msg)); \
  } while (false)

// Aborts the current test (must be used directly inside the TEST body, which returns void).
#define REQUIRE(expr)                                                      \
  do {                                                                     \
    if (!(expr)) {                                                         \
      ::mingol::test::fail(__FILE__, __LINE__, "REQUIRE(" #expr ")");      \
      return;                                                              \
    }                                                                      \
  } while (false)

#define CHECK_EQ(a, b)                                                                            \
  do {                                                                                            \
    const auto mtA = (a); /* by value: operands may be members of temporaries */                  \
    const auto mtB = (b);                                                                         \
    if (!(mtA == mtB))                                                                            \
      ::mingol::test::fail(__FILE__, __LINE__,                                                    \
                           std::string("CHECK_EQ(" #a ", " #b ") -> ") + ::mingol::test::str(mtA) + \
                               " vs " + ::mingol::test::str(mtB));                                \
  } while (false)

#define CHECK_NEAR(a, b, eps)                                                                    \
  do {                                                                                           \
    const double mtA = static_cast<double>(a);                                                   \
    const double mtB = static_cast<double>(b);                                                   \
    if (!(std::fabs(mtA - mtB) <= static_cast<double>(eps)))                                     \
      ::mingol::test::fail(__FILE__, __LINE__,                                                   \
                           std::string("CHECK_NEAR(" #a ", " #b ") -> ") + std::to_string(mtA) + \
                               " vs " + std::to_string(mtB));                                    \
  } while (false)
