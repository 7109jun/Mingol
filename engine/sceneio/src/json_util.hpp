// SPDX-License-Identifier: GPL-2.0-only
// Small typed accessors over json::Object with path-aware error messages (internal to sceneio).
#pragma once

#include <charconv>
#include <cmath>
#include <format>
#include <string>

#include "mingol/core/error.hpp"
#include "mingol/core/json.hpp"
#include "mingol/core/math.hpp"

namespace mingol::sceneio::ju {

inline Error corrupt(const std::string& m) { return Error(ErrorCode::Corrupt, ".minseen: " + m); }

// float -> JSON number using the shortest decimal that reads back as the same float (0.1f stays 0.1).
inline json::Value num(float f) {
  const std::string s = std::format("{}", f);
  double d = 0;
  std::from_chars(s.data(), s.data() + s.size(), d);
  return json::Value(d);
}
inline json::Value vec3(Vec3 v) { return json::Value(json::Array{num(v.x), num(v.y), num(v.z)}); }
inline json::Value quat(Quat q) { return json::Value(json::Array{num(q.x), num(q.y), num(q.z), num(q.w)}); }

inline const json::Value* find(const json::Object& o, const char* key) { return o.find(key); }

inline Result<const json::Object*> object(const json::Value& v, const std::string& where) {
  if (!v.isObject()) return corrupt(where + " must be an object");
  return &v.asObject();
}

inline Result<float> getFloat(const json::Object& o, const char* key, float def, const std::string& where) {
  const json::Value* v = o.find(key);
  if (v == nullptr) return def;
  if (!v->isNumber()) return corrupt(where + "." + key + " must be a number");
  const double d = v->toDouble();
  if (!std::isfinite(d) || std::fabs(d) > 3.0e38) return corrupt(where + "." + key + " is out of range");
  return static_cast<float>(d);
}
inline Result<bool> getBool(const json::Object& o, const char* key, bool def, const std::string& where) {
  const json::Value* v = o.find(key);
  if (v == nullptr) return def;
  if (!v->isBool()) return corrupt(where + "." + key + " must be a boolean");
  return v->asBool();
}
inline Result<std::int64_t> getInt(const json::Object& o, const char* key, std::int64_t def, const std::string& where) {
  const json::Value* v = o.find(key);
  if (v == nullptr) return def;
  if (!v->isInt()) return corrupt(where + "." + key + " must be an integer");
  return v->asInt();
}
inline Result<std::string> getString(const json::Object& o, const char* key, const std::string& def, const std::string& where) {
  const json::Value* v = o.find(key);
  if (v == nullptr) return def;
  if (!v->isString()) return corrupt(where + "." + key + " must be a string");
  return v->asString();
}
// A string that may also be null/absent (-> empty).
inline Result<std::string> getOptRef(const json::Object& o, const char* key, const std::string& where) {
  const json::Value* v = o.find(key);
  if (v == nullptr || v->isNull()) return std::string();
  if (!v->isString()) return corrupt(where + "." + key + " must be a string or null");
  return v->asString();
}

inline Result<std::vector<float>> getFloats(const json::Object& o, const char* key, std::size_t n, const std::string& where) {
  const json::Value* v = o.find(key);
  if (v == nullptr || !v->isArray() || v->asArray().size() != n) return corrupt(where + "." + key + " must be an array of " + std::to_string(n) + " numbers");
  std::vector<float> out;
  for (const auto& e : v->asArray()) {
    if (!e.isNumber() || !std::isfinite(e.toDouble()) || std::fabs(e.toDouble()) > 3.0e38) return corrupt(where + "." + key + " contains an invalid number");
    out.push_back(static_cast<float>(e.toDouble()));
  }
  return out;
}
inline Result<Vec3> getVec3(const json::Object& o, const char* key, Vec3 def, bool required, const std::string& where) {
  if (!required && !o.contains(key)) return def;
  MINGOL_TRY_ASSIGN(auto f, getFloats(o, key, 3, where));
  return Vec3(f[0], f[1], f[2]);
}
inline Result<Quat> getQuat(const json::Object& o, const char* key, Quat def, bool required, const std::string& where) {
  if (!required && !o.contains(key)) return def;
  MINGOL_TRY_ASSIGN(auto f, getFloats(o, key, 4, where));
  return Quat(f[0], f[1], f[2], f[3]);
}

}  // namespace mingol::sceneio::ju
