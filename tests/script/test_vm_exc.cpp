// SPDX-License-Identifier: GPL-2.0-only
// End-to-end tests: exceptions, try/catch/finally control flow, coroutines.
#include "vm_test_util.hpp"

using namespace mingol::test;

namespace {
const char* kPre = R"(
class MyError extends Exception { int code; MyError(string m, int code) { super(m); this.code = code; } }
class OtherError extends Exception { OtherError(string m) { super(m); } }
class Log { static string s = ""; static void add(string x) { s += x; } }
)";
std::string withPre(const std::string& s) { return std::string(kPre) + s; }
}  // namespace

TEST(VmExc, CatchByTypeAndHierarchy) {
  CHECK_INT(withPre(R"(class Main { static int f() { try { throw new MyError("x", 42); } catch (MyError e) { return e.code; } } })"), 42);
  CHECK_INT(withPre(R"(class Main { static int f() { try { throw new MyError("x", 1); } catch (OtherError e) { return 2; } catch (Exception e) { return 3; } } })"), 3);
  CHECK_INT(withPre(R"(class Main { static int f() { try { throw new MyError("x", 1); } catch (Exception e) { return 3; } catch (MyError e) { return 2; } } })"), 3);  // first match wins
  CHECK_STR(withPre(R"(class Main { static string f() { try { throw new OtherError("boom"); } catch (Exception e) { return e.getMessage(); } } })"), "boom");
  CHECK_THROWS(withPre(R"(class Main { static int f() { try { throw new OtherError("a"); } catch (MyError e) { return 1; } } })"), "OtherError");
}

TEST(VmExc, RuntimeErrorsAreCatchable) {
  CHECK_INT(R"(class Main { static int f() { try { int z = 0; return 1 / z; } catch (DivideByZeroError e) { return 7; } } })", 7);
  CHECK_INT(R"(class Main { static int f() { try { int[] a = new int[1]; return a[3]; } catch (IndexOutOfRangeError e) { return 8; } } })", 8);
  CHECK_INT(R"(class P { int x; } class Main { static int f() { try { P p = null; return p.x; } catch (RuntimeError e) { return 9; } } })", 9);
  CHECK_INT(R"(class Main { static int f() { try { int z = 0; return 1 / z; } catch (Exception e) { return 10; } } })", 10);
  CHECK_INT(R"(class Main { static int r(int n) { return r(n + 1) + 1; } static int f() { try { return r(0); } catch (StackOverflowError e) { return 11; } } })", 11);
  CHECK_STR(R"(class Main { static string f() { try { int[] a = new int[2]; return "" + a[5]; } catch (RuntimeError e) { return e.getMessage(); } } })", "index 5 is out of range for length 2");
}

TEST(VmExc, FinallyRunsOnEveryExit) {
  CHECK_STR(withPre(R"(class Main { static string f() { try { Log.add("t"); } finally { Log.add("f"); } return Log.s; } })"), "tf");
  CHECK_STR(withPre(R"(class Main { static string f() { try { try { throw new OtherError("x"); } finally { Log.add("f"); } } catch (OtherError e) { Log.add("c"); } return Log.s; } })"), "fc");
  CHECK_STR(withPre(R"(class Main { static string f() { try { throw new OtherError("x"); } catch (OtherError e) { Log.add("c"); } finally { Log.add("f"); } return Log.s; } })"), "cf");
  CHECK_STR(withPre(R"(class Main { static int g() { try { Log.add("t"); return 5; } finally { Log.add("f"); } } static string f() { int v = g(); return Log.s + v; } })"), "tf5");
  CHECK_STR(withPre(R"(class Main { static int g() { try { throw new OtherError("x"); } catch (OtherError e) { Log.add("c"); return 6; } finally { Log.add("f"); } } static string f() { int v = g(); return Log.s + v; } })"), "cf6");
  // return value is computed before finally runs
  CHECK_INT(R"(class Main { static int n; static int g() { n = 1; try { return n; } finally { n = 99; } } static int f() { int v = g(); return v * 1000 + n; } })", 1 * 1000 + 99);
}

TEST(VmExc, FinallyWithBreakContinueAndNesting) {
  CHECK_STR(withPre(R"(class Main { static string f() { for (int i = 0; i < 3; i++) { try { if (i == 1) continue; Log.add("b" + i); } finally { Log.add("f" + i); } } return Log.s; } })"), "b0f0f1b2f2");
  CHECK_STR(withPre(R"(class Main { static string f() { for (int i = 0; i < 5; i++) { try { if (i == 2) break; Log.add("b" + i); } finally { Log.add("f" + i); } } return Log.s; } })"), "b0f0b1f1f2");
  CHECK_STR(withPre(R"(class Main { static string f() { try { try { Log.add("1"); } finally { Log.add("2"); } } finally { Log.add("3"); } return Log.s; } })"), "123");
  CHECK_STR(withPre(R"(class Main { static int g() { try { try { return 1; } finally { Log.add("a"); } } finally { Log.add("b"); } } static string f() { g(); return Log.s; } })"), "ab");
  CHECK_STR(withPre(R"(class Main { static string f() { while (true) { try { try { break; } finally { Log.add("i"); } } finally { Log.add("o"); } } return Log.s; } })"), "io");
  // break out of a loop that is inside the try: finally must not run for it
  CHECK_STR(withPre(R"(class Main { static string f() { try { for (int i = 0; i < 3; i++) { if (i == 1) break; Log.add("l"); } } finally { Log.add("f"); } return Log.s; } })"), "lf");
}

TEST(VmExc, ThrowInsideCatchAndFinally) {
  CHECK_STR(withPre(R"(class Main { static string f() { try { try { throw new OtherError("a"); } catch (OtherError e) { throw new MyError("b", 1); } finally { Log.add("f"); } } catch (MyError e) { Log.add("m"); } return Log.s; } })"), "fm");
  // an exception thrown from finally replaces the pending one
  CHECK_STR(withPre(R"(class Main { static string f() { try { try { throw new OtherError("a"); } finally { throw new MyError("b", 1); } } catch (Exception e) { return e.getMessage(); } } })"), "b");
  CHECK_STR(withPre(R"(class Main { static string f() { try { try { throw new OtherError("a"); } catch (OtherError e) { Log.add("1"); throw e; } } catch (OtherError e) { Log.add("2"); } return Log.s; } })"), "12");
  CHECK_THROWS(withPre(R"(class Main { static int f() { try { return 1; } finally { throw new OtherError("late"); } } })"), "OtherError");
}

TEST(VmExc, ExceptionStateAcrossCalls) {
  CHECK_INT(withPre(R"(class Main {
    static void thrower(int d) { if (d == 0) throw new MyError("deep", 5); thrower(d - 1); }
    static int f() { try { thrower(10); return 0; } catch (MyError e) { return e.code; } } })"), 5);
  CHECK_STR(withPre(R"(class Main {
    static void a() { try { b(); } finally { Log.add("a"); } }
    static void b() { try { c(); } finally { Log.add("b"); } }
    static void c() { throw new OtherError("x"); }
    static string f() { try { a(); } catch (Exception e) { Log.add("!"); } return Log.s; } })"), "ba!");
  // catching leaves the VM fully usable and the stack balanced
  CHECK_INT(withPre(R"(class Main { static int f() { int s = 0; for (int i = 0; i < 1000; i++) { try { throw new MyError("x", i); } catch (MyError e) { s += e.code; } } return s; } })"), 499500);
}

TEST(VmExc, StackTraceNamesFunctionsAndLines) {
  auto l = load("class Main {\n  static void inner() {\n    throw new Exception(\"bad\");\n  }\n  static void outer() {\n    inner();\n  }\n  static int f() {\n    outer();\n    return 0;\n  }\n}\n");
  REQUIRE(l.ok());
  auto o = callStatic(l, "Main", "f");
  REQUIRE(!o.ok);
  CHECK_EQ(o.ex.type, "mingol.Exception");
  CHECK_EQ(o.ex.message, "bad");
  CHECK(o.ex.stackTrace.find("Main.inner()") != std::string::npos);
  CHECK(o.ex.stackTrace.find("test.mins:3") != std::string::npos);
  CHECK(o.ex.stackTrace.find("Main.outer()") != std::string::npos);
  CHECK(o.ex.stackTrace.find("test.mins:6") != std::string::npos);
  CHECK(o.ex.stackTrace.find("Main.f()") != std::string::npos);
  CHECK(o.ex.stackTrace.find("test.mins:9") != std::string::npos);
  // innermost first
  CHECK(o.ex.stackTrace.find("Main.inner()") < o.ex.stackTrace.find("Main.outer()"));
}

TEST(VmExc, UncaughtExceptionDuringStaticInit) {
  auto l = load("class Main { static int x = boom(); static int boom() { throw new Exception(\"init failed\"); } static int f() { return x; } }");
  REQUIRE(l.program != nullptr);
  CHECK(!l.initError.empty());
  CHECK(l.initError.find("init failed") != std::string::npos);
}

// ---- coroutines -----------------------------------------------------------------------------------------

TEST(VmCoro, YieldAndResume) {
  CHECK_INT(R"(
class Main {
  static coroutine void count(int n) { for (int i = 0; i < n; i++) { yield i; } }
  static int f() {
    Coroutine c = count(4);
    int sum = 0; int steps = 0;
    while (!c.isDone()) { c.resume(); if (!c.isDone()) { sum += c.yieldedInt(); steps++; } }
    return sum * 100 + steps;
  }
})", 6 * 100 + 4);
}

TEST(VmCoro, NothingRunsUntilFirstResume) {
  CHECK_INT(R"(
class Main {
  static int n;
  static coroutine void co() { n = 1; yield; n = 2; yield; n = 3; }
  static int f() {
    Coroutine c = co();
    int a = n;
    c.resume(); int b = n;
    c.resume(); int d = n;
    c.resume(); int e = n;
    return a * 1000 + b * 100 + d * 10 + e + (c.isDone() ? 10000 : 0);
  }
})", 0 * 1000 + 1 * 100 + 2 * 10 + 3 + 10000);
}

TEST(VmCoro, YieldKindsAndValues) {
  CHECK_STR(R"(
class Main {
  static coroutine void co() { yield 5; yield 2.5; yield true; yield "s"; yield new Main(); yield; }
  static string f() {
    Coroutine c = co();
    string r = "";
    for (int i = 0; i < 6; i++) { c.resume(); r += c.yieldKind(); }
    c.resume();
    return r + (c.isDone() ? "D" : "R");
  }
})", "123450D");
  CHECK_FLOAT(R"(
class Main {
  static coroutine void co() { yield 5; }
  static float f() { Coroutine c = co(); c.resume(); return c.yieldedFloat(); }
})", 5.0);
  CHECK_STR(R"(
class Main {
  static coroutine void co() { yield "hello"; }
  static string f() { Coroutine c = co(); c.resume(); return c.yieldedString(); }
})", "hello");
  CHECK_THROWS(R"(
class Main {
  static coroutine void co() { yield 5; }
  static string f() { Coroutine c = co(); c.resume(); return c.yieldedString(); }
})", "mingol.ArgumentError");
}

TEST(VmCoro, StateLivesAcrossYieldsIncludingClosuresAndInstanceMethods) {
  CHECK_INT(R"(
class Walker {
  int pos = 0;
  coroutine void walk(int steps) {
    int local = 100;
    fn():void bump = () => { local += 1; };
    for (int i = 0; i < steps; i++) { pos += 2; bump(); yield local; }
  }
}
class Main {
  static int f() {
    Walker w = new Walker();
    Coroutine c = w.walk(3);
    int last = 0;
    while (!c.isDone()) { c.resume(); if (!c.isDone()) { last = c.yieldedInt(); } }
    return w.pos * 1000 + last;
  }
})", 6 * 1000 + 103);
}

TEST(VmCoro, VirtualCoroutinesAndMultipleInstances) {
  CHECK_INT(R"(
class A { coroutine void run() { yield 1; } }
class B extends A { override coroutine void run() { yield 2; } }
class Main {
  static int f() {
    A a = new B();
    Coroutine c = a.run();
    c.resume();
    return c.yieldedInt();
  }
})", 2);
  CHECK_INT(R"(
class Main {
  static coroutine void count(int from) { int i = from; while (true) { yield i; i++; } }
  static int f() {
    Coroutine a = count(10); Coroutine b = count(100);
    a.resume(); b.resume(); a.resume(); b.resume(); a.resume();
    return a.yieldedInt() * 1000 + b.yieldedInt();
  }
})", 12 * 1000 + 101);
}

TEST(VmCoro, ExceptionsAndFinallyInsideCoroutines) {
  CHECK_INT(R"(
class Main {
  static coroutine void co() { try { yield 1; throw new Exception("x"); } catch (Exception e) { yield 2; } yield 3; }
  static int f() {
    Coroutine c = co(); int s = 0;
    for (int i = 0; i < 3; i++) { c.resume(); s = s * 10 + c.yieldedInt(); }
    return s;
  }
})", 123);
  // an exception escaping the coroutine propagates out of resume() and ends it
  CHECK_INT(R"(
class Main {
  static coroutine void co() { yield 1; throw new Exception("boom"); }
  static int f() {
    Coroutine c = co(); c.resume();
    try { c.resume(); } catch (Exception e) { return c.isDone() ? 1 : 0; }
    return -1;
  }
})", 1);
  // resuming a finished coroutine is a no-op
  CHECK_INT(R"(
class Main {
  static coroutine void co() { yield 1; }
  static int f() { Coroutine c = co(); c.resume(); c.resume(); c.resume(); c.resume(); return c.isDone() ? 1 : 0; }
})", 1);
}

TEST(VmCoro, CoroutinesCanDriveOtherCoroutines) {
  CHECK_INT(R"(
class Main {
  static coroutine void inner() { yield 1; yield 2; }
  static coroutine void outer() {
    Coroutine c = inner();
    while (true) { c.resume(); if (c.isDone()) break; yield c.yieldedInt() * 10; }
    yield 99;
  }
  static int f() {
    Coroutine o = outer(); int s = 0;
    for (int i = 0; i < 3; i++) { o.resume(); s = s * 100 + o.yieldedInt(); }
    return s;
  }
})", 10 * 10000 + 20 * 100 + 99);
}

TEST(VmCoro, HostCanDriveCoroutines) {
  auto l = load("class Main { static coroutine void co() { yield 1; yield 2; } static Coroutine make() { return co(); } }");
  REQUIRE(l.ok());
  auto made = callStatic(l, "Main", "make");
  REQUIRE(made.ok);
  Rooted keep(*l.vm, made.value);
  ScriptException ex;
  int sum = 0;
  while (!l.vm->coroutineDone(keep.get())) {
    REQUIRE(l.vm->resumeCoroutine(keep.get(), ex));
    if (!l.vm->coroutineDone(keep.get())) {
      CHECK_EQ(l.vm->coroutineYieldKind(keep.get()), 1);
      sum += static_cast<int>(l.vm->coroutineYielded(keep.get()).u.i);
    }
  }
  CHECK_EQ(sum, 3);
}
