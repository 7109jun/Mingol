// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/package/mindev.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

#include "mingol/core/fs.hpp"
#include "mingol/core/hash.hpp"
#include "mingol/core/uuid.hpp"

namespace mingol::package {
namespace {

constexpr const char* kFormatTag = "mindev";

bool hasControl(std::string_view s) {
  return std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}

Error corrupt(const std::string& m) { return Error(ErrorCode::Corrupt, ".mindev: " + m); }
Error invalid(const std::string& m) { return Error(ErrorCode::InvalidArgument, ".mindev: " + m); }

bool validVersion(std::string_view v) {
  // major.minor.patch, digits only
  int dots = 0;
  bool digit = false;
  for (char c : v) {
    if (c == '.') {
      if (!digit) return false;
      ++dots;
      digit = false;
    } else if (c >= '0' && c <= '9') {
      digit = true;
    } else {
      return false;
    }
  }
  return dots == 2 && digit;
}

Result<void> checkPathList(const std::vector<std::string>& v, const char* what) {
  std::set<std::string> seen;
  for (const auto& p : v) {
    if (!isValidRelativePath(p)) return invalid(std::string(what) + " has an invalid path: \"" + p + "\"");
    if (!seen.insert(p).second) return invalid(std::string(what) + " lists \"" + p + "\" twice");
  }
  return {};
}

// ---- JSON field access ------------------------------------------------------------------------------

Result<const json::Value*> need(const json::Object& o, const char* key, const char* where) {
  const json::Value* v = o.find(key);
  if (v == nullptr) return corrupt(std::string("missing field '") + where + key + "'");
  return v;
}

Result<std::string> getString(const json::Object& o, const char* key, const char* where) {
  MINGOL_TRY_ASSIGN(const json::Value* v, need(o, key, where));
  if (!v->isString()) return corrupt(std::string("field '") + where + key + "' must be a string");
  return v->asString();
}

Result<bool> getBool(const json::Object& o, const char* key, const char* where, bool def) {
  const json::Value* v = o.find(key);
  if (v == nullptr) return def;
  if (!v->isBool()) return corrupt(std::string("field '") + where + key + "' must be true or false");
  return v->asBool();
}

Result<std::vector<std::string>> getStringList(const json::Object& o, const char* key) {
  std::vector<std::string> out;
  const json::Value* v = o.find(key);
  if (v == nullptr) return out;
  if (!v->isArray()) return corrupt(std::string("field '") + key + "' must be an array of strings");
  for (const auto& e : v->asArray()) {
    if (!e.isString()) return corrupt(std::string("field '") + key + "' must contain only strings");
    out.push_back(e.asString());
  }
  return out;
}

json::Value stringList(const std::vector<std::string>& v) {
  json::Array a;
  for (const auto& s : v) a.emplace_back(s);
  return json::Value(std::move(a));
}

std::string integrityOf(const json::Object& body) {
  json::Object copy = body;
  copy.erase("integrity");
  auto text = json::write(json::Value(std::move(copy)), {.pretty = false});
  const std::string& s = text.value();
  char buf[32];
  std::snprintf(buf, sizeof buf, "crc32:%08x",
                crc32({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()}));
  return buf;
}

const char* const kKnownKeys[] = {"format", "formatVersion", "name", "id", "engineVersion", "entryScene", "scenes",
                                  "assetDirs", "scriptDirs", "plugins", "settings", "build", "integrity"};

bool isKnownKey(std::string_view k) {
  return std::any_of(std::begin(kKnownKeys), std::end(kKnownKeys), [&](const char* n) { return k == n; });
}

}  // namespace

ProjectFile makeNewProject(std::string name, std::string engineVersion) {
  ProjectFile p;
  p.name = std::move(name);
  p.id = Uuid::generate().toString();
  p.engineVersion = std::move(engineVersion);
  p.assetDirs = {"assets"};
  p.scriptDirs = {"scripts"};
  return p;
}

bool isValidRelativePath(std::string_view path) noexcept {
  if (path.empty() || path.size() > 1024) return false;
  if (path.front() == '/' || path.back() == '/') return false;
  if (path.size() >= 2 && path[1] == ':') return false;  // drive letter
  if (hasControl(path) || path.find('\\') != std::string_view::npos) return false;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = std::min(path.find('/', start), path.size());
    const std::string_view seg = path.substr(start, end - start);
    if (seg.empty() || seg == "." || seg == "..") return false;
    start = end + 1;
  }
  return true;
}

Result<void> validate(const ProjectFile& p) {
  if (p.name.empty() || p.name.size() > kMaxProjectNameLength || hasControl(p.name))
    return invalid("project name must be 1-128 characters without control characters");
  if (!Uuid::parse(p.id).has_value()) return invalid("project id is not a valid UUID");
  if (!validVersion(p.engineVersion)) return invalid("engineVersion must look like 1.2.3");
  MINGOL_TRY(checkPathList(p.scenes, "scenes"));
  MINGOL_TRY(checkPathList(p.assetDirs, "assetDirs"));
  MINGOL_TRY(checkPathList(p.scriptDirs, "scriptDirs"));
  if (p.scenes.empty()) {
    if (!p.entryScene.empty()) return invalid("entryScene is set but the project has no scenes");
  } else if (std::find(p.scenes.begin(), p.scenes.end(), p.entryScene) == p.scenes.end()) {
    return invalid("entryScene \"" + p.entryScene + "\" is not listed in scenes");
  }
  std::set<std::string> seen;
  for (const auto& pl : p.plugins) {
    if (!isValidRelativePath(pl.path)) return invalid("plugin has an invalid path: \"" + pl.path + "\"");
    if (pl.abiVersion < 1) return invalid("plugin \"" + pl.path + "\" has an invalid abiVersion");
    if (!seen.insert(pl.path).second) return invalid("plugins lists \"" + pl.path + "\" twice");
  }
  if (hasControl(p.build.keyId) || p.build.keyId.size() > 256) return invalid("build.keyId is invalid");
  if (p.build.encrypt && p.build.keyId.empty()) return invalid("build.encrypt needs a build.keyId");
  for (std::size_t i = 0; i < p.extra.size(); ++i) {
    if (isKnownKey(p.extra.keyAt(i))) return invalid("extra must not contain the known key '" + p.extra.keyAt(i) + "'");
  }
  return {};
}

Result<json::Value> toJson(const ProjectFile& p) {
  MINGOL_TRY(validate(p));
  json::Object o;
  o.set("format", kFormatTag);
  o.set("formatVersion", kMindevFormatVersion);
  o.set("name", p.name);
  o.set("id", p.id);
  o.set("engineVersion", p.engineVersion);
  o.set("entryScene", p.entryScene);
  o.set("scenes", stringList(p.scenes));
  o.set("assetDirs", stringList(p.assetDirs));
  o.set("scriptDirs", stringList(p.scriptDirs));
  json::Array plugins;
  for (const auto& pl : p.plugins) {
    json::Object po;
    po.set("path", pl.path);
    po.set("abiVersion", pl.abiVersion);
    plugins.emplace_back(std::move(po));
  }
  o.set("plugins", json::Value(std::move(plugins)));
  o.set("settings", json::Value(p.settings));
  json::Object b;
  b.set("singleExe", p.build.singleExe);
  b.set("compress", p.build.compress);
  b.set("encrypt", p.build.encrypt);
  b.set("keyId", p.build.keyId);
  o.set("build", json::Value(std::move(b)));
  for (std::size_t i = 0; i < p.extra.size(); ++i) o.set(p.extra.keyAt(i), p.extra.valueAt(i));
  o.set("integrity", integrityOf(o));
  return json::Value(std::move(o));
}

Result<ProjectFile> fromJson(const json::Value& doc, const LoadOptions& options) {
  if (!doc.isObject()) return corrupt("the document must be a JSON object");
  const json::Object& o = doc.asObject();

  MINGOL_TRY_ASSIGN(std::string tag, getString(o, "format", ""));
  if (tag != kFormatTag) return corrupt("'format' is \"" + tag + "\", expected \"mindev\"");
  MINGOL_TRY_ASSIGN(const json::Value* ver, need(o, "formatVersion", ""));
  if (!ver->isInt() || ver->asInt() < 1) return corrupt("'formatVersion' must be an integer >= 1");
  if (ver->asInt() > kMindevFormatVersion)
    return makeError(ErrorCode::VersionMismatch, ".mindev: file format version {} is newer than this engine supports ({}); update Mingol",
                     ver->asInt(), kMindevFormatVersion);

  if (options.verifyIntegrity) {
    if (const json::Value* iv = o.find("integrity")) {
      if (!iv->isString()) return corrupt("'integrity' must be a string");
      if (iv->asString() != integrityOf(o))
        return Error(ErrorCode::Corrupt, ".mindev: integrity check failed (the file was modified or damaged; expected " + integrityOf(o) + ")");
    }
  }

  ProjectFile p;
  MINGOL_TRY_ASSIGN(p.name, getString(o, "name", ""));
  MINGOL_TRY_ASSIGN(p.id, getString(o, "id", ""));
  MINGOL_TRY_ASSIGN(p.engineVersion, getString(o, "engineVersion", ""));
  if (o.contains("entryScene")) { MINGOL_TRY_ASSIGN(p.entryScene, getString(o, "entryScene", "")); }
  MINGOL_TRY_ASSIGN(p.scenes, getStringList(o, "scenes"));
  MINGOL_TRY_ASSIGN(p.assetDirs, getStringList(o, "assetDirs"));
  MINGOL_TRY_ASSIGN(p.scriptDirs, getStringList(o, "scriptDirs"));

  if (const json::Value* pv = o.find("plugins")) {
    if (!pv->isArray()) return corrupt("'plugins' must be an array");
    for (const auto& e : pv->asArray()) {
      if (!e.isObject()) return corrupt("each plugin must be an object");
      PluginRef pr;
      MINGOL_TRY_ASSIGN(pr.path, getString(e.asObject(), "path", "plugins[]."));
      MINGOL_TRY_ASSIGN(const json::Value* av, need(e.asObject(), "abiVersion", "plugins[]."));
      if (!av->isInt() || av->asInt() < 1 || av->asInt() > 1000000) return corrupt("plugin 'abiVersion' must be a positive integer");
      pr.abiVersion = static_cast<int>(av->asInt());
      p.plugins.push_back(std::move(pr));
    }
  }
  if (const json::Value* sv = o.find("settings")) {
    if (!sv->isObject()) return corrupt("'settings' must be an object");
    p.settings = sv->asObject();
  }
  if (const json::Value* bv = o.find("build")) {
    if (!bv->isObject()) return corrupt("'build' must be an object");
    const json::Object& b = bv->asObject();
    MINGOL_TRY_ASSIGN(p.build.singleExe, getBool(b, "singleExe", "build.", false));
    MINGOL_TRY_ASSIGN(p.build.compress, getBool(b, "compress", "build.", false));
    MINGOL_TRY_ASSIGN(p.build.encrypt, getBool(b, "encrypt", "build.", false));
    if (b.contains("keyId")) { MINGOL_TRY_ASSIGN(p.build.keyId, getString(b, "keyId", "build.")); }
  }
  for (std::size_t i = 0; i < o.size(); ++i) {
    if (!isKnownKey(o.keyAt(i))) p.extra.set(o.keyAt(i), o.valueAt(i));
  }
  MINGOL_TRY(validate(p));
  return p;
}

Result<std::string> serialize(const ProjectFile& project) {
  MINGOL_TRY_ASSIGN(json::Value doc, toJson(project));
  return json::write(doc);
}

Result<ProjectFile> parseMindev(std::string_view text, const LoadOptions& options) {
  auto doc = json::parse(text);
  if (!doc.ok()) return Error(ErrorCode::Corrupt, ".mindev: " + doc.error().message);
  return fromJson(doc.value(), options);
}

Result<ProjectFile> loadMindev(const std::filesystem::path& path, const LoadOptions& options) {
  MINGOL_TRY_ASSIGN(std::string text, fs::readTextFile(path, 64ull << 20));
  return parseMindev(text, options);
}

Result<void> saveMindev(const std::filesystem::path& path, const ProjectFile& project) {
  MINGOL_TRY_ASSIGN(std::string text, serialize(project));
  return fs::writeTextFileAtomic(path, text);
}

}  // namespace mingol::package
