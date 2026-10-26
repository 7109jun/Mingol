// SPDX-License-Identifier: GPL-2.0-only
#include <filesystem>
#include <string>
#include <vector>

#include "mingol/core/fs.hpp"
#include "mingol/core/uuid.hpp"
#include "mingol_test.hpp"

using namespace mingol;

namespace {

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() / ("mingol_fs_" + Uuid::generate().toString());
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

std::size_t entryCount(const std::filesystem::path& dir) {
  std::size_t n = 0;
  for (const auto& e : std::filesystem::directory_iterator(dir)) {
    (void)e;
    ++n;
  }
  return n;
}

}  // namespace

TEST(Fs, RoundTripBinary) {
  TempDir dir;
  std::vector<std::uint8_t> data(70000);
  for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 31 + 7);
  const auto file = dir.path / "blob.bin";
  REQUIRE(fs::writeFileAtomic(file, data).ok());
  auto back = fs::readFile(file);
  REQUIRE(back.ok());
  CHECK(back.value() == data);
}

TEST(Fs, EmptyFile) {
  TempDir dir;
  const auto file = dir.path / "empty";
  REQUIRE(fs::writeFileAtomic(file, {}).ok());
  auto back = fs::readFile(file);
  REQUIRE(back.ok());
  CHECK(back.value().empty());
}

TEST(Fs, OverwriteLeavesNoTempFiles) {
  TempDir dir;
  const auto file = dir.path / "scene.txt";
  REQUIRE(fs::writeTextFileAtomic(file, "first version, long content").ok());
  REQUIRE(fs::writeTextFileAtomic(file, "second").ok());
  auto text = fs::readTextFile(file);
  REQUIRE(text.ok());
  CHECK(text.value() == "second");
  CHECK_EQ(entryCount(dir.path), std::size_t{1});
}

TEST(Fs, MissingFileIsNotFound) {
  TempDir dir;
  auto r = fs::readFile(dir.path / "nope");
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::NotFound);
}

TEST(Fs, DirectoryIsNotReadableAsFile) {
  TempDir dir;
  auto r = fs::readFile(dir.path);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::NotFound);
}

TEST(Fs, SizeLimitIsEnforced) {
  TempDir dir;
  const auto file = dir.path / "big";
  REQUIRE(fs::writeTextFileAtomic(file, std::string(1000, 'x')).ok());
  auto r = fs::readFile(file, 999);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::OutOfRange);
  CHECK(fs::readFile(file, 1000).ok());
}

TEST(Fs, FailedWriteKeepsOriginalAndLeavesNoTemp) {
  TempDir dir;
  const auto file = dir.path / "keep.txt";
  REQUIRE(fs::writeTextFileAtomic(file, "original").ok());
  // Destination is a directory: the rename must fail and the original path must stay untouched.
  const auto blocker = dir.path / "blocked";
  std::filesystem::create_directory(blocker);
  std::filesystem::create_directory(blocker / "child");  // non-empty directory cannot be replaced
  auto r = fs::writeTextFileAtomic(blocker, "data");
  CHECK(!r.ok());
  CHECK(std::filesystem::is_directory(blocker));
  CHECK(fs::readTextFile(file).value() == "original");
  CHECK_EQ(entryCount(dir.path), std::size_t{2});  // keep.txt + blocked, no stray temp file
}

TEST(Fs, WriteToMissingDirectoryFails) {
  TempDir dir;
  auto r = fs::writeTextFileAtomic(dir.path / "missing" / "f.txt", "x");
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::Io);
}
