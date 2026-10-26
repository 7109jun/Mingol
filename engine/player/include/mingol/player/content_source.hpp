// SPDX-License-Identifier: GPL-2.0-only
// Where a game's files come from: a project directory (development) or a .mingol archive (shipping).
// Names are project relative with '/' separators; the project file is always called "project.mindev".
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/package/archive.hpp"

namespace mingol::player {

class ContentSource {
 public:
  virtual ~ContentSource() = default;
  virtual Result<std::vector<std::uint8_t>> read(const std::string& name) const = 0;
  virtual bool contains(const std::string& name) const = 0;
  // Names of all files below `prefix` (a directory name without trailing '/'), sorted, recursive.
  virtual std::vector<std::string> list(const std::string& prefix) const = 0;
  virtual std::string describe() const = 0;
};

// `mindev` is the project file; everything else is resolved relative to its directory.
Result<std::unique_ptr<ContentSource>> openDirectorySource(const std::filesystem::path& mindev);
Result<std::unique_ptr<ContentSource>> openArchiveSource(const std::filesystem::path& archive, const std::optional<package::ArchiveKey>& key = {});
// An archive appended to an executable (see package::packExecutable).
Result<std::unique_ptr<ContentSource>> openEmbeddedSource(const std::filesystem::path& exe, const std::optional<package::ArchiveKey>& key = {});

}  // namespace mingol::player
