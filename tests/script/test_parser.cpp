// SPDX-License-Identifier: GPL-2.0-only
#include "script_test_util.hpp"

using namespace mingol;
using namespace mingol::script;
using namespace mingol::test;

TEST(ScriptParser, ArithmeticPrecedenceAndAssociativity) {
  CHECK_EQ(exprOf("1 + 2 * 3"), std::string("(+ 1 (* 2 3))"));
  CHECK_EQ(exprOf("1 * 2 + 3"), std::string("(+ (* 1 2) 3)"));
  CHECK_EQ(exprOf("1 - 2 - 3"), std::string("(- (- 1 2) 3)"));        // left associative
  CHECK_EQ(exprOf("8 / 4 / 2"), std::string("(/ (/ 8 4) 2)"));
  CHECK_EQ(exprOf("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));
  CHECK_EQ(exprOf("a << 1 + 2"), std::string("(<< a (+ 1 2))"));      // shifts bind looser than +
  CHECK_EQ(exprOf("a & b == c"), std::string("(& a (== b c))"));      // C-style: == binds tighter than &
  CHECK_EQ(exprOf("a | b ^ c & d"), std::string("(| a (^ b (& c d)))"));
  CHECK_EQ(exprOf("a < b == c < d"), std::string("(== (< a b) (< c d))"));
  CHECK_EQ(exprOf("a || b && c"), std::string("(|| a (&& b c))"));
  CHECK_EQ(exprOf("a && b || c"), std::string("(|| (&& a b) c)"));
}

TEST(ScriptParser, UnaryAndPostfix) {
  CHECK_EQ(exprOf("-a * b"), std::string("(* (neg a) b)"));
  CHECK_EQ(exprOf("- -a"), std::string("(neg (neg a))"));
  CHECK_EQ(exprOf("!a && !b"), std::string("(&& (not a) (not b))"));
  CHECK_EQ(exprOf("~a"), std::string("(bitnot a)"));
  CHECK_EQ(exprOf("a++ + ++b"), std::string("(+ (postinc a) (preinc b))"));
  CHECK_EQ(exprOf("a.b.c"), std::string("(. (. a b) c)"));
  CHECK_EQ(exprOf("a.b(1, 2)[3].c"), std::string("(. ([] (call (. a b) 1 2) 3) c)"));
  CHECK_EQ(exprOf("f()()"), std::string("(call (call f))"));
  CHECK_EQ(exprOf("-a.b"), std::string("(neg (. a b))"));  // postfix binds tighter than unary
  CHECK_EQ(exprOf("&a.b"), std::string("(addr (. a b))"));
  CHECK_EQ(exprOf("*p + 1"), std::string("(+ (deref p) 1)"));
  CHECK_EQ(exprOf("**pp"), std::string("(deref (deref pp))"));
}

TEST(ScriptParser, AssignmentTernaryAndCasts) {
  CHECK_EQ(exprOf("a = b = c"), std::string("(= a (= b c))"));    // right associative
  CHECK_EQ(exprOf("a += b * 2"), std::string("(+= a (* b 2))"));
  CHECK_EQ(exprOf("a ? b : c ? d : e"), std::string("(? a b (? c d e))"));
  CHECK_EQ(exprOf("a ? b = 1 : c"), std::string("(? a (= b 1) c)"));
  CHECK_EQ(exprOf("a || b ? c : d"), std::string("(? (|| a b) c d)"));
  CHECK_EQ(exprOf("x as int"), std::string("(as x int)"));
  CHECK_EQ(exprOf("x as Foo[]"), std::string("(as x Foo[])"));
  CHECK_EQ(exprOf("x is a.b.Foo"), std::string("(is x a.b.Foo)"));
  CHECK_EQ(exprOf("a + b as float"), std::string("(as (+ a b) float)"));  // `as` is relational, looser than +
  CHECK_EQ(exprOf("x is Foo && y"), std::string("(&& (is x Foo) y)"));
  CHECK_EQ(exprOf("x as fn(int):int"), std::string("(as x fn(int):int)"));
}

TEST(ScriptParser, LiteralsAndNew) {
  CHECK_EQ(exprOf("42"), std::string("42"));
  CHECK_EQ(exprOf("1.5"), std::string("1.5f"));
  CHECK_EQ(exprOf("\"hi\""), std::string("\"hi\""));
  CHECK_EQ(exprOf("true || false"), std::string("(|| true false)"));
  CHECK_EQ(exprOf("null"), std::string("null"));
  CHECK_EQ(exprOf("new Foo()"), std::string("(new Foo)"));
  CHECK_EQ(exprOf("new a.b.Foo(1, x)"), std::string("(new a.b.Foo 1 x)"));
  CHECK_EQ(exprOf("new int[10]"), std::string("(newarray int 10)"));
  CHECK_EQ(exprOf("new Foo[n + 1]"), std::string("(newarray Foo (+ n 1))"));
  CHECK_EQ(exprOf("new int[3][]"), std::string("(newarray int[] 3)"));
  CHECK_EQ(exprOf("[1, 2, 3]"), std::string("(array 1 2 3)"));
  CHECK_EQ(exprOf("[]"), std::string("(array)"));
  CHECK_EQ(exprOf("[1, 2,]"), std::string("(array 1 2)"));
  CHECK_EQ(exprOf("new Foo().bar()"), std::string("(call (. (new Foo) bar))"));
  CHECK_EQ(exprOf("typeof(x)"), std::string("(typeof x)"));
  CHECK_EQ(exprOf("typeof(int[])"), std::string("(typeof int[])"));
  CHECK_EQ(exprOf("typeof(a.b)"), std::string("(typeof (. a b))"));
}

TEST(ScriptParser, IntMinLiteral) {
  CHECK_EQ(exprOf("-9223372036854775808"), std::to_string(INT64_MIN));
  CHECK_EQ(exprOf("0x8000000000000000"), std::to_string(INT64_MIN));
  CHECK(exprOf("9223372036854775808").rfind("ERROR", 0) == 0);
  CHECK_EQ(exprOf("- 9223372036854775807"), std::string("(neg 9223372036854775807)"));
}

TEST(ScriptParser, Lambdas) {
  CHECK_EQ(exprOf("(a, b) => a + b"), std::string("(lambda (a b) (+ a b))"));
  CHECK_EQ(exprOf("(int a, int b) => a + b"), std::string("(lambda (int a int b) (+ a b))"));
  CHECK_EQ(exprOf("x => x * 2"), std::string("(lambda (x) (* x 2))"));
  CHECK_EQ(exprOf("() => 1"), std::string("(lambda () 1)"));
  CHECK_EQ(exprOf("(ref int a) => a++"), std::string("(lambda (ref int a) (postinc a))"));
  CHECK_EQ(exprOf("x => { return x; }"), std::string("(lambda (x) { (return x) })"));
  CHECK_EQ(exprOf("f((a) => a, 2)"), std::string("(call f (lambda (a) a) 2)"));
  CHECK_EQ(exprOf("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));  // parentheses that are not a lambda
  CHECK_EQ(exprOf("a => b => a + b"), std::string("(lambda (a) (lambda (b) (+ a b)))"));
  CHECK_EQ(exprOf("(fn(int):int f) => f(1)"), std::string("(lambda (fn(int):int f) (call f 1))"));
  CHECK_EQ(exprOf("x => y = x"), std::string("(lambda (x) (= y x))"));
}

TEST(ScriptParser, DeclarationVersusExpression) {
  CHECK_EQ(stmtOf("int x = 1;"), std::string("(decl int x 1)"));
  CHECK_EQ(stmtOf("var x = 1;"), std::string("(decl var x 1)"));
  CHECK_EQ(stmtOf("Foo x;"), std::string("(decl Foo x)"));
  CHECK_EQ(stmtOf("a.b.Foo x = null;"), std::string("(decl a.b.Foo x null)"));
  CHECK_EQ(stmtOf("int[] xs = [1];"), std::string("(decl int[] xs (array 1))"));
  CHECK_EQ(stmtOf("Foo[] xs;"), std::string("(decl Foo[] xs)"));
  CHECK_EQ(stmtOf("Foo* p = &x;"), std::string("(decl Foo* p (addr x))"));
  CHECK_EQ(stmtOf("int** pp;"), std::string("(decl int** pp)"));
  CHECK_EQ(stmtOf("fn(int):int f = x => x;"), std::string("(decl fn(int):int f (lambda (x) x))"));
  CHECK_EQ(stmtOf("x = 1;"), std::string("(expr (= x 1))"));
  CHECK_EQ(stmtOf("xs[0] = 1;"), std::string("(expr (= ([] xs 0) 1))"));
  CHECK_EQ(stmtOf("f(x);"), std::string("(expr (call f x))"));
  CHECK_EQ(stmtOf("a.b = c;"), std::string("(expr (= (. a b) c))"));
  CHECK_EQ(stmtOf("i++;"), std::string("(expr (postinc i))"));
  CHECK_EQ(stmtOf("*p = 3;"), std::string("(expr (= (deref p) 3))"));
  CHECK(stmtOf("int a = 1, b = 2;").rfind("ERROR", 0) == 0);
  CHECK(stmtOf("void v;").rfind("ERROR", 0) == 0);
}

TEST(ScriptParser, ControlFlow) {
  CHECK_EQ(stmtOf("if (a) b(); else c();"), std::string("(if a (expr (call b)) (expr (call c)))"));
  CHECK_EQ(stmtOf("if (a) if (b) x(); else y();"), std::string("(if a (if b (expr (call x)) (expr (call y))))"));  // dangling else binds inward
  CHECK_EQ(stmtOf("while (a) { b(); }"), std::string("(while a { (expr (call b)) })"));
  CHECK_EQ(stmtOf("do x(); while (a);"), std::string("(do (expr (call x)) a)"));
  CHECK_EQ(stmtOf("for (int i = 0; i < n; i++) s += i;"),
           std::string("(for (decl int i 0) (< i n) (postinc i) (expr (+= s i)))"));
  CHECK_EQ(stmtOf("for (;;) break;"), std::string("(for _ _ _ (break))"));
  CHECK_EQ(stmtOf("for (i = 0; ; ) continue;"), std::string("(for (expr (= i 0)) _ _ (continue))"));
  CHECK_EQ(stmtOf("for (int x : xs) f(x);"), std::string("(foreach int x xs (expr (call f x)))"));
  CHECK_EQ(stmtOf("for (var x : a.items) {}"), std::string("(foreach var x (. a items) { })"));
  CHECK_EQ(stmtOf("for (Foo f : fs) {}"), std::string("(foreach Foo f fs { })"));
  CHECK_EQ(stmtOf("return;"), std::string("(return)"));
  CHECK_EQ(stmtOf("return a + 1;"), std::string("(return (+ a 1))"));
  CHECK_EQ(stmtOf("throw new Error(\"x\");"), std::string("(throw (new Error \"x\"))"));
  CHECK_EQ(stmtOf("yield;"), std::string("(yield)"));
  CHECK_EQ(stmtOf("yield 0.5;"), std::string("(yield 0.5f)"));
  CHECK_EQ(stmtOf(";"), std::string("{ }"));
}

TEST(ScriptParser, SwitchAndTry) {
  CHECK_EQ(stmtOf("switch (x) { case 1, 2: a(); break; case 3: b(); default: c(); }"),
           std::string("(switch x (case 1 2 : (expr (call a)) (break)) (case 3 : (expr (call b))) (default : (expr (call c))))"));
  CHECK_EQ(stmtOf("switch (x) { }"), std::string("(switch x)"));
  CHECK(stmtOf("switch (x) { default: a(); default: b(); }").rfind("ERROR", 0) == 0);
  CHECK(stmtOf("switch (x) { a(); }").rfind("ERROR", 0) == 0);
  CHECK_EQ(stmtOf("try { a(); } catch (Error e) { b(); }"),
           std::string("(try { (expr (call a)) } (catch Error e { (expr (call b)) }))"));
  CHECK_EQ(stmtOf("try { a(); } catch (A e) {} catch (B e) {} finally { c(); }"),
           std::string("(try { (expr (call a)) } (catch A e { }) (catch B e { }) (finally { (expr (call c)) }))"));
  CHECK_EQ(stmtOf("try { a(); } finally { c(); }"), std::string("(try { (expr (call a)) } (finally { (expr (call c)) }))"));
  CHECK(stmtOf("try { a(); }").rfind("ERROR", 0) == 0);
}

TEST(ScriptParser, ClassesInterfacesEnums) {
  auto p = parseText(R"(
    module game.player;
    import engine.math;
    import util;
    public abstract class Player extends Entity implements Damageable, Named {
      private int hp = 100;
      public static final float SPEED = 2.5;
      protected string[] tags;
      Player(string n, ref int c) { super(n); }
      public override void update(float dt) { hp -= 1; }
      public abstract int score();
      public native void ping();
      public static Vec operator +(Vec a, Vec b) { return a; }
      public int operator [](Player p, int i) { return i; }
      public coroutine void run() { yield 1.0; }
    }
    interface Damageable extends Named, Other { void hit(int amount); }
    enum Color { Red, Green = 5, Blue }
  )");
  CHECK_MSG(!p.diags.hasErrors(), p.errors());
  REQUIRE(p.unit != nullptr);
  const std::string got = sd::unit(*p.unit);
  const std::string want =
      "module game.player\n"
      "import engine.math\n"
      "import util\n"
      "class public abstract Player extends Entity : Damageable : Named\n"
      "  (field private int hp 100)\n"
      "  (field public static final float SPEED 2.5f)\n"
      "  (field protected string[] tags)\n"
      "  (ctor <init> (string n, ref int c) { (expr (call super n)) })\n"
      "  (method public override void update (float dt) { (expr (-= hp 1)) })\n"
      "  (method public abstract int score ())\n"
      "  (method public native void ping ())\n"
      "  (op public static Vec operator'+' (Vec a, Vec b) { (return a) })\n"
      "  (op public int operator[] (Player p, int i) { (return i) })\n"
      "  (method public coroutine void run () { (yield 1f) })\n"
      "interface Damageable : Named : Other\n"
      "  (method void hit (int amount))\n"
      "enum Color\n"
      "  (const Red)\n"
      "  (const Green 5)\n"
      "  (const Blue)\n";
  CHECK_EQ(got, want);
}

TEST(ScriptParser, MemberDisambiguation) {
  // A method returning the class's own type is not a constructor; a constructor has no return type.
  auto p = parseText("class A { A make() { return null; } A() {} int A2; A f; }");
  CHECK_MSG(!p.diags.hasErrors(), p.errors());
  const auto& m = p.unit->types[0]->members;
  REQUIRE(m.size() == 4);
  CHECK(m[0].kind == MemberKind::Method);
  CHECK(m[1].kind == MemberKind::Constructor);
  CHECK(m[2].kind == MemberKind::Field);
  CHECK(m[3].kind == MemberKind::Field);
  CHECK(parseText("interface I { I() {} }").diags.hasErrors());
  CHECK(parseText("class A { public private int x; }").diags.hasErrors());
  CHECK(parseText("class A { static static int x; }").diags.hasErrors());
  CHECK(parseText("class A { int operator ?(A a) { } }").diags.hasErrors());
}

TEST(ScriptParser, EmptyAndMinimalInputs) {
  auto e = parseText("");
  CHECK(!e.diags.hasErrors());
  CHECK(e.unit->types.empty());
  CHECK(!parseText("// nothing\n").diags.hasErrors());
  CHECK(!parseText("module a;").diags.hasErrors());
  CHECK(!parseText("class A {}").diags.hasErrors());
}

TEST(ScriptParser, SyntaxErrorsCarryLocationsAndRecover) {
  auto p = parseText(
      "class A {\n"
      "  void f() {\n"
      "    int x = ;\n"          // error 1 (line 3)
      "    int y = 2;\n"
      "    if (y > ) y = 3;\n"   // error 2 (line 5)
      "    return;\n"
      "  }\n"
      "  int g( { }\n"          // error 3 (line 8)
      "  void h() { x = 1; }\n"  // fine: recovery resumed
      "}\n");
  const auto& errs = p.diags.errors();
  REQUIRE(errs.size() >= 3);
  CHECK_EQ(errs[0].line, 3u);
  CHECK_EQ(errs[1].line, 5u);
  CHECK_EQ(errs[2].line, 8u);
  CHECK_EQ(errs[0].file, std::string("test.mins"));
  CHECK(errs[0].message.find("expected an expression") != std::string::npos);
  // The tree after recovery still contains the later, valid members.
  bool sawH = false;
  for (const auto& m : p.unit->types[0]->members) sawH = sawH || m.name == "h";
  CHECK(sawH);
}

TEST(ScriptParser, MissingTokensAreNamedInTheMessage) {
  auto p = parseText("class A { void f() { int x = 1 } }");
  REQUIRE(p.diags.hasErrors());
  CHECK(p.diags.errors()[0].message.find("expected ';'") != std::string::npos);
  auto q = parseText("class A { void f( { } }");
  CHECK(q.diags.hasErrors());
  auto r = parseText("class { }");
  REQUIRE(r.diags.hasErrors());
  CHECK(r.diags.errors()[0].message.find("type name") != std::string::npos);
  auto s = parseText("class A { void f() { super; } }");
  CHECK(s.diags.hasErrors());
  auto t = parseText("class A { void f() { new int(); } }");
  CHECK(t.diags.hasErrors());
  auto u = parseText("class A { void f() { new int[]; } }");
  CHECK(u.diags.hasErrors());
  CHECK(parseText("garbage").diags.hasErrors());
  CHECK(parseText("class A { void f() { } ").diags.hasErrors());  // missing closing brace
}

TEST(ScriptParser, DeepNestingIsRejectedNotACrash) {
  std::string e = "class T { int x = ";
  for (int i = 0; i < 5000; ++i) e += "(";
  e += "1";
  for (int i = 0; i < 5000; ++i) e += ")";
  e += "; }";
  auto p = parseText(e);
  REQUIRE(p.diags.hasErrors());
  CHECK(p.errors().find("too deep") != std::string::npos);

  std::string b = "class T { void f() { ";
  for (int i = 0; i < 5000; ++i) b += "{";
  for (int i = 0; i < 5000; ++i) b += "}";
  b += " } }";
  CHECK(parseText(b).diags.hasErrors());

  std::string u = "class T { int x = ";
  for (int i = 0; i < 100000; ++i) u += "-";
  u += "1; }";
  CHECK(parseText(u).diags.hasErrors());

  std::string bin = "class T { int x = 1";
  for (int i = 0; i < 100000; ++i) bin += " + 1";
  bin += "; }";
  // A flat chain is parsed iteratively but builds a left-deep tree, so it is capped (every later
  // pass, and tree destruction, recurses over it).
  auto binErr = parseText(bin);
  REQUIRE(binErr.diags.hasErrors());
  CHECK(binErr.errors().find("too many chained") != std::string::npos);
  std::string longOk = "class T { int x = 1";
  for (int i = 0; i < 900; ++i) longOk += " + 1";
  longOk += "; }";
  CHECK(!parseText(longOk).diags.hasErrors());
  std::string dots = "class T { int x = a";
  for (int i = 0; i < 100000; ++i) dots += ".b";
  dots += "; }";
  CHECK(parseText(dots).diags.hasErrors());

  // Moderate nesting is accepted.
  std::string ok = "class T { int x = ";
  for (int i = 0; i < 50; ++i) ok += "(";
  ok += "1";
  for (int i = 0; i < 50; ++i) ok += ")";
  ok += "; }";
  CHECK(!parseText(ok).diags.hasErrors());
}

TEST(ScriptParser, MutatedValidProgramsNeverCrashAndAlwaysTerminate) {
  const std::string base = R"(
    module m;
    import x.y;
    public class Foo extends Bar implements Baz {
      private int[] xs = [1, 2, 3];
      static final float K = 2.5e3;
      Foo(int a) { super(a); this.n = a; }
      public override int f(ref int a, fn(int):int cb) {
        int total = 0;
        for (int i = 0; i < xs.length; i++) { total += cb(xs[i]) * (a as int); }
        for (var v : xs) { if (v is Foo && !(v == null)) continue; else break; }
        try { throw new Error("x"); } catch (Error e) { total--; } finally { total++; }
        switch (total) { case 1, 2: return 1; default: return total > 0 ? total : -total; }
        var l = (a, b) => a + b;
        yield 1.0;
        while (true) { do { *p = &total; } while (false); }
        return typeof(Foo) == typeof(total) ? 1 : 0;
      }
      public static Vec operator +(Vec a, Vec b) { return new Vec(a.x + b.x); }
    }
    interface Baz { void g(); }
    enum E { A, B = 2 }
  )";
  {
    auto ok = parseText(base);
    CHECK_MSG(!ok.diags.hasErrors(), ok.errors());
  }
  ScriptLcg rng(20261021);
  for (int round = 0; round < 4000; ++round) {
    std::string s = base;
    const std::uint32_t edits = 1 + rng.below(4);
    for (std::uint32_t e = 0; e < edits; ++e) {
      if (s.empty()) break;
      const std::size_t pos = rng.below(static_cast<std::uint32_t>(s.size()));
      switch (rng.below(4)) {
        case 0: s.erase(pos, 1 + rng.below(8)); break;
        case 1: s.insert(pos, 1, static_cast<char>(rng.below(256))); break;
        case 2: s[pos] = "(){}[];,.=<>+-*&!?:\"0aZ_ "[rng.below(25)]; break;
        default: {
          const std::size_t from = rng.below(static_cast<std::uint32_t>(s.size()));
          s.insert(pos, s.substr(from, rng.below(40)));
        }
      }
    }
    auto p = parseText(s);  // must return (no hang, no crash); ASan/valgrind runs check memory safety
    CHECK(p.unit != nullptr);
    CHECK(p.diags.errors().size() <= Diagnostics::kMaxErrors);
  }
}

TEST(ScriptParser, TruncatedProgramsAtEveryLengthTerminate) {
  const std::string src =
      "module a.b; import c; class A extends B implements C { int x = (1 + 2) * 3; void f(int a) { "
      "for (int i = 0; i < 3; i++) { if (a is A) { try { throw a; } catch (E e) { } } } } }";
  for (std::size_t n = 0; n <= src.size(); ++n) {
    auto p = parseText(std::string_view(src).substr(0, n));
    CHECK(p.unit != nullptr);
    // After `class` starts, every proper prefix is incomplete (the header lines alone are valid).
    if (n > src.find("class") && n < src.size()) CHECK_MSG(p.diags.hasErrors(), "prefix of length " + std::to_string(n) + " should be incomplete");
  }
}
