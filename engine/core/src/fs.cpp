// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/fs.hpp"

#include <atomic>
#include <cstdio>
#include <memory>
#include <system_error>

#include "mingol/core/platform.hpp"

#if defined(MINGOL_PLATFORM_WINDOWS)
#include <io.h>
#define MINGOL_FSYNC(f) _commit(_fileno(f))
#else
#include <unistd.h>
#define MINGOL_FSYNC(f) fsync(fileno(f))
#endif

namespace mingol::fs {

namespace {

std::FILE* openFile(const std::filesystem::path& path, bool write) {
#if defined(MINGOL_PLATFORM_WINDOWS)
  return _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
  return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

struct FileCloser {
  void operator()(std::FILE* f) const noexcept {
    if (f != nullptr) std::fclose(f);
  }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

}  // namespace

Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path, std::uint64_t maxSize) {
  std::error_code ec;
  const bool exists = std::filesystem::is_regular_file(path, ec);
  if (ec || !exists) {
    return makeError(ErrorCode::NotFound, "file not found: {}", path.string());
  }
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) {
    return makeError(ErrorCode::Io, "cannot stat {}: {}", path.string(), ec.message());
  }
  if (size > maxSize) {
    return makeError(ErrorCode::OutOfRange, "file too large ({} > {} bytes): {}", size, maxSize, path.string());
  }

  FilePtr file(openFile(path, false));
  if (!file) {
    return makeError(ErrorCode::Io, "cannot open {}", path.string());
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  if (size != 0 && std::fread(data.data(), 1, data.size(), file.get()) != data.size()) {
    return makeError(ErrorCode::Io, "short read on {}", path.string());
  }
  return data;
}

Result<std::string> readTextFile(const std::filesystem::path& path, std::uint64_t maxSize) {
  MINGOL_TRY_ASSIGN(auto bytes, readFile(path, maxSize));
  return std::string(bytes.begin(), bytes.end());
}

Result<void> writeFileAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
  static std::atomic<std::uint64_t> counter{0};
  std::filesystem::path tmp = path;
  tmp += ".tmp" + std::to_string(counter.fetch_add(1));

  auto cleanup = [&tmp]() {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
  };

  {
    FilePtr file(openFile(tmp, true));
    if (!file) {
      return makeError(ErrorCode::Io, "cannot create temp file for {}", path.string());
    }
    if (!data.empty() && std::fwrite(data.data(), 1, data.size(), file.get()) != data.size()) {
      file.reset();
      cleanup();
      return makeError(ErrorCode::Io, "write failed for {}", path.string());
    }
    if (std::fflush(file.get()) != 0 || MINGOL_FSYNC(file.get()) != 0) {
      file.reset();
      cleanup();
      return makeError(ErrorCode::Io, "flush failed for {}", path.string());
    }
  }  // closed here

  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    cleanup();
    return makeError(ErrorCode::Io, "cannot replace {}: {}", path.string(), ec.message());
  }
  return {};
}

Result<void> writeTextFileAtomic(const std::filesystem::path& path, std::string_view text) {
  return writeFileAtomic(
      path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

}  // namespace mingol::fs
