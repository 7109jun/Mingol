// SPDX-License-Identifier: GPL-2.0-only
// End-to-end tests: classes, interfaces, closures, references, operators, modules.
#include "vm_test_util.hpp"

using namespace mingol::test;

TEST(VmLang, ClassesFieldsAndConstructors) {
  CHECK_INT(R"(
class P { int x; int y = 5; P(int x) { this.x = x; } P() { this(10); } int sum() { return x + y; } }
class Main { static int f() { P a = new P(3); P b = new P(); return a.sum() * 100 + b.sum(); } })", 8 * 100 + 15);
  CHECK_INT(R"(
class A { int v = 1; A() { v += 10; } }
class B extends A { int w = 2; B() { super(); w += v; } }
class Main { static int f() { B b = new B(); return b.v * 100 + b.w; } })", 11 * 100 + 13);
  // implicit super() when the constructor does not chain
  CHECK_INT(R"(
class A { int v; A() { v = 7; } }
class B extends A { B() { v += 1; } }
class Main { static int f() { return new B().v; } })", 8);
  // default constructor chains to the base default constructor and runs field initialisers
  CHECK_INT(R"(
class A { int a = 3; }
class B extends A { int b = 4; }
class Main { static int f() { B x = new B(); return x.a * 10 + x.b; } })", 34);
}

TEST(VmLang, InitialisationOrder) {
  // base ctor body runs before derived field initialisers; base field init before base ctor body
  CHECK_STR(R"(
class Log { static string s = ""; }
class A { int a = mark("Af"); A() { Log.s += "Ac "; } static int mark(string m) { Log.s += m + " "; return 1; } }
class B extends A { int b = A.mark("Bf"); B() { Log.s += "Bc "; } }
class Main { static string f() { new B(); return Log.s; } })", "Af Ac Bf Bc ");
  // static initialisers run once, in declaration order
  CHECK_STR(R"(
class Main {
  static string log = "";
  static int a = note("a");
  static int b = note("b");
  static int note(string s) { log += s; return 1; }
  static string f() { return log; }
})", "ab");
}

TEST(VmLang, VirtualDispatchAbstractAndSuper) {
  CHECK_INT(R"(
abstract class Shape { abstract int sides(); int twice() { return sides() * 2; } }
class Tri extends Shape { override int sides() { return 3; } }
class Quad extends Shape { override int sides() { return 4; } override int twice() { return super.twice() + 1; } }
class Main { static int f() { Shape a = new Tri(); Shape b = new Quad(); return a.twice() * 100 + b.twice(); } })", 6 * 100 + 9);
  CHECK_INT(R"(
class A { int g() { return 1; } int h() { return g(); } }
class B extends A { override int g() { return 2; } }
class C extends B { override int g() { return 3; } }
class Main { static int f() { A a = new C(); B b = new B(); return a.h() * 10 + b.h(); } })", 32);
  CHECK_INT(R"(
class A { private int p() { return 1; } int q() { return p(); } }
class B extends A { int p() { return 2; } }
class Main { static int f() { return new B().q(); } })", 1);  // private methods are not virtual
}

TEST(VmLang, InterfacesAndCasts) {
  CHECK_INT(R"(
interface Named { int id(); }
interface Scored extends Named { int score(); }
class Player implements Scored { int id() { return 7; } int score() { return 40; } }
class Main { static int f() { Named n = new Player(); Scored s = n as Scored; return n.id() + s.score(); } })", 47);
  CHECK_BOOL(R"(
interface I { } class A implements I { } class B { }
class Main { static bool f() { Object a = new A(); Object b = new B(); return a is I && !(b is I) && a is A && !(a is B); } })", true);
  CHECK_THROWS(R"(
class A { } class B { }
class Main { static int f() { Object o = new A(); B b = o as B; return 0; } })", "mingol.CastError");
  CHECK_BOOL(R"(
class A { }
class Main { static bool f() { Object o = null; A a = o as A; return a == null && !(o is A); } })", true);
  CHECK_INT(R"(
class A { int v = 1; } class B extends A { int w = 2; }
class Main { static int f() { A a = new B(); B b = a as B; return b.v + b.w; } })", 3);
}

TEST(VmLang, StaticMembersAndEnumsInClasses) {
  CHECK_INT(R"(
class Counter { static int n; static int next() { n++; return n; } }
class Main { static int f() { Counter.next(); Counter.next(); return Counter.next() * 10 + Counter.n; } })", 33);
  CHECK_INT(R"(
class Cfg { static int[] table = [1, 2, 3]; static int total() { int s = 0; for (int v : table) { s += v; } return s; } }
class Main { static int f() { return Cfg.total(); } })", 6);
}

TEST(VmLang, OverloadResolution) {
  CHECK_INT(R"(
class Main {
  static int g(int a) { return 1; } static int g(float a) { return 2; } static int g(string a) { return 3; } static int g(Object a) { return 4; }
  static int f() { return g(1) * 1000 + g(1.5) * 100 + g("s") * 10 + g(new Main()); }
})", 1234);
  CHECK_INT(R"(
class Main {
  static int h(int a, int b) { return a + b; } static int h(int a) { return a * 10; }
  static int f() { return h(2) + h(2, 3); }
})", 25);
}

TEST(VmLang, ClosuresCaptureAndShare) {
  CHECK_INT(R"(
class Main {
  static fn(int):int adder(int k) { return (int x) => x + k; }
  static int f() { fn(int):int a = adder(3); fn(int):int b = adder(10); return a(1) * 100 + b(1); }
})", 4 * 100 + 11);
  // a captured variable that is reassigned is shared (boxed)
  CHECK_INT(R"(
class Main { static int f() { int n = 0; fn():void inc = () => { n++; }; inc(); inc(); inc(); return n; } })", 3);
  CHECK_INT(R"(
class Main { static int f() { int n = 1; fn():int get = () => n; n = 5; return get(); } })", 5);
  // two closures share one variable
  CHECK_INT(R"(
class Main { static int f() { int n = 0; fn():void inc = () => { n += 2; }; fn():int get = () => n; inc(); inc(); return get(); } })", 4);
  // each loop iteration gets its own variable
  CHECK_INT(R"(
class Main { static int f() {
  (fn():int)[] fs = null;
  (fn():int)[] a = null;
  int total = 0;
  (fn():int)[] arr = new (fn():int)[3];
  return 0;
} })", 0);
}

TEST(VmLang, ClosuresInLoopsGetFreshVariables) {
  CHECK_INT(R"(
class Main { static int f() {
  (fn():int)[] fs = new (fn():int)[3];
  for (int i = 0; i < 3; i++) { int j = i * 10; fs[i] = () => j; }
  return fs[0]() + fs[1]() * 100 + fs[2]() * 10000;
} })", 0 + 10 * 100 + 20 * 10000);
  CHECK_INT(R"(
class Main { static int f() {
  (fn():int)[] fs = new (fn():int)[3];
  int idx = 0;
  for (int v : [4, 5, 6]) { fs[idx] = () => v; idx++; }
  return fs[0]() + fs[1]() * 10 + fs[2]() * 100;
} })", 4 + 50 + 600);
}

TEST(VmLang, NestedLambdasRecursionAndThis) {
  CHECK_INT(R"(
class Main { static int f() { int a = 1; fn(int):fn(int):int mk = (int b) => (int c) => a + b + c; return mk(10)(100); } })", 111);
  CHECK_INT(R"(
class Main {
  int base = 100;
  fn(int):int make() { return (int x) => x + base; }
  static int f() { Main m = new Main(); fn(int):int g = m.make(); m.base = 200; return g(1); }
})", 201);
  CHECK_INT(R"(
class Main { static int f() { fn(int):int fact = null; fact = (int n) => n <= 1 ? 1 : n * fact(n - 1); return fact(10); } })", 3628800);
  CHECK_INT(R"(
class Main { static int apply(fn(int,int):int op, int a, int b) { return op(a, b); }
  static int f() { return apply((a, b) => a * b, 6, 7) + apply((int a, int b) => a - b, 10, 3); } })", 49);
}

TEST(VmLang, MethodValues) {
  CHECK_INT(R"(
class Main {
  static int dbl(int x) { return x * 2; }
  int off = 5;
  int add(int x) { return x + off; }
  static int f() { fn(int):int a = dbl; Main m = new Main(); fn(int):int b = m.add; m.off = 7; return a(4) * 100 + b(1); }
})", 8 * 100 + 8);
  CHECK_INT(R"(
class A { int g() { return 1; } } class B extends A { override int g() { return 2; } }
class Main { static int f() { A a = new B(); fn():int h = a.g; return h(); } })", 2);
}

TEST(VmLang, RefParametersAndPointers) {
  CHECK_INT(R"(
class Main {
  static void swap(ref int a, ref int b) { int t = a; a = b; b = t; }
  static int f() { int x = 1; int y = 2; swap(x, y); return x * 10 + y; }
})", 21);
  CHECK_INT(R"(
class Main {
  static void inc(ref int v) { v += 5; }
  static int[] arr;
  static int g;
  int fld;
  static int f() {
    int local = 1; inc(local);
    arr = [10, 20]; inc(arr[1]);
    g = 100; inc(g);
    Main m = new Main(); m.fld = 1000; inc(m.fld);
    return local + arr[1] + g + m.fld;
  }
})", 6 + 25 + 105 + 1005);
  CHECK_INT(R"(
class Main {
  static void forward(ref int v) { bump(v); bump(v); }
  static void bump(ref int v) { v++; }
  static int f() { int n = 0; forward(n); return n; }
})", 2);
  CHECK_INT(R"(
class Main { static int f() { int a = 1; int* p = &a; *p = 9; int b = *p + 1; return a * 100 + b; } })", 9 * 100 + 10);
  CHECK_INT(R"(
class Main { static int f() { int[] arr = [1, 2, 3]; int* p = &arr[1]; *p += 40; return arr[1]; } })", 42);
  CHECK_INT(R"(
class B { int v = 1; } class Main { static int f() { B b = new B(); int* p = &b.v; *p = 8; return b.v; } })", 8);
  CHECK_BOOL(R"(
class Main { static bool f() { int a = 0; int b = 0; int* p = &a; int* q = &a; int* r = &b; return p == q && p != r; } })", true);
  CHECK_THROWS(R"(
class Main { static int f() { int* p = null; return *p; } })", "mingol.NullReferenceError");
  // a pointer to a captured variable keeps it alive and shared
  CHECK_INT(R"(
class Main { static int f() { int n = 1; int* p = &n; fn():void inc = () => { n += 10; }; inc(); return *p; } })", 11);
  // ref parameters of lambdas
  CHECK_INT(R"(
class Main { static int f() { fn(ref int):void inc = (ref int v) => { v += 3; }; int x = 4; inc(x); return x; } })", 7);
}

TEST(VmLang, OperatorOverloading) {
  const std::string vec = R"(
class Vec {
  float x; float y;
  Vec(float x, float y) { this.x = x; this.y = y; }
  static Vec operator+(Vec a, Vec b) { return new Vec(a.x + b.x, a.y + b.y); }
  static Vec operator-(Vec a, Vec b) { return new Vec(a.x - b.x, a.y - b.y); }
  static Vec operator*(Vec a, float k) { return new Vec(a.x * k, a.y * k); }
  static Vec operator-(Vec a) { return new Vec(-a.x, -a.y); }
  static bool operator==(Vec a, Vec b) { return a.x == b.x && a.y == b.y; }
  static float operator[](Vec v, int i) { return i == 0 ? v.x : v.y; }
  static void operator[](Vec v, int i, float val) { if (i == 0) v.x = val; else v.y = val; }
}
)";
  CHECK_FLOAT(vec + "class Main { static float f() { Vec v = new Vec(1, 2) + new Vec(3, 4) * 2.0; return v.x * 100 + v.y; } }", 7 * 100 + 10.0);
  CHECK_FLOAT(vec + "class Main { static float f() { Vec v = -new Vec(1, 2); return v.x + v.y; } }", -3.0);
  CHECK_BOOL(vec + "class Main { static bool f() { return new Vec(1, 2) == new Vec(1, 2) && new Vec(1, 2) != new Vec(2, 1); } }", true);
  CHECK_FLOAT(vec + "class Main { static float f() { Vec v = new Vec(5, 6); v[0] = 9; v[1] += 1; return v[0] * 100 + v[1]; } }", 9 * 100 + 7.0);
  CHECK_FLOAT(vec + "class Main { static float f() { Vec v = new Vec(1, 1); v += new Vec(2, 3); return v.x + v.y; } }", 7.0);
  CHECK_FLOAT(vec + "class Main { static float f() { Vec v = new Vec(1, 1); v *= 3.0; return v.x + v.y; } }", 6.0);
}

TEST(VmLang, ModulesAndImports) {
  auto l = loadSources({{"math.mins", "module util.math;\npublic class M { public static int sq(int x) { return x * x; } }\n"},
                        {"main.mins", "import util.math;\nclass Main { static int f() { return M.sq(7); } }\n"}});
  auto o = runMain(l);
  REQUIRE(o.ok);
  CHECK_EQ(o.value.u.i, 49);
  // qualified access without import
  auto l2 = loadSources({{"math.mins", "module util.math;\npublic class M { public static int sq(int x) { return x * x; } }\n"},
                         {"main.mins", "class Main { static int f() { return util.math.M.sq(3); } }\n"}});
  auto o2 = runMain(l2);
  REQUIRE(o2.ok);
  CHECK_EQ(o2.value.u.i, 9);
}

TEST(VmLang, Recursion) {
  CHECK_INT(R"(class Main { static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); } static int f() { return fib(22); } })", 17711);
  CHECK_INT(R"(class Main { static int ack(int m, int n) { if (m == 0) return n + 1; if (n == 0) return ack(m - 1, 1); return ack(m - 1, ack(m, n - 1)); } static int f() { return ack(2, 3); } })", 9);
  CHECK_THROWS(R"(class Main { static int r(int n) { return r(n + 1) + 1; } static int f() { return r(0); } })", "mingol.StackOverflowError");
}
