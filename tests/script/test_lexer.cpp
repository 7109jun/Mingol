// SPDX-License-Identifier: GPL-2.0-only
#include <climits>
#include <cmath>

#include "mingol/script/lexer.hpp"
#include "script_test_util.hpp"

using namespace mingol;
using namespace mingol::script;
using namespace mingol::test;

namespace {
struct Lexed {
  std::vector<Token> toks;
  Diagnostics diags;
};
Lexed lexText(std::string_view s) {
  Lexed l;
  l.diags.addFile("t.mins");
  l.toks = lex(s, 0, l.diags);
  return l;
}
std::string kinds(const Lexed& l) {
  std::string s;
  for (const Token& t : l.toks) {
    if (t.kind == Tok::End) break;
    s += std::string(s.empty() ? "" : " ") + tokName(t.kind);
  }
  return s;
}
}  // namespace

TEST(ScriptLexer, KeywordsIdentifiersAndPunctuation) {
  auto l = lexText("class Foo extends Bar { int x = 1; }");
  CHECK(!l.diags.hasErrors());
  CHECK_EQ(kinds(l), std::string("'class' identifier 'extends' identifier '{' 'int' identifier '=' integer literal ';' '}'"));
  CHECK(l.toks.back().kind == Tok::End);
  // Keywords are matched whole: prefixes are identifiers.
  auto m = lexText("classy iffy intx _x x_1 ünï");
  CHECK_EQ(kinds(m), std::string("identifier identifier identifier identifier identifier identifier"));
  CHECK_EQ(m.toks[5].text, std::string("ünï"));
}

TEST(ScriptLexer, OperatorsAreMaximalMunch) {
  auto l = lexText("<<= >>= << >> <= >= == != && || ++ -- += -= *= /= %= &= |= ^= => < > = ! ~ & | ^");
  CHECK(!l.diags.hasErrors());
  CHECK_EQ(kinds(l), std::string("'<<=' '>>=' '<<' '>>' '<=' '>=' '==' '!=' '&&' '||' '++' '--' '+=' '-=' '*=' '/=' '%=' '&=' '|=' '^=' '=>' '<' '>' '=' '!' '~' '&' '|' '^'"));
  // a+++b is a ++ + b
  CHECK_EQ(kinds(lexText("a+++b")), std::string("identifier '++' '+' identifier"));
}

TEST(ScriptLexer, IntegerLiterals) {
  auto l = lexText("0 42 1_000_000 0xFF 0Xab_cd 0b1010 0b1_0 9223372036854775807");
  CHECK(!l.diags.hasErrors());
  const std::int64_t want[] = {0, 42, 1000000, 255, 0xabcd, 10, 2, INT64_MAX};
  REQUIRE(l.toks.size() == 9);
  for (int i = 0; i < 8; ++i) CHECK_EQ(l.toks[static_cast<std::size_t>(i)].ival, want[i]);
  // Full-width bit patterns are accepted in hex and wrap to the signed value.
  auto h = lexText("0xFFFFFFFFFFFFFFFF 0x8000000000000000");
  CHECK(!h.diags.hasErrors());
  CHECK_EQ(h.toks[0].ival, std::int64_t{-1});
  CHECK_EQ(h.toks[1].ival, INT64_MIN);
}

TEST(ScriptLexer, IntegerOverflowAndMalformedNumbers) {
  CHECK(lexText("9223372036854775809").diags.hasErrors());
  CHECK(lexText("99999999999999999999999").diags.hasErrors());
  CHECK(lexText("0x1FFFFFFFFFFFFFFFF").diags.hasErrors());
  CHECK(lexText("0x").diags.hasErrors());
  CHECK(lexText("0b").diags.hasErrors());
  CHECK(lexText("0b102").diags.hasErrors());
  CHECK(lexText("12abc").diags.hasErrors());
  CHECK(lexText("0xZZ").diags.hasErrors());
  // The one decimal literal that only fits with a sign is lexed (the parser decides).
  auto m = lexText("9223372036854775808");
  CHECK(!m.diags.hasErrors());
  CHECK_EQ(m.toks[0].ival, INT64_MIN);
}

TEST(ScriptLexer, FloatLiterals) {
  auto l = lexText("1.5 0.25 1e3 2.5e-2 1E+2 3_0.5_0");
  CHECK(!l.diags.hasErrors());
  const double want[] = {1.5, 0.25, 1000.0, 0.025, 100.0, 30.5};
  REQUIRE(l.toks.size() == 7);
  for (int i = 0; i < 6; ++i) {
    CHECK(l.toks[static_cast<std::size_t>(i)].kind == Tok::FloatLit);
    CHECK_NEAR(l.toks[static_cast<std::size_t>(i)].fval, want[i], 1e-12);
  }
  // `1.` is an int followed by '.', `1.x` a member access on an int, `1e` an error.
  CHECK_EQ(kinds(lexText("1.x")), std::string("integer literal '.' identifier"));
  CHECK(lexText("1e").diags.hasErrors());
  CHECK(lexText("1e999").diags.hasErrors());
}

TEST(ScriptLexer, StringEscapes) {
  auto l = lexText(R"("a\nb\t\\\"\0\x41\u{e9}\u{1F600}")");
  CHECK(!l.diags.hasErrors());
  REQUIRE(l.toks[0].kind == Tok::StringLit);
  const std::string want = std::string("a\nb\t\\\"") + std::string(1, '\0') + "A" + "\xC3\xA9" + "\xF0\x9F\x98\x80";
  CHECK_EQ(l.toks[0].text, want);
  CHECK(l.toks[0].text.size() == want.size());
}

TEST(ScriptLexer, StringErrors) {
  CHECK(lexText("\"abc").diags.hasErrors());
  CHECK(lexText("\"abc\ndef\"").diags.hasErrors());
  CHECK(lexText("\"\\q\"").diags.hasErrors());
  CHECK(lexText("\"\\x4\"").diags.hasErrors());
  CHECK(lexText("\"\\u0041\"").diags.hasErrors());
  CHECK(lexText("\"\\u{}\"").diags.hasErrors());
  CHECK(lexText("\"\\u{D800}\"").diags.hasErrors());
  CHECK(lexText("\"\\u{110000}\"").diags.hasErrors());
  CHECK(lexText("\"abc\\").diags.hasErrors());
}

TEST(ScriptLexer, CommentsAndPositions) {
  auto l = lexText("a // c1\n/* multi\nline */ b\n  c");
  CHECK(!l.diags.hasErrors());
  REQUIRE(l.toks.size() == 4);
  CHECK_EQ(l.toks[0].loc.line, 1u);
  CHECK_EQ(l.toks[0].loc.col, 1u);
  CHECK_EQ(l.toks[1].loc.line, 3u);
  CHECK_EQ(l.toks[1].loc.col, 9u);
  CHECK_EQ(l.toks[2].loc.line, 4u);
  CHECK_EQ(l.toks[2].loc.col, 3u);
  CHECK(lexText("/* never closed").diags.hasErrors());
  CHECK(!lexText("// only a comment").diags.hasErrors());
  CHECK_EQ(lexText("/**/ x").toks.size(), std::size_t{2});
}

TEST(ScriptLexer, InvalidCharactersAreReportedAndSkipped) {
  auto l = lexText("a # b @ c `");
  CHECK_EQ(l.diags.errors().size(), std::size_t{3});
  CHECK_EQ(kinds(l), std::string("identifier identifier identifier"));
  // Embedded NUL and control bytes must not terminate or crash lexing.
  std::string s = "x";
  s += '\0';
  s += "y\x01 z";
  auto m = lexText(s);
  CHECK(m.diags.hasErrors());
  CHECK_EQ(kinds(m), std::string("identifier identifier identifier"));
}

TEST(ScriptLexer, ErrorCountIsCapped) {
  std::string s(10000, '#');
  auto l = lexText(s);
  CHECK_EQ(l.diags.errors().size(), Diagnostics::kMaxErrors);
  CHECK(l.diags.truncated());
  CHECK(l.toks.back().kind == Tok::End);
}

TEST(ScriptLexer, RandomBytesNeverCrashAndAlwaysEndWithEnd) {
  ScriptLcg rng(7);
  for (int round = 0; round < 3000; ++round) {
    std::string s;
    const std::uint32_t n = rng.below(80);
    for (std::uint32_t i = 0; i < n; ++i) {
      // Biased towards the characters that start interesting lexemes.
      static const char kInteresting[] = "\"\\/*0123456789xbe._+-<>=!&|'u{}$#\n a";
      s += rng.below(4) == 0 ? static_cast<char>(rng.below(256)) : kInteresting[rng.below(sizeof(kInteresting) - 1)];
    }
    auto l = lexText(s);
    REQUIRE(!l.toks.empty());
    CHECK(l.toks.back().kind == Tok::End);
    for (const Token& t : l.toks) {
      CHECK(t.loc.line >= 1 && t.loc.col >= 1);
      if (t.kind == Tok::FloatLit) CHECK(std::isfinite(t.fval));
    }
  }
}
