// SPDX-License-Identifier: GPL-2.0-only
// Native plugin ABI: loading a C shared library, binding natives, errors, GC interaction.
#include "vm_test_util.hpp"

using namespace mingol::test;

namespace {
const char* kDecl = R"(
public native class Native {
  public static native int addInts(int a, int b);
  public static native float halve(float x);
  public static native string shout(string s);
  public static native void fail();
  public static native int[] iota(int n);
  public static native int sumArray(int[] a);
  public static native int finalizedCount();
  public static native int applyTwice(fn(int):int f, int x);
  public static native Point makePoint(int x, int y);
}
public native class Handle { public native void attach(); public native int read(); }
class Point { int x; int y; }
public native class Unbound { public static native int nothing(); }
)";
Loaded loadWithPlugin(const std::string& body, std::string* err = nullptr, VMOptions opt = {}) {
  Loaded l = load(std::string(kDecl) + body, std::move(opt));
  if (!l.vm) return l;
  auto r = l.vm->loadPlugin(MINGOL_TEST_PLUGIN);
  if (!r.ok() && err) *err = r.error().message;
  if (!r.ok() && !err) { l.initError = r.error().message; mingol::test::fail(__FILE__, __LINE__, "loadPlugin: " + l.initError); }
  return l;
}
}  // namespace

TEST(Plugin, LoadsAndBindsNatives) {
  auto l = loadWithPlugin("class Main { static int f() { return Native.addInts(40, 2); } }");
  REQUIRE(l.ok());
  auto o = runMain(l);
  REQUIRE(o.ok);
  CHECK_EQ(o.value.u.i, 42);
  // only the intentionally unbound native remains
  auto un = l.vm->unboundNatives();
  bool sawUnbound = false;
  for (const auto& n : un) { if (n.find("Unbound.nothing") != std::string::npos) sawUnbound = true; CHECK(n.rfind("mingol.", 0) != 0); }
  CHECK(sawUnbound);
}

TEST(Plugin, ValueMarshalling) {
  auto l = loadWithPlugin(R"(class Main {
    static string f() {
      int[] sq = Native.iota(5);
      return Native.halve(5.0) + "|" + Native.shout("hey") + "|" + sq[4] + "|" + Native.sumArray(sq) + "|" + Native.sumArray(new int[0]);
    } })");
  REQUIRE(l.ok());
  auto o = runMain(l);
  REQUIRE(o.ok);
  CHECK_EQ(std::string(l.vm->stringValue(o.value)), "2.5|hey!|16|30|0");
}

TEST(Plugin, ErrorsBecomeScriptExceptions) {
  auto l = loadWithPlugin(R"(class Main {
    static string f() { try { Native.fail(); } catch (ArgumentError e) { return e.getMessage(); } return "none"; }
    static string g() { string big = ""; for (int i = 0; i < 200; i++) { big += "x"; } try { Native.shout(big); } catch (Exception e) { return e.getMessage(); } return "none"; }
  })");
  REQUIRE(l.ok());
  auto o = callStatic(l, "Main", "f");
  REQUIRE(o.ok);
  CHECK_EQ(std::string(l.vm->stringValue(o.value)), "plugin says no");
  auto o2 = callStatic(l, "Main", "g");
  REQUIRE(o2.ok);
  CHECK_EQ(std::string(l.vm->stringValue(o2.value)), "too long");
}

TEST(Plugin, UnboundNativeThrows) {
  auto l = loadWithPlugin("class Main { static int f() { return Unbound.nothing(); } }");
  REQUIRE(l.ok());
  auto o = runMain(l);
  CHECK(!o.ok);
  CHECK(o.ex.message.find("not bound") != std::string::npos);
}

TEST(Plugin, CallbacksIntoScriptAndNewObject) {
  auto l = loadWithPlugin(R"(class Main {
    static int f() { int k = 3; return Native.applyTwice((int x) => x * k, 2); }
    static int g() { Point p = Native.makePoint(7, 9); return p.x * 100 + p.y; }
    static string h() { try { Native.applyTwice((int x) => 10 / (x - x), 1); } catch (DivideByZeroError e) { return "propagated"; } return "swallowed"; }
  })");
  REQUIRE(l.ok());
  auto a = callStatic(l, "Main", "f"); REQUIRE(a.ok); CHECK_EQ(a.value.u.i, 18);
  auto b = callStatic(l, "Main", "g"); REQUIRE(b.ok); CHECK_EQ(b.value.u.i, 709);
  auto c = callStatic(l, "Main", "h"); REQUIRE(c.ok); CHECK_EQ(std::string(l.vm->stringValue(c.value)), "propagated");
}

TEST(Plugin, NativeDataAndFinalizer) {
  VMOptions opt;
  opt.gcThresholdBytes = 1u << 30;
  auto l = loadWithPlugin(R"(class Main {
    static int make() { Handle h = new Handle(); h.attach(); return h.read(); }
    static int fin() { return Native.finalizedCount(); }
  })", nullptr, opt);
  REQUIRE(l.ok());
  auto before = callStatic(l, "Main", "fin"); REQUIRE(before.ok);
  auto m = callStatic(l, "Main", "make"); REQUIRE(m.ok);
  CHECK(m.value.u.i >= 10);
  l.vm->collectGarbage();
  auto after = callStatic(l, "Main", "fin"); REQUIRE(after.ok);
  CHECK_EQ(after.value.u.i, before.value.u.i + 1);
}

TEST(Plugin, LoadFailures) {
  auto l = load(std::string(kDecl) + "class Main { static int f() { return 1; } }");
  REQUIRE(l.ok());
  CHECK(!l.vm->loadPlugin("/definitely/not/there.so").ok());
  CHECK(!l.vm->loadPlugin(MINGOL_TEST_PLUGIN_BAD).ok());       // wrong ABI version
  CHECK(!l.vm->loadPlugin(std::string(MINGOL_TEST_PLUGIN) + ".nope").ok());
  // a plugin registering a native the program does not declare is rejected
  auto l2 = load("class Main { static int f() { return 1; } }");
  REQUIRE(l2.ok());
  CHECK(!l2.vm->loadPlugin(MINGOL_TEST_PLUGIN).ok());
}
