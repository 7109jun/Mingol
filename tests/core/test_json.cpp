// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "mingol/core/json.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using mingol::test::fail;

namespace {

struct Lcg {
  std::uint64_t s;
  std::uint32_t next() {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>(s >> 33);
  }
  std::uint32_t below(std::uint32_t n) { return next() % n; }
};

bool parses(std::string_view t) { return json::parse(t).ok(); }
std::string compact(const json::Value& v) { return json::write(v, {.pretty = false}).value(); }

std::string randomString(Lcg& r) {
  static const char* pieces[] = {"a", "Z", " ", "\"", "\\", "/", "\n", "\t", "\x01", "\x7f", "한", "é", "\xF0\x9F\x98\x80", "{", "[", ","};
  std::string s;
  const auto n = r.below(8);
  for (std::uint32_t i = 0; i < n; ++i) s += pieces[r.below(sizeof pieces / sizeof *pieces)];
  return s;
}

json::Value randomValue(Lcg& r, int depth) {
  switch (r.below(depth > 3 ? 5 : 7)) {
    case 0: return json::Value(nullptr);
    case 1: return json::Value(r.below(2) == 0);
    case 2: return json::Value(static_cast<std::int64_t>(static_cast<std::int64_t>(r.next()) * (r.below(2) ? -1 : 1) * (r.below(3) == 0 ? 4294967296ll : 1)));
    case 3: {
      std::uint64_t bits = (static_cast<std::uint64_t>(r.next()) << 32) | r.next();
      double d;
      std::memcpy(&d, &bits, sizeof d);
      if (!std::isfinite(d)) d = 1.5;
      return json::Value(d);
    }
    case 4: return json::Value(randomString(r));
    case 5: {
      json::Array a;
      const auto n = r.below(4);
      for (std::uint32_t i = 0; i < n; ++i) a.push_back(randomValue(r, depth + 1));
      return json::Value(std::move(a));
    }
    default: {
      json::Object o;
      const auto n = r.below(4);
      for (std::uint32_t i = 0; i < n; ++i) o.set("k" + std::to_string(r.below(6)) + randomString(r), randomValue(r, depth + 1));
      return json::Value(std::move(o));
    }
  }
}

}  // namespace

TEST(Json, ParsesScalarsAndKinds) {
  CHECK(json::parse("null").value().isNull());
  CHECK(json::parse("true").value().asBool());
  CHECK(!json::parse(" false ").value().asBool());
  CHECK(json::parse("42").value().isInt());
  CHECK_EQ(json::parse("42").value().asInt(), 42);
  CHECK_EQ(json::parse("-7").value().asInt(), -7);
  CHECK(json::parse("1.0").value().isFloat());
  CHECK(json::parse("1e2").value().isFloat());
  CHECK_EQ(json::parse("1e2").value().asFloat(), 100.0);
  CHECK_EQ(json::parse("-2.5E-1").value().asFloat(), -0.25);
  CHECK_EQ(json::parse("9223372036854775807").value().asInt(), std::numeric_limits<std::int64_t>::max());
  CHECK_EQ(json::parse("-9223372036854775808").value().asInt(), std::numeric_limits<std::int64_t>::min());
  // beyond int64: becomes a float rather than failing
  CHECK(json::parse("9223372036854775808").value().isFloat());
  CHECK(!(json::parse("1") .value() == json::parse("1.0").value()));
  CHECK(json::parse("1").value().toDouble() == 1.0);
}

TEST(Json, RejectsMalformedNumbers) {
  for (const char* t : {"01", "-01", "1.", ".5", "+1", "-", "1e", "1e+", "0x10", "NaN", "Infinity", "-Infinity", "1.5.2", "--1", "1e999", "- 1"})
    CHECK_MSG(!parses(t), t);
}

TEST(Json, StringsAndEscapes) {
  CHECK_EQ(json::parse(R"("a\"b\\c\/d\b\f\n\r\t")").value().asString(), "a\"b\\c/d\b\f\n\r\t");
  CHECK_EQ(json::parse(R"("\u0041\u00e9\ud55c")").value().asString(), "A\xC3\xA9\xED\x95\x9C");
  CHECK_EQ(json::parse(R"("\ud83d\ude00")").value().asString(), "\xF0\x9F\x98\x80");  // surrogate pair
  CHECK_EQ(json::parse("\"raw \xED\x95\x9C utf8\"").value().asString(), "raw \xED\x95\x9C utf8");
  CHECK_EQ(json::parse(R"("\u0000")").value().asString().size(), 1u);  // embedded NUL is legal
}

TEST(Json, RejectsMalformedStrings) {
  for (const char* t : {"\"abc", "\"a\nb\"", "\"a\tb\"", R"("\x41")", R"("\u12")", R"("\u12G4")", R"("\ud800")", R"("\ud800x")", R"("\ud800\u0041")",
                        R"("\udc00")", R"("\)", "'single'", "\"\xC3\"", "\"\xC0\x80\"", "\"\xED\xA0\x80\"", "\"\xF4\x90\x80\x80\"", "\"\xFF\""})
    CHECK_MSG(!parses(t), t);
}

TEST(Json, RejectsMalformedStructure) {
  for (const char* t : {"", "   ", "[1,]", "[,1]", "[1 2]", "{\"a\":1,}", "{\"a\" 1}", "{a:1}", "{\"a\":}", "[1", "{\"a\":1", "// c\n1", "/* c */ 1",
                        "1 2", "[] []", "{\"a\":1}x", "tru", "nul", "True", "{\"a\":1,\"a\":2}", "{1:2}", "[\"a\" \"b\"]", "}", "]"})
    CHECK_MSG(!parses(t), t);
}

TEST(Json, StructureParsesAndKeepsOrder) {
  auto v = json::parse(R"({"z":1,"a":[1,2,{"x":null}],"m":{}})");
  REQUIRE(v.ok());
  const auto& o = v.value().asObject();
  CHECK_EQ(o.size(), 3u);
  CHECK_EQ(o.keyAt(0), "z");
  CHECK_EQ(o.keyAt(1), "a");
  CHECK_EQ(o.keyAt(2), "m");
  CHECK_EQ(o.find("a")->asArray().size(), 3u);
  CHECK(o.find("a")->asArray()[2].asObject().find("x")->isNull());
  CHECK(o.find("missing") == nullptr);
  CHECK_EQ(compact(v.value()), R"({"z":1,"a":[1,2,{"x":null}],"m":{}})");
}

TEST(Json, ObjectSetReplacesInPlaceAndErase) {
  json::Object o;
  o.set("a", 1);
  o.set("b", 2);
  o.set("a", 3);  // replace keeps position
  CHECK_EQ(o.size(), 2u);
  CHECK_EQ(o.keyAt(0), "a");
  CHECK_EQ(o.find("a")->asInt(), 3);
  CHECK(o.erase("a"));
  CHECK(!o.erase("a"));
  CHECK_EQ(o.keyAt(0), "b");
}

TEST(Json, BomIsSkippedWhitespaceAllowed) {
  CHECK(parses("\xEF\xBB\xBF{}"));
  CHECK(parses(" \t\r\n[ 1 , 2 ] \n"));
  CHECK(!parses("\xEF\xBB\xBF\xEF\xBB\xBF{}"));
}

TEST(Json, ErrorsCarryLineAndColumn) {
  auto r = json::parse("{\n  \"a\": 1,\n  \"b\": ?\n}");
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::Corrupt);
  CHECK_MSG(r.error().message.rfind("3:8:", 0) == 0, r.error().message);
  auto dup = json::parse("{\"a\":1,\n\"a\":2}");
  REQUIRE(!dup.ok());
  CHECK_MSG(dup.error().message.find("duplicate key") != std::string::npos, dup.error().message);
}

TEST(Json, DepthLimitAndNoStackOverflow) {
  auto nest = [](int n) { return std::string(static_cast<std::size_t>(n), '[') + std::string(static_cast<std::size_t>(n), ']'); };
  CHECK(parses(nest(200)));
  CHECK(!parses(nest(300)));
  CHECK(!parses(std::string(200000, '[')));  // must fail cleanly, not crash
  CHECK(!parses(std::string(200000, '{')));
  std::string objs;
  for (int i = 0; i < 100000; ++i) objs += "{\"a\":";
  CHECK(!parses(objs));
  CHECK(json::parse(nest(10), {.maxDepth = 5}).ok() == false);
}

TEST(Json, WriterFormatsPrettyAndCompact) {
  auto v = json::parse(R"({"a":[1,2.5,"x"],"b":{},"c":[],"d":{"e":true}})").value();
  CHECK_EQ(compact(v), R"({"a":[1,2.5,"x"],"b":{},"c":[],"d":{"e":true}})");
  const std::string pretty = json::write(v).value();
  CHECK_EQ(pretty,
           "{\n"
           "  \"a\": [\n"
           "    1,\n"
           "    2.5,\n"
           "    \"x\"\n"
           "  ],\n"
           "  \"b\": {},\n"
           "  \"c\": [],\n"
           "  \"d\": {\n"
           "    \"e\": true\n"
           "  }\n"
           "}\n");
}

TEST(Json, WriterEscapesAndKeepsFloatsFloats) {
  CHECK_EQ(compact(json::Value(std::string("a\"\\\n\t\x01\x1f\x7f"))), R"("a\"\\\n\t\u0001\u001f\u007f")");
  CHECK_EQ(compact(json::Value("한é")), "\"한é\"");  // non-ASCII stays raw UTF-8
  CHECK_EQ(compact(json::Value(2.0)), "2.0");
  CHECK_EQ(compact(json::Value(1e300)), "1e+300");
  CHECK_EQ(compact(json::Value(-0.0)), "-0.0");
  CHECK_EQ(compact(json::Value(std::int64_t{5})), "5");
  CHECK(json::parse(compact(json::Value(2.0))).value().isFloat());
}

TEST(Json, WriterRejectsNonFiniteInvalidUtf8AndDepth) {
  CHECK(!json::write(json::Value(std::numeric_limits<double>::quiet_NaN())).ok());
  CHECK(!json::write(json::Value(std::numeric_limits<double>::infinity())).ok());
  CHECK(!json::write(json::Value(std::string("\xFF"))).ok());
  CHECK(!json::write(json::Value(std::string("\xED\xA0\x80"))).ok());
  json::Value deep(json::Array{});
  for (int i = 0; i < 20; ++i) {
    json::Array a;
    a.push_back(std::move(deep));
    deep = json::Value(std::move(a));
  }
  CHECK(json::write(deep, {.pretty = false, .maxDepth = 10}).error().code == ErrorCode::InvalidArgument);
  CHECK(json::write(deep, {.pretty = false}).ok());
}

TEST(Json, DoublesRoundTripBitExactly) {
  Lcg r{7};
  for (int i = 0; i < 20000; ++i) {
    std::uint64_t bits = (static_cast<std::uint64_t>(r.next()) << 32) | r.next();
    double d;
    std::memcpy(&d, &bits, sizeof d);
    if (!std::isfinite(d)) continue;
    auto back = json::parse(compact(json::Value(d)));
    REQUIRE(back.ok());
    REQUIRE(back.value().isFloat());
    std::uint64_t b2;
    const double got = back.value().asFloat();
    std::memcpy(&b2, &got, sizeof b2);
    if (b2 != bits) { fail(__FILE__, __LINE__, "double did not round-trip: " + compact(json::Value(d))); return; }
  }
}

TEST(Json, RandomDocumentsRoundTripPrettyAndCompact) {
  Lcg r{12345};
  for (int i = 0; i < 3000; ++i) {
    json::Value v = randomValue(r, 0);
    for (bool pretty : {true, false}) {
      auto text = json::write(v, {.pretty = pretty});
      REQUIRE(text.ok());
      auto back = json::parse(text.value());
      if (!back.ok()) { fail(__FILE__, __LINE__, "reparse failed: " + back.error().message + "\n" + text.value()); return; }
      if (!(back.value() == v)) { fail(__FILE__, __LINE__, "round trip changed the value:\n" + text.value()); return; }
      // writing is deterministic and idempotent
      CHECK_EQ(json::write(back.value(), {.pretty = pretty}).value(), text.value());
    }
  }
}

TEST(Json, FuzzedMutationsNeverCrashAndStayConsistent) {
  Lcg r{99};
  const std::string base = json::write(json::parse(R"({"name":"Mingol","list":[1,2.5,-3e2,"x\n\u00e9",null,true],"o":{"k":[{}],"z":""}})").value()).value();
  int accepted = 0;
  for (int i = 0; i < 20000; ++i) {
    std::string t = base;
    const auto edits = 1 + r.below(4);
    for (std::uint32_t e = 0; e < edits && !t.empty(); ++e) {
      const auto pos = r.below(static_cast<std::uint32_t>(t.size()));
      switch (r.below(4)) {
        case 0: t[pos] = static_cast<char>(r.below(256)); break;
        case 1: t.erase(pos, 1 + r.below(3)); break;
        case 2: t.insert(pos, 1, "{}[],:\"\\0e-"[r.below(11)]); break;
        default: t.resize(pos); break;
      }
    }
    auto p = json::parse(t);
    if (p.ok()) {
      ++accepted;
      auto w = json::write(p.value());
      if (!w.ok()) continue;  // e.g. an embedded \uXXXX NUL is fine, invalid UTF-8 cannot get here
      auto again = json::parse(w.value());
      if (!again.ok() || !(again.value() == p.value())) { fail(__FILE__, __LINE__, "accepted document does not round-trip: " + t); return; }
    }
  }
  CHECK(accepted > 0);
}
