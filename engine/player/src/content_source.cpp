// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/player/content_source.hpp"

#include <algorithm>

#include "mingol/core/fs.hpp"
#include "mingol/package/mindev.hpp"

namespace mingol::player {

namespace fsys = std::filesystem;

namespace {

class DirectorySource final : public ContentSource {
 public:
  DirectorySource(fsys::path mindev, fsys::path root) : mindev_(std::move(mindev)), root_(std::move(root)) {}

  Result<std::vector<std::uint8_t>> read(const std::string& name) const override {
    const auto p = resolve(name);
    if (!p) return makeError(ErrorCode::InvalidArgument, "invalid content name '{}'", name);
    return fs::readFile(*p);
  }
  bool contains(const std::string& name) const override {
    const auto p = resolve(name);
    std::error_code ec;
    return p && fsys::is_regular_file(*p, ec);
  }
  std::vector<std::string> list(const std::string& prefix) const override {
    std::vector<std::string> out;
    if (!package::isValidRelativePath(prefix)) return out;
    std::error_code ec;
    const fsys::path base = root_ / prefix;
    if (!fsys::is_directory(base, ec)) return out;
    for (fsys::recursive_directory_iterator it(base, fsys::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      const std::string rel = it->path().lexically_relative(root_).generic_string();
      if (package::isValidRelativePath(rel)) out.push_back(rel);
    }
    std::sort(out.begin(), out.end());
    return out;
  }
  std::string describe() const override { return "directory " + root_.string(); }

 private:
  std::optional<fsys::path> resolve(const std::string& name) const {
    if (name == "project.mindev") return mindev_;
    if (!package::isValidRelativePath(name)) return std::nullopt;
    return root_ / name;
  }
  fsys::path mindev_, root_;
};

class ArchiveSource final : public ContentSource {
 public:
  ArchiveSource(package::ArchiveReader r, std::string what) : reader_(std::move(r)), what_(std::move(what)) {}
  Result<std::vector<std::uint8_t>> read(const std::string& name) const override { return reader_.read(name); }
  bool contains(const std::string& name) const override { return reader_.contains(name); }
  std::vector<std::string> list(const std::string& prefix) const override {
    std::vector<std::string> out;
    const std::string p = prefix + "/";
    for (const auto& e : reader_.entries())
      if (e.name.rfind(p, 0) == 0) out.push_back(e.name);
    std::sort(out.begin(), out.end());
    return out;
  }
  std::string describe() const override { return what_; }

 private:
  package::ArchiveReader reader_;
  std::string what_;
};

}  // namespace

Result<std::unique_ptr<ContentSource>> openDirectorySource(const fsys::path& mindev) {
  std::error_code ec;
  if (!fsys::is_regular_file(mindev, ec)) return makeError(ErrorCode::NotFound, "project file {} not found", mindev.string());
  const fsys::path abs = fsys::absolute(mindev, ec);
  return std::unique_ptr<ContentSource>(new DirectorySource(abs, abs.parent_path()));
}

Result<std::unique_ptr<ContentSource>> openArchiveSource(const fsys::path& archive, const std::optional<package::ArchiveKey>& key) {
  MINGOL_TRY_ASSIGN(package::ArchiveReader r, package::ArchiveReader::openFile(archive, key));
  return std::unique_ptr<ContentSource>(new ArchiveSource(std::move(r), "archive " + archive.string()));
}

Result<std::unique_ptr<ContentSource>> openEmbeddedSource(const fsys::path& exe, const std::optional<package::ArchiveKey>& key) {
  MINGOL_TRY_ASSIGN(package::ArchiveReader r, package::ArchiveReader::openExecutable(exe, key));
  return std::unique_ptr<ContentSource>(new ArchiveSource(std::move(r), "embedded archive in " + exe.string()));
}

}  // namespace mingol::player
