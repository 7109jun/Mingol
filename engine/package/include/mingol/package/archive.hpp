// SPDX-License-Identifier: GPL-2.0-only
// The `.mingol` archive: the packaged, read-only form of a game's files (scenes, assets, scripts, the project
// file). One file, random access by name, optionally encrypted. Full layout and rules: docs/MINGOL.md.
//
//   header (80 bytes) | entry data ... (contiguous, in table order) | table of contents (at the end)
//
// Security model (stated plainly): the key must be available to the game executable, so encryption only
// keeps files from being casually extracted and detects tampering; it does not stop a determined attacker who
// has the executable. Encrypted archives use ChaCha20-Poly1305 per 64 KiB chunk with keys derived by
// HKDF-SHA256 from the archive key and a random per-file salt; the table of contents is authenticated too.
// Unencrypted archives carry SHA-256 hashes (corruption detection, not tamper-proof against someone who can
// rewrite the whole file).
//
// Every byte of an archive is covered by an integrity check: any modification, truncation or extension is
// reported when the archive is opened or when the affected entry is read.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/package/crypto.hpp"
#include "mingol/package/mindev.hpp"

namespace mingol::package {

inline constexpr std::uint32_t kMingolFormatVersion = 1;
inline constexpr std::size_t kArchiveChunkSize = 64 * 1024;
inline constexpr std::size_t kMaxArchiveEntries = 1u << 20;

// 256-bit archive key.
struct ArchiveKey {
  crypto::Key256 bytes{};
  static Result<ArchiveKey> fromHex(std::string_view hex64);
  // PBKDF2-HMAC-SHA256 of a passphrase. `salt` should be unique per project (e.g. the project id).
  static Result<ArchiveKey> fromPassphrase(std::string_view passphrase, std::string_view salt, std::uint32_t iterations = 200000);
  static Result<ArchiveKey> generate();
  std::string toHex() const;
};

struct WriteOptions {
  std::optional<ArchiveKey> key;                       // set: encrypt
  std::optional<std::array<std::uint8_t, 16>> salt;    // fixed salt (reproducible builds/tests); default: random
};

struct EntryInfo {
  std::string name;
  std::uint64_t size = 0;        // plaintext bytes
  std::uint64_t storedSize = 0;  // bytes in the archive
};

class ArchiveWriter {
 public:
  // `name`: relative path with '/' separators (see isValidRelativePath). Duplicates are rejected.
  Result<void> add(std::string name, std::vector<std::uint8_t> data);
  Result<void> addFile(std::string name, const std::filesystem::path& file);
  std::size_t count() const noexcept { return entries_.size(); }
  Result<std::vector<std::uint8_t>> serialize(const WriteOptions& options = {}) const;
  // Crash-safe: temp file + rename.
  Result<void> save(const std::filesystem::path& path, const WriteOptions& options = {}) const;

 private:
  struct Item { std::string name; std::vector<std::uint8_t> data; };
  std::vector<Item> entries_;
};

class ArchiveReader {
 public:
  ~ArchiveReader();
  ArchiveReader(ArchiveReader&&) noexcept;
  ArchiveReader& operator=(ArchiveReader&&) noexcept;

  static Result<ArchiveReader> openFile(const std::filesystem::path& path, const std::optional<ArchiveKey>& key = {});
  static Result<ArchiveReader> openMemory(std::vector<std::uint8_t> bytes, const std::optional<ArchiveKey>& key = {});
  // An archive appended to an executable by packExecutable().
  static Result<ArchiveReader> openExecutable(const std::filesystem::path& exe, const std::optional<ArchiveKey>& key = {});
  // True if `exe` has an archive appended.
  static bool hasEmbeddedArchive(const std::filesystem::path& exe);

  bool encrypted() const noexcept;
  const std::vector<EntryInfo>& entries() const noexcept;
  bool contains(std::string_view name) const;
  // Reads and verifies one entry. NotFound for an unknown name; Corrupt / AuthFailed when damaged or wrong key.
  Result<std::vector<std::uint8_t>> read(std::string_view name) const;
  // Reads every entry (full integrity check of the whole archive).
  Result<void> verifyAll() const;

 struct Impl;   // opaque

 private:
  explicit ArchiveReader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Writes exe + archive + trailer to `output` (atomically); the result is still a runnable executable.
// Fails if `exe` already has an archive. Copies the file permissions of `exe` (POSIX).
Result<void> packExecutable(const std::filesystem::path& exe, const std::filesystem::path& archive, const std::filesystem::path& output);

struct ProjectPackOptions {
  WriteOptions write;
};
struct ProjectPackReport {
  std::size_t files = 0;
  std::uint64_t bytes = 0;
  std::vector<std::string> skipped;   // symlinks, unreadable files, ...
};
// Packs a project: its .mindev (as "project.mindev"), every scene it lists, and all files below its assetDirs
// and scriptDirs. Entry names are project-relative; the archive is deterministic apart from the encryption salt.
Result<std::vector<std::uint8_t>> packProject(const std::filesystem::path& mindevPath, const ProjectPackOptions& options, ProjectPackReport* report = nullptr);

}  // namespace mingol::package
