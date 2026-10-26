// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/error.hpp"

namespace mingol {

const char* toString(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok: return "Ok";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::Io: return "Io";
    case ErrorCode::Corrupt: return "Corrupt";
    case ErrorCode::VersionMismatch: return "VersionMismatch";
    case ErrorCode::AuthFailed: return "AuthFailed";
    case ErrorCode::OutOfRange: return "OutOfRange";
    case ErrorCode::OutOfMemory: return "OutOfMemory";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::Timeout: return "Timeout";
    case ErrorCode::OutOfDate: return "OutOfDate";
    case ErrorCode::Internal: return "Internal";
  }
  return "Unknown";
}

std::string Error::describe() const {
  return std::string(toString(code)) + ": " + message;
}

}  // namespace mingol
