// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mingol/core/error.hpp"

namespace mingol::fs {

inline constexpr std::uint64_t kDefaultMaxFileSize = 1ull << 32;  // 4 GiB

// Reads a whole file. Fails with NotFound / Io / OutOfRange (larger than maxSize).
Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path,
                                           std::uint64_t maxSize = kDefaultMaxFileSize);
Result<std::string> readTextFile(const std::filesystem::path& path, std::uint64_t maxSize = kDefaultMaxFileSize);

// Crash-safe save: writes a sibling temp file, flushes it to disk, then renames over `path`.
// On failure the original file is left unchanged and the temp file is removed.
Result<void> writeFileAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> data);
Result<void> writeTextFileAtomic(const std::filesystem::path& path, std::string_view text);

}  // namespace mingol::fs
