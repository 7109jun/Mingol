// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <variant>

#include "mingol/core/assert.hpp"

namespace mingol {

enum class ErrorCode : std::uint32_t {
  Ok = 0,
  InvalidArgument,
  NotFound,
  AlreadyExists,
  Io,
  Corrupt,          // data failed structural validation
  VersionMismatch,  // data/ABI version not supported
  AuthFailed,       // authentication tag or key mismatch
  OutOfRange,
  OutOfMemory,
  Unsupported,
  Timeout,
  OutOfDate,  // a presentation surface changed and its swapchain must be recreated
  Internal,
};

const char* toString(ErrorCode code) noexcept;

struct Error {
  ErrorCode code = ErrorCode::Internal;
  std::string message;

  Error() = default;
  Error(ErrorCode c, std::string m) : code(c), message(std::move(m)) {}

  // "Code: message"
  std::string describe() const;
};

template <class... Args>
Error makeError(ErrorCode code, std::format_string<Args...> fmt, Args&&... args) {
  return Error(code, std::format(fmt, std::forward<Args>(args)...));
}

// A value or an Error. Accessing the wrong alternative is a programming error and aborts.
template <class T>
class [[nodiscard]] Result {
 public:
  Result(T value) : v_(std::in_place_index<0>, std::move(value)) {}
  Result(Error error) : v_(std::in_place_index<1>, std::move(error)) {}

  bool ok() const noexcept { return v_.index() == 0; }
  explicit operator bool() const noexcept { return ok(); }

  T& value() & {
    MINGOL_VERIFY(ok());
    return std::get<0>(v_);
  }
  const T& value() const& {
    MINGOL_VERIFY(ok());
    return std::get<0>(v_);
  }
  T&& value() && {
    MINGOL_VERIFY(ok());
    return std::get<0>(std::move(v_));
  }

  const Error& error() const& {
    MINGOL_VERIFY(!ok());
    return std::get<1>(v_);
  }

  T valueOr(T fallback) const& { return ok() ? std::get<0>(v_) : std::move(fallback); }

 private:
  std::variant<T, Error> v_;
};

template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  Result(Error error) : error_(std::move(error)), ok_(false) {}

  bool ok() const noexcept { return ok_; }
  explicit operator bool() const noexcept { return ok_; }

  const Error& error() const& {
    MINGOL_VERIFY(!ok_);
    return error_;
  }

 private:
  Error error_;
  bool ok_ = true;
};

}  // namespace mingol

// Propagation helpers. The enclosing function must return Result<U> (any U) or Error-convertible.
//   MINGOL_TRY(doThing());                       // Result<void>
//   MINGOL_TRY_ASSIGN(auto bytes, readFile(p));  // Result<T> -> declares `bytes`
#define MINGOL_TRY(expr)                                   \
  do {                                                     \
    auto&& mingolTryResult_ = (expr);                      \
    if (!mingolTryResult_.ok()) {                          \
      return mingolTryResult_.error();                     \
    }                                                      \
  } while (false)

#define MINGOL_TRY_ASSIGN_IMPL(tmp, decl, expr) \
  auto tmp = (expr);                            \
  if (!tmp.ok()) {                              \
    return tmp.error();                         \
  }                                             \
  decl = std::move(tmp).value()

#define MINGOL_TRY_ASSIGN(decl, expr) MINGOL_TRY_ASSIGN_IMPL(MINGOL_UNIQUE(mingolTryTmp_), decl, expr)
