// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/json.hpp"

#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace mingol::json {

bool operator==(const Value& a, const Value& b) {
  if (a.v_.index() != b.v_.index()) return false;
  switch (a.v_.index()) {
    case 0: return true;
    case 1: return std::get<1>(a.v_) == std::get<1>(b.v_);
    case 2: return std::get<2>(a.v_) == std::get<2>(b.v_);
    case 3: {
      const double x = std::get<3>(a.v_), y = std::get<3>(b.v_);
      return x == y && std::signbit(x) == std::signbit(y);
    }
    case 4: return std::get<4>(a.v_) == std::get<4>(b.v_);
    case 5: return std::get<5>(a.v_) == std::get<5>(b.v_);
    default: {
      const Object& x = std::get<6>(a.v_);
      const Object& y = std::get<6>(b.v_);
      if (x.size() != y.size()) return false;
      for (std::size_t i = 0; i < x.size(); ++i) {
        if (x.keyAt(i) != y.keyAt(i) || !(x.valueAt(i) == y.valueAt(i))) return false;
      }
      return true;
    }
  }
}

namespace {

// Decodes one UTF-8 sequence at s[i]; returns its length or 0 if invalid (overlong, surrogate, > U+10FFFF).
std::size_t utf8Length(std::string_view s, std::size_t i) {
  const auto b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) return 1;
  auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
  if (b0 >= 0xC2 && b0 <= 0xDF) return cont(1) ? 2 : 0;
  if (b0 >= 0xE0 && b0 <= 0xEF) {
    if (!cont(1) || !cont(2)) return 0;
    const auto b1 = static_cast<unsigned char>(s[i + 1]);
    if (b0 == 0xE0 && b1 < 0xA0) return 0;
    if (b0 == 0xED && b1 >= 0xA0) return 0;  // surrogates
    return 3;
  }
  if (b0 >= 0xF0 && b0 <= 0xF4) {
    if (!cont(1) || !cont(2) || !cont(3)) return 0;
    const auto b1 = static_cast<unsigned char>(s[i + 1]);
    if (b0 == 0xF0 && b1 < 0x90) return 0;
    if (b0 == 0xF4 && b1 >= 0x90) return 0;
    return 4;
  }
  return 0;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

class Parser {
 public:
  Parser(std::string_view t, const ParseOptions& o) : s_(t), opt_(o) {}

  Result<Value> run() {
    if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB && static_cast<unsigned char>(s_[2]) == 0xBF) i_ = 3;
    skipWs();
    Value v;
    if (!parseValue(v, 0)) return err_;
    skipWs();
    if (i_ != s_.size()) return fail("unexpected data after the document");
    return v;
  }

 private:
  std::string_view s_;
  ParseOptions opt_;
  std::size_t i_ = 0;
  Error err_;

  Error fail(const std::string& why) {
    std::size_t line = 1, col = 1;
    for (std::size_t k = 0; k < i_ && k < s_.size(); ++k) {
      if (s_[k] == '\n') { ++line; col = 1; } else { ++col; }
    }
    err_ = Error(ErrorCode::Corrupt, std::to_string(line) + ":" + std::to_string(col) + ": " + why);
    return err_;
  }
  bool bad(const std::string& why) { fail(why); return false; }

  void skipWs() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
  }

  bool parseValue(Value& out, std::size_t depth) {
    if (depth > opt_.maxDepth) return bad("nesting is too deep");
    if (i_ >= s_.size()) return bad("unexpected end of input");
    const char c = s_[i_];
    switch (c) {
      case '{': return parseObject(out, depth);
      case '[': return parseArray(out, depth);
      case '"': {
        std::string str;
        if (!parseString(str)) return false;
        out = Value(std::move(str));
        return true;
      }
      case 't': return literal("true", Value(true), out);
      case 'f': return literal("false", Value(false), out);
      case 'n': return literal("null", Value(nullptr), out);
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return bad(std::string("unexpected character '") + c + "'");
    }
  }

  bool literal(std::string_view word, Value v, Value& out) {
    if (s_.substr(i_, word.size()) != word) return bad("invalid literal");
    i_ += word.size();
    out = std::move(v);
    return true;
  }

  bool parseNumber(Value& out) {
    const std::size_t start = i_;
    if (s_[i_] == '-') ++i_;
    if (i_ >= s_.size()) return bad("incomplete number");
    if (s_[i_] == '0') {
      ++i_;
      if (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') return bad("leading zeros are not allowed");
    } else if (s_[i_] >= '1' && s_[i_] <= '9') {
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    } else {
      return bad("invalid number");
    }
    bool isFloat = false;
    if (i_ < s_.size() && s_[i_] == '.') {
      isFloat = true;
      ++i_;
      if (i_ >= s_.size() || s_[i_] < '0' || s_[i_] > '9') return bad("digits expected after '.'");
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      isFloat = true;
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      if (i_ >= s_.size() || s_[i_] < '0' || s_[i_] > '9') return bad("digits expected in exponent");
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
    }
    const char* b = s_.data() + start;
    const char* e = s_.data() + i_;
    if (!isFloat) {
      std::int64_t iv = 0;
      auto r = std::from_chars(b, e, iv);
      if (r.ec == std::errc() && r.ptr == e) {
        out = Value(iv);
        return true;
      }
      // out of int64 range: fall through to a double
    }
    double dv = 0;
    auto r = std::from_chars(b, e, dv);
    if (r.ec == std::errc::result_out_of_range || (r.ec == std::errc() && !std::isfinite(dv))) return bad("number is out of range");
    if (r.ec != std::errc() || r.ptr != e) return bad("invalid number");
    out = Value(dv);
    return true;
  }

  bool hex4(std::uint32_t& cp) {
    if (i_ + 4 > s_.size()) return bad("incomplete \\u escape");
    cp = 0;
    for (int k = 0; k < 4; ++k) {
      const char h = s_[i_ + static_cast<std::size_t>(k)];
      cp <<= 4;
      if (h >= '0' && h <= '9') cp |= static_cast<std::uint32_t>(h - '0');
      else if (h >= 'a' && h <= 'f') cp |= static_cast<std::uint32_t>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') cp |= static_cast<std::uint32_t>(h - 'A' + 10);
      else return bad("invalid \\u escape");
    }
    i_ += 4;
    return true;
  }

  bool parseString(std::string& out) {
    ++i_;  // opening quote
    while (true) {
      if (i_ >= s_.size()) return bad("unterminated string");
      const auto c = static_cast<unsigned char>(s_[i_]);
      if (c == '"') { ++i_; return true; }
      if (c < 0x20) return bad("control character in string");
      if (c == '\\') {
        ++i_;
        if (i_ >= s_.size()) return bad("unterminated escape");
        const char e = s_[i_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            std::uint32_t cp = 0;
            if (!hex4(cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (i_ + 2 > s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') return bad("high surrogate without a low surrogate");
              i_ += 2;
              std::uint32_t lo = 0;
              if (!hex4(lo)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF) return bad("invalid low surrogate");
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              return bad("lone low surrogate");
            }
            appendUtf8(out, cp);
            break;
          }
          default: --i_; return bad("invalid escape sequence");
        }
        continue;
      }
      const std::size_t n = utf8Length(s_, i_);
      if (n == 0) return bad("invalid UTF-8");
      out.append(s_.data() + i_, n);
      i_ += n;
    }
  }

  bool parseArray(Value& out, std::size_t depth) {
    ++i_;
    Array arr;
    skipWs();
    if (i_ < s_.size() && s_[i_] == ']') { ++i_; out = Value(std::move(arr)); return true; }
    while (true) {
      skipWs();
      Value v;
      if (!parseValue(v, depth + 1)) return false;
      arr.push_back(std::move(v));
      skipWs();
      if (i_ >= s_.size()) return bad("unterminated array");
      if (s_[i_] == ',') { ++i_; continue; }
      if (s_[i_] == ']') { ++i_; break; }
      return bad("expected ',' or ']'");
    }
    out = Value(std::move(arr));
    return true;
  }

  bool parseObject(Value& out, std::size_t depth) {
    ++i_;
    Object obj;
    std::unordered_set<std::string> seen;
    skipWs();
    if (i_ < s_.size() && s_[i_] == '}') { ++i_; out = Value(std::move(obj)); return true; }
    while (true) {
      skipWs();
      if (i_ >= s_.size() || s_[i_] != '"') return bad("expected a string key");
      std::string key;
      if (!parseString(key)) return false;
      if (!seen.insert(key).second) return bad("duplicate key \"" + key + "\"");
      skipWs();
      if (i_ >= s_.size() || s_[i_] != ':') return bad("expected ':'");
      ++i_;
      skipWs();
      Value v;
      if (!parseValue(v, depth + 1)) return false;
      obj.appendUnique(std::move(key), std::move(v));
      skipWs();
      if (i_ >= s_.size()) return bad("unterminated object");
      if (s_[i_] == ',') { ++i_; continue; }
      if (s_[i_] == '}') { ++i_; break; }
      return bad("expected ',' or '}'");
    }
    out = Value(std::move(obj));
    return true;
  }
};

class Writer {
 public:
  Writer(const WriteOptions& o) : opt_(o) {}
  Result<std::string> run(const Value& v) {
    MINGOL_TRY(emit(v, 0));
    if (opt_.pretty) out_.push_back('\n');
    return std::move(out_);
  }

 private:
  const WriteOptions& opt_;
  std::string out_;

  void newline(std::size_t depth) {
    if (!opt_.pretty) return;
    out_.push_back('\n');
    out_.append(depth * 2, ' ');
  }

  Result<void> emitString(const std::string& s) {
    out_.push_back('"');
    for (std::size_t i = 0; i < s.size();) {
      const auto c = static_cast<unsigned char>(s[i]);
      if (c < 0x80) {
        switch (c) {
          case '"': out_ += "\\\""; break;
          case '\\': out_ += "\\\\"; break;
          case '\b': out_ += "\\b"; break;
          case '\f': out_ += "\\f"; break;
          case '\n': out_ += "\\n"; break;
          case '\r': out_ += "\\r"; break;
          case '\t': out_ += "\\t"; break;
          default:
            if (c < 0x20 || c == 0x7F) {
              static const char* hex = "0123456789abcdef";
              out_ += "\\u00";
              out_.push_back(hex[c >> 4]);
              out_.push_back(hex[c & 15]);
            } else {
              out_.push_back(static_cast<char>(c));
            }
        }
        ++i;
      } else {
        const std::size_t n = utf8Length(s, i);
        if (n == 0) return Error(ErrorCode::InvalidArgument, "string is not valid UTF-8");
        out_.append(s, i, n);
        i += n;
      }
    }
    out_.push_back('"');
    return {};
  }

  Result<void> emit(const Value& v, std::size_t depth) {
    if (depth > opt_.maxDepth) return Error(ErrorCode::InvalidArgument, "document is nested too deeply");
    if (v.isNull()) out_ += "null";
    else if (v.isBool()) out_ += v.asBool() ? "true" : "false";
    else if (v.isInt()) out_ += std::to_string(v.asInt());
    else if (v.isFloat()) {
      const double d = v.asFloat();
      if (!std::isfinite(d)) return Error(ErrorCode::InvalidArgument, "NaN and Infinity cannot be written as JSON");
      char buf[64];
      auto r = std::to_chars(buf, buf + sizeof buf, d);
      std::string t(buf, r.ptr);
      if (t.find_first_of(".eE") == std::string::npos) t += ".0";
      out_ += t;
    } else if (v.isString()) {
      MINGOL_TRY(emitString(v.asString()));
    } else if (v.isArray()) {
      const Array& a = v.asArray();
      if (a.empty()) { out_ += "[]"; return {}; }
      out_.push_back('[');
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (i != 0) out_.push_back(',');
        newline(depth + 1);
        MINGOL_TRY(emit(a[i], depth + 1));
      }
      newline(depth);
      out_.push_back(']');
    } else {
      const Object& o = v.asObject();
      if (o.empty()) { out_ += "{}"; return {}; }
      out_.push_back('{');
      for (std::size_t i = 0; i < o.size(); ++i) {
        if (i != 0) out_.push_back(',');
        newline(depth + 1);
        MINGOL_TRY(emitString(o.keyAt(i)));
        out_ += opt_.pretty ? ": " : ":";
        MINGOL_TRY(emit(o.valueAt(i), depth + 1));
      }
      newline(depth);
      out_.push_back('}');
    }
    return {};
  }
};

}  // namespace

Result<Value> parse(std::string_view text, const ParseOptions& options) { return Parser(text, options).run(); }

Result<std::string> write(const Value& value, const WriteOptions& options) { return Writer(options).run(value); }

}  // namespace mingol::json
