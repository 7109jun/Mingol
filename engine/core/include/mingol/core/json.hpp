// SPDX-License-Identifier: GPL-2.0-only
// A small, strict JSON (RFC 8259) document model, parser and writer. Used by the text file formats
// (.mindev, and later .minseen). Design points:
//  * integers and floats stay distinct: a literal without '.', 'e' or 'E' that fits in int64 is an Int;
//  * objects keep insertion order, so saved files are stable and diff-friendly;
//  * duplicate object keys, trailing commas, comments, lone surrogates and invalid UTF-8 are errors;
//  * nesting depth is limited (default 256) so hostile input cannot overflow the stack.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mingol/core/error.hpp"

namespace mingol::json {

class Value;
using Array = std::vector<Value>;

// Insertion-ordered string -> Value map (linear lookup: documents here are small).
class Object {
 public:
  std::size_t size() const noexcept { return keys_.size(); }
  bool empty() const noexcept { return keys_.empty(); }
  const std::string& keyAt(std::size_t i) const { return keys_[i]; }
  const Value& valueAt(std::size_t i) const { return vals_[i]; }
  Value& valueAt(std::size_t i) { return vals_[i]; }

  const Value* find(std::string_view key) const noexcept;
  Value* find(std::string_view key) noexcept;
  bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }
  // Inserts, or replaces the value of an existing key (keeping its position). Returns the stored value.
  Value& set(std::string key, Value value);
  bool erase(std::string_view key);
  // Appends without a lookup; the caller guarantees the key is not present (used by the parser).
  void appendUnique(std::string key, Value value);

 private:
  std::vector<std::string> keys_;
  std::vector<Value> vals_;
};

class Value {
 public:
  Value() noexcept : v_(std::nullptr_t{}) {}
  Value(std::nullptr_t) noexcept : v_(std::nullptr_t{}) {}
  Value(bool b) noexcept : v_(b) {}
  Value(int i) noexcept : v_(static_cast<std::int64_t>(i)) {}
  Value(std::int64_t i) noexcept : v_(i) {}
  Value(double d) noexcept : v_(d) {}
  Value(const char* s) : v_(std::string(s)) {}
  Value(std::string s) : v_(std::move(s)) {}
  Value(std::string_view s) : v_(std::string(s)) {}
  Value(Array a) : v_(std::move(a)) {}
  Value(Object o) : v_(std::move(o)) {}

  bool isNull() const noexcept { return v_.index() == 0; }
  bool isBool() const noexcept { return v_.index() == 1; }
  bool isInt() const noexcept { return v_.index() == 2; }
  bool isFloat() const noexcept { return v_.index() == 3; }
  bool isNumber() const noexcept { return isInt() || isFloat(); }
  bool isString() const noexcept { return v_.index() == 4; }
  bool isArray() const noexcept { return v_.index() == 5; }
  bool isObject() const noexcept { return v_.index() == 6; }

  // Wrong-type access is a programming error and aborts (check with is*() first).
  bool asBool() const { MINGOL_VERIFY(isBool()); return std::get<1>(v_); }
  std::int64_t asInt() const { MINGOL_VERIFY(isInt()); return std::get<2>(v_); }
  double asFloat() const { MINGOL_VERIFY(isFloat()); return std::get<3>(v_); }
  double toDouble() const { MINGOL_VERIFY(isNumber()); return isInt() ? static_cast<double>(std::get<2>(v_)) : std::get<3>(v_); }
  const std::string& asString() const { MINGOL_VERIFY(isString()); return std::get<4>(v_); }
  const Array& asArray() const { MINGOL_VERIFY(isArray()); return std::get<5>(v_); }
  Array& asArray() { MINGOL_VERIFY(isArray()); return std::get<5>(v_); }
  const Object& asObject() const { MINGOL_VERIFY(isObject()); return std::get<6>(v_); }
  Object& asObject() { MINGOL_VERIFY(isObject()); return std::get<6>(v_); }

  friend bool operator==(const Value& a, const Value& b);

 private:
  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> v_;
};

inline const Value* Object::find(std::string_view key) const noexcept {
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) return &vals_[i];
  }
  return nullptr;
}
inline Value* Object::find(std::string_view key) noexcept {
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) return &vals_[i];
  }
  return nullptr;
}
inline Value& Object::set(std::string key, Value value) {
  if (Value* v = find(key)) {
    *v = std::move(value);
    return *v;
  }
  keys_.push_back(std::move(key));
  vals_.push_back(std::move(value));
  return vals_.back();
}
inline void Object::appendUnique(std::string key, Value value) {
  keys_.push_back(std::move(key));
  vals_.push_back(std::move(value));
}
inline bool Object::erase(std::string_view key) {
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(i));
      vals_.erase(vals_.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  return false;
}

// Structural equality; Int and Float are different kinds (1 != 1.0); object key order is significant.
bool operator==(const Value& a, const Value& b);

struct ParseOptions {
  std::size_t maxDepth = 256;
};

// Parses exactly one JSON document (surrounding whitespace allowed). A leading UTF-8 BOM is skipped.
// Errors are ErrorCode::Corrupt with "line:column: reason".
Result<Value> parse(std::string_view text, const ParseOptions& options = {});

struct WriteOptions {
  bool pretty = true;     // 2-space indentation and newlines; false = compact single line
  std::size_t maxDepth = 256;
};

// Serialises deterministically. Fails (InvalidArgument) on NaN/Infinity, invalid UTF-8 in a string,
// or nesting deeper than maxDepth. Pretty output ends with a newline.
Result<std::string> write(const Value& value, const WriteOptions& options = {});

}  // namespace mingol::json
