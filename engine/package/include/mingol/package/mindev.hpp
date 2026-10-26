// SPDX-License-Identifier: GPL-2.0-only
// The `.mindev` project file: a UTF-8 text file containing one JSON object. It is meant to be read,
// diffed and merged by humans and version control, so it is never compressed or encrypted.
//
//   {
//     "format": "mindev",
//     "formatVersion": 1,
//     "name": "My Game",
//     "id": "3f2b8c1e-....",
//     "engineVersion": "0.1.0",
//     "entryScene": "scenes/main.minseen",
//     "scenes": ["scenes/main.minseen"],
//     "assetDirs": ["assets"],
//     "scriptDirs": ["scripts"],
//     "plugins": [{"path": "plugins/physics_extras", "abiVersion": 1}],
//     "settings": { ...free-form, owned by the project... },
//     "build": {"singleExe": false, "compress": false, "encrypt": false, "keyId": ""},
//     "integrity": "crc32:0a1b2c3d"
//   }
//
// Rules:
//  * Integrity: `integrity` is the CRC-32 of the compact JSON serialisation of the document without the
//    `integrity` key. Whitespace and formatting edits therefore do not matter; changing a value does.
//    A missing `integrity` is accepted (hand-written file); a present but wrong one is an error unless
//    LoadOptions::verifyIntegrity is false.
//  * Versioning: `formatVersion` is an integer. Files from a newer format are rejected (VersionMismatch)
//    rather than guessed at. Unknown top-level keys are kept in ProjectFile::extra and written back.
//  * Saving is crash-safe (temp file + rename).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/json.hpp"

namespace mingol::package {

inline constexpr int kMindevFormatVersion = 1;
inline constexpr std::size_t kMaxProjectNameLength = 128;

struct PluginRef {
  std::string path;      // relative path without extension; the platform adds .dll / .so
  int abiVersion = 1;
  friend bool operator==(const PluginRef&, const PluginRef&) = default;
};

struct BuildSettings {
  bool singleExe = false;  // pack runtime + data into one executable
  bool compress = false;   // off by default (spec)
  bool encrypt = false;    // encrypt .mingol packages with the developer key
  std::string keyId;       // identifies the developer key (never the key itself)
  friend bool operator==(const BuildSettings&, const BuildSettings&) = default;
};

struct ProjectFile {
  std::string name;
  std::string id;             // UUID string
  std::string engineVersion;  // "major.minor.patch" of the engine that last saved the file
  std::string entryScene;     // one of `scenes`, or empty when there are no scenes
  std::vector<std::string> scenes;
  std::vector<std::string> assetDirs;
  std::vector<std::string> scriptDirs;
  std::vector<PluginRef> plugins;
  json::Object settings;
  BuildSettings build;
  json::Object extra;  // unknown top-level keys, preserved across load/save
};

struct LoadOptions {
  bool verifyIntegrity = true;
};

// Creates a new project with a fresh id and defaults (assets/, scripts/, no scenes).
ProjectFile makeNewProject(std::string name, std::string engineVersion);

// True if `path` is a safe project-relative path: non-empty, '/'-separated, no leading '/', no drive
// letter, no '.', '..' or empty segments, no backslashes or control characters.
bool isValidRelativePath(std::string_view path) noexcept;

// Checks every field and cross-references (entryScene in scenes, unique paths, valid id, ...).
Result<void> validate(const ProjectFile& project);

Result<json::Value> toJson(const ProjectFile& project);  // includes `integrity`
Result<ProjectFile> fromJson(const json::Value& doc, const LoadOptions& options = {});

Result<std::string> serialize(const ProjectFile& project);
Result<ProjectFile> parseMindev(std::string_view text, const LoadOptions& options = {});

Result<ProjectFile> loadMindev(const std::filesystem::path& path, const LoadOptions& options = {});
Result<void> saveMindev(const std::filesystem::path& path, const ProjectFile& project);

}  // namespace mingol::package
