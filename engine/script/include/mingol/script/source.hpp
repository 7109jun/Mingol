// SPDX-License-Identifier: GPL-2.0-only
// Source locations and diagnostics shared by every stage of the Mingol Script compiler.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mingol::script {

struct SourceLoc {
  std::uint32_t file = 0;  // index into the compilation's file table
  std::uint32_t line = 1;  // 1-based
  std::uint32_t col = 1;   // 1-based, in bytes
};

struct Diagnostic {
  std::string file;
  std::uint32_t line = 0;
  std::uint32_t col = 0;
  std::string message;
  std::string format() const { return file + ":" + std::to_string(line) + ":" + std::to_string(col) + ": error: " + message; }
};

// Collects errors instead of stopping at the first one. Capped so a pathological file cannot
// produce millions of messages.
class Diagnostics {
 public:
  static constexpr std::size_t kMaxErrors = 100;

  void setFiles(std::vector<std::string> names) { files_ = std::move(names); }
  std::uint32_t addFile(std::string name) {
    files_.push_back(std::move(name));
    return static_cast<std::uint32_t>(files_.size() - 1);
  }
  void error(SourceLoc loc, std::string message) {
    if (errors_.size() >= kMaxErrors) { truncated_ = true; return; }
    errors_.push_back({loc.file < files_.size() ? files_[loc.file] : std::string("<unknown>"), loc.line, loc.col, std::move(message)});
  }
  bool hasErrors() const noexcept { return !errors_.empty(); }
  bool truncated() const noexcept { return truncated_; }
  const std::vector<Diagnostic>& errors() const noexcept { return errors_; }
  const std::string& fileName(std::uint32_t i) const { return files_.at(i); }

 private:
  std::vector<std::string> files_;
  std::vector<Diagnostic> errors_;
  bool truncated_ = false;
};

}  // namespace mingol::script
