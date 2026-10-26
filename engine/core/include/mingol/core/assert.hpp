// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "mingol/core/platform.hpp"

namespace mingol::detail {

// Logs the failure (if logging is usable) and aborts. Never returns.
[[noreturn]] void assertFail(const char* expr, const char* file, int line, const char* msg) noexcept;

}  // namespace mingol::detail

// Always-on check: guards invariants whose violation would be undefined behaviour.
#define MINGOL_VERIFY(expr) \
  (static_cast<bool>(expr) ? static_cast<void>(0) : ::mingol::detail::assertFail(#expr, __FILE__, __LINE__, nullptr))

#define MINGOL_VERIFY_MSG(expr, msg) \
  (static_cast<bool>(expr) ? static_cast<void>(0) : ::mingol::detail::assertFail(#expr, __FILE__, __LINE__, (msg)))

// Debug-only check.
#ifndef NDEBUG
#define MINGOL_ASSERT(expr) MINGOL_VERIFY(expr)
#else
#define MINGOL_ASSERT(expr) static_cast<void>(0)
#endif
