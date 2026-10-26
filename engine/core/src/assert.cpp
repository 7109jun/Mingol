// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/assert.hpp"

#include <cstdio>
#include <cstdlib>

namespace mingol::detail {

void assertFail(const char* expr, const char* file, int line, const char* msg) noexcept {
  // Deliberately uses stdio only: the logger itself may be what failed.
  std::fprintf(stderr, "[mingol] assertion failed: %s\n  at %s:%d\n", expr, file, line);
  if (msg != nullptr) {
    std::fprintf(stderr, "  %s\n", msg);
  }
  std::fflush(stderr);
  std::abort();
}

}  // namespace mingol::detail
