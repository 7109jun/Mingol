// SPDX-License-Identifier: GPL-2.0-only
// Negative tests: the compiler must reject invalid programs with a located diagnostic and never crash.
#include "vm_test_util.hpp"
#include <cstdlib>
#include <random>

using namespace mingol::test;

namespace {
// Compiles `src`; returns the formatted diagnostics ("" when it compiled).
std::string errorsOf(const std::string& src) {
  Diagnostics d;
  auto p = compile({{"t.mins", src}}, d);
  std::string out;
  for (const auto& e : d.errors()) out += e.format() + "\n";
  if (out.empty() && !p) out = "<null program without diagnostics>";
  return out;
}
bool rejects(const std::string& body, const std::string& needle) {
  const std::string e = errorsOf("class Main { static void f() { " + body + " } }");
  if (e.find(needle) == std::string::npos) { mingol::test::fail(__FILE__, __LINE__, "expected diagnostic '" + needle + "' but got: " + (e.empty() ? "<none>" : e)); return false; }
  return e.find("t.mins:") != std::string::npos;  // located
}
}  // namespace

TEST(Diag, TypeErrors) {
  CHECK(rejects("int x = \"s\";", "error"));
  CHECK(rejects("string s = 1;", "error"));
  CHECK(rejects("bool b = 1;", "error"));
  CHECK(rejects("int x = 1.5;", "error"));
  CHECK(rejects("int x = 1; x.foo();", "error"));
  CHECK(rejects("undefinedVar = 3;", "undefinedVar"));
  CHECK(rejects("Nope n = null;", "Nope"));
  CHECK(rejects("int x = 1; int x = 2;", "error"));
  CHECK(rejects("int[] a = new int[2]; a.nope = 1;", "nope"));
  CHECK(rejects("f(1, 2);", "error"));
  CHECK(rejects("if (1) { }", "error"));
}

TEST(Diag, FlowAndStructure) {
  CHECK(!errorsOf("class Main { static int f() { } }").empty());                       // missing return
  CHECK(!errorsOf("class Main { static void f() { return 1; } }").empty());            // value from void
  CHECK(!errorsOf("class Main { static int f() { return; } }").empty());               // no value from int
  CHECK(!errorsOf("class Main { static void f() { break; } }").empty());               // break outside loop
  CHECK(!errorsOf("class Main { static void f() { continue; } }").empty());
  CHECK(!errorsOf("class Main { static void f() { return; int x = 1; } }").empty());   // unreachable
  CHECK(!errorsOf("class Main { void f() { yield 1; } }").empty());                    // yield outside coroutine
  CHECK(!errorsOf("class Main { static void f() { this.g(); } void g() { } }").empty());
  CHECK(!errorsOf("class Main { void g() { } static void f() { g(); } }").empty());    // instance from static
}

TEST(Diag, ClassRules) {
  CHECK(!errorsOf("class A { } class A { }").empty());                                 // duplicate class
  CHECK(!errorsOf("class A extends A { }").empty());                                   // cyclic inheritance
  CHECK(!errorsOf("class A extends B { } class B extends A { }").empty());
  CHECK(!errorsOf("interface I { void m(); } class C implements I { }").empty());      // missing implementation
  CHECK(!errorsOf("abstract class A { } class Main { static void f() { new A(); } }").empty());
  CHECK(!errorsOf("class A { final int x = 1; void f() { x = 2; } }").empty());        // assign to final
  CHECK(!errorsOf("class A { int f() { return 1; } } class B extends A { override string f() { return \"\"; } }").empty());
  CHECK(!errorsOf("class A { void f() { } } class B extends A { override void g() { } }").empty());  // override nothing
  CHECK(!errorsOf("class A { private int p; } class Main { static void f() { new A().p = 1; } }").empty());
  CHECK(!errorsOf("class A { A(int x) { } } class Main { static void f() { new A(); } }").empty());
  CHECK(!errorsOf("class E extends Exception { } class Main { static void f() { throw 3; } }").empty());
}

TEST(Diag, SyntaxErrors) {
  const char* bad[] = {
    "class", "class A {", "class A { int }", "class A { void f( }", "class A { void f() { int x = ; } }",
    "class A { void f() { if } }", "class A { void f() { 1 + ; } }", "class A { void f() { \"unterminated } }",
    "class A { void f() { 'c } }", "class A { int x = 1 2; }", "}", "class A { void f() { for (;;) } }",
    "class A { void f() { x = (1 + 2; } }", "class A { void f() { new int[; } }", "enum E { A, , B }",
    "class A { void f() { 0x; } }", "class A { void f() { 1.e5; } }", "class A { void f() { \"\\q\"; } }",
    "module ; class A { }", "import ; class A { }", "class A extends { }", "class A implements { }"};
  for (const char* src : bad) {
    const auto e = errorsOf(src);
    if (e.empty()) mingol::test::fail(__FILE__, __LINE__, std::string("accepted invalid source: ") + src);
  }
}

TEST(Diag, ErrorLimitAndNoCascadeExplosion) {
  std::string src = "class Main { static void f() {\n";
  for (int i = 0; i < 500; ++i) src += "  int a" + std::to_string(i) + " = \"x\";\n";
  src += "} }";
  Diagnostics d;
  auto p = compile({{"t.mins", src}}, d);
  CHECK(d.hasErrors());
  CHECK(d.errors().size() <= 500 + 5);
  CHECK(p == nullptr);
}

TEST(Diag, DeepNestingDoesNotOverflowTheStack) {
  std::string e1 = "class Main { static int f() { return ";
  for (int i = 0; i < 5000; ++i) e1 += "(";
  e1 += "1";
  for (int i = 0; i < 5000; ++i) e1 += ")";
  e1 += "; } }";
  CHECK(!errorsOf(e1).empty());   // nesting limit diagnosed, no crash
  std::string e2 = "class Main { static void f() { ";
  for (int i = 0; i < 5000; ++i) e2 += "{";
  for (int i = 0; i < 5000; ++i) e2 += "}";
  e2 += " } }";
  CHECK(!errorsOf(e2).empty());
}

TEST(Diag, MutationFuzzNeverCrashes) {
  const std::string seed = R"(
module game;
interface Shape { float area(); }
enum Kind { A, B = 3 }
class Circle implements Shape {
  float r = 1.0; static int count;
  Circle(float r) { this.r = r; count++; }
  float area() { return 3.14 * r * r; }
  static Circle operator +(Circle a, Circle b) { return new Circle(a.r + b.r); }
}
class Main {
  static int f(int n) {
    int[] a = new int[n]; string s = "x";
    for (int i = 0; i < n; i++) { a[i] = i * 2; s += i; }
    try { if (n > 3) { throw new Exception("e"); } } catch (Exception e) { return -1; } finally { s += "!"; }
    fn(int):int g = (int v) => v + a.length;
    switch (n) { case 1, 2: return 1; default: return g(n); }
  }
  static coroutine void co() { yield 1; }
}
)";
  CHECK(errorsOf(seed).empty());
  const char* seedEnv = std::getenv("MINGOL_FUZZ_SEED");
  const char* itEnv = std::getenv("MINGOL_FUZZ_ITERS");
  std::mt19937 rng(seedEnv ? static_cast<unsigned>(std::atoi(seedEnv)) : 12345u);
  const int iterations = itEnv ? std::atoi(itEnv) : 1500;
  const std::string ins[] = {"{", "}", "(", ")", ";", "\"", "class ", "=", "=>", "new ", "int ", ".", ",", "[", "]", "/*", "0x", "1e", "'"};
  for (int iter = 0; iter < iterations; ++iter) {
    std::string s = seed;
    const int edits = 1 + static_cast<int>(rng() % 4);
    for (int k = 0; k < edits; ++k) {
      const size_t pos = rng() % (s.size() + 1);
      switch (rng() % 3) {
        case 0: if (pos < s.size()) s.erase(pos, 1 + rng() % 6); break;
        case 1: s.insert(pos, ins[rng() % (sizeof(ins) / sizeof(ins[0]))]); break;
        default: if (pos < s.size()) s[pos] = static_cast<char>(rng() % 256); break;
      }
    }
    (void)errorsOf(s);  // must neither crash nor hang; ASan/UBSan catch memory errors
  }
}
