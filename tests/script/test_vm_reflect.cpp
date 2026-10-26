// SPDX-License-Identifier: GPL-2.0-only
// End-to-end tests: reflection, host API, garbage collection.
#include "vm_test_util.hpp"

using namespace mingol::test;

namespace {
const char* kShapes = R"(
interface Named { string name(); }
class Pt implements Named {
  int x = 3; float y = 1.5; bool on = true; string label = "p"; Pt next;
  static int count = 9;
  int add(int a) { return x + a; }
  static int twice(int a) { return a * 2; }
  string name() { return label; }
  void setLabel(string s) { label = s; }
  float scale(float k, int n) { return y * k + n; }
  Pt self() { return this; }
}
class Pt3 extends Pt { int z = 7; override string name() { return "pt3"; } }
abstract class Abs { }
class NoDefault { NoDefault(int a) { } }
class A { static Object[] a0() { return new Object[0]; } static Object[] a1(Object a) { Object[] r = new Object[1]; r[0] = a; return r; } static Object[] a2(Object a, Object b) { Object[] r = new Object[2]; r[0] = a; r[1] = b; return r; } }
class Thrower { void boom() { throw new Exception("from reflection"); } }
)";
std::string with(const std::string& s) { return std::string(kShapes) + s; }
}  // namespace

TEST(VmReflect, TypeInformation) {
  CHECK_STR(with(R"(class Main { static string f() { Type t = typeof(Pt3); return t.getName() + "|" + t.getBase().getName() + "|" + t.getBase().getBase().getName(); } })"), "Pt3|Pt|mingol.Object");
  CHECK_BOOL(with(R"(class Main { static bool f() { Type n = typeof(Named); Type p = typeof(Pt); return n.isInterface() && !p.isInterface() && p.isClass() && typeof(Abs).isAbstract() && !p.isAbstract(); } })"), true);
  CHECK_BOOL(with(R"(class Main { static bool f() { return typeof(Pt).isAssignableFrom(typeof(Pt3)) && !typeof(Pt3).isAssignableFrom(typeof(Pt)) && typeof(Named).isAssignableFrom(typeof(Pt3)) && typeof(Object).isAssignableFrom(typeof(Pt)); } })"), true);
  CHECK_BOOL(with(R"(class Main { static bool f() { Pt p = new Pt3(); return typeof(p) == typeof(Pt3) && typeof(p) != typeof(Pt) && typeof(Pt) == typeof(Pt); } })"), true);
  CHECK_BOOL(with(R"(class Main { static bool f() { return Type.forName("Pt3") == typeof(Pt3) && Type.forName("nope") == null; } })"), true);
  CHECK_STR(R"(enum E { A } class Main { static string f() { return typeof(E).getName() + typeof(int).getName() + typeof(string).getName() + typeof(int[]).getName(); } })", "Eintstringint[]");
  CHECK_BOOL(R"(enum E { A } class Main { static bool f() { return typeof(E).isEnum() && !typeof(int).isClass(); } })", true);
}

TEST(VmReflect, FieldsAndMethodsListing) {
  CHECK_INT(with(R"(class Main { static int f() { return typeof(Pt3).getFields().length; } })"), 7);  // x y on label next count (static) + z
}

TEST(VmReflect, FieldAccess) {
  CHECK_INT(with(R"(class Main { static int f() { Pt p = new Pt(); FieldInfo fx = typeof(Pt).getField("x"); int before = fx.getInt(p); fx.setInt(p, 40); return before * 100 + p.x; } })"), 3 * 100 + 40);
  CHECK_FLOAT(with(R"(class Main { static float f() { Pt p = new Pt(); FieldInfo fy = typeof(Pt).getField("y"); fy.setFloat(p, 2.5); return fy.getFloat(p) + p.y; } })"), 5.0);
  CHECK_BOOL(with(R"(class Main { static bool f() { Pt p = new Pt(); FieldInfo f = typeof(Pt).getField("on"); f.setBool(p, false); return !f.getBool(p) && !p.on; } })"), true);
  CHECK_STR(with(R"(class Main { static string f() { Pt p = new Pt(); FieldInfo f = typeof(Pt).getField("label"); f.setString(p, "new"); return f.getString(p) + p.label; } })"), "newnew");
  CHECK_BOOL(with(R"(class Main { static bool f() { Pt p = new Pt(); Pt q = new Pt(); FieldInfo f = typeof(Pt).getField("next"); f.setObject(p, q); return f.getObject(p) == q && p.next == q; } })"), true);
  CHECK_INT(with(R"(class Main { static int f() { FieldInfo c = typeof(Pt).getField("count"); c.setInt(null, 77); return Pt.count * 100 + c.getInt(null) - 77; } })"), 7700);
  CHECK_INT(with(R"(class Main { static int f() { Pt3 p = new Pt3(); return typeof(Pt3).getField("x").getInt(p) * 100 + typeof(Pt3).getField("z").getInt(p); } })"), 3 * 100 + 7);
  CHECK_BOOL(with(R"(class Main { static bool f() { return typeof(Pt).getField("nope") == null && typeof(Pt).getField("count").isStatic() && !typeof(Pt).getField("x").isStatic(); } })"), true);
  CHECK_STR(with(R"(class Main { static string f() { FieldInfo f = typeof(Pt).getField("x"); return f.getName() + ":" + f.getTypeName(); } })"), "x:int");
}

TEST(VmReflect, FieldAccessErrors) {
  CHECK_THROWS(with(R"(class Main { static int f() { Pt p = new Pt(); return typeof(Pt).getField("y").getInt(p); } })"), "mingol.ArgumentError");
  CHECK_THROWS(with(R"(class Main { static int f() { return typeof(Pt).getField("x").getInt(null); } })"), "mingol.NullReferenceError");
  CHECK_THROWS(with(R"(class Main { static int f() { Pt3 d = new Pt3(); Object o = new Abs2(); return typeof(Pt3).getField("z").getInt(new Pt()); } } class Abs2 { })"), "mingol.ArgumentError");
  CHECK_THROWS(with(R"(class Main { static int f() { Pt p = new Pt(); typeof(Pt).getField("next").setObject(p, new Thrower()); return 0; } })"), "mingol.CastError");
}

TEST(VmReflect, InvokeMethods) {
  CHECK_INT(with(R"(class Main { static int f() { Pt p = new Pt(); MethodInfo m = typeof(Pt).getMethod("add"); Object r = m.invoke(p, A.a1(new IntBox(4))); return (r as IntBox).value; } })"), 7);
  CHECK_INT(with(R"(class Main { static int f() { MethodInfo m = typeof(Pt).getMethod("twice"); return (m.invoke(null, A.a1(new IntBox(21))) as IntBox).value; } })"), 42);
  CHECK_FLOAT(with(R"(class Main { static float f() { Pt p = new Pt(); MethodInfo m = typeof(Pt).getMethod("scale"); return (m.invoke(p, A.a2(new FloatBox(2.0), new IntBox(1))) as FloatBox).value; } })"), 4.0);
  CHECK_STR(with(R"(class Main { static string f() { Pt p = new Pt(); typeof(Pt).getMethod("setLabel").invoke(p, A.a1(new StringBox("zz"))); return (typeof(Pt).getMethod("name").invoke(p, A.a0()) as StringBox).value; } })"), "zz");
  CHECK_BOOL(with(R"(class Main { static bool f() { Pt p = new Pt(); return typeof(Pt).getMethod("self").invoke(p, A.a0()) == p && typeof(Pt).getMethod("setLabel").invoke(p, A.a1(new StringBox("a"))) == null; } })"), true);
  // virtual dispatch through MethodInfo
  CHECK_STR(with(R"(class Main { static string f() { Pt p = new Pt3(); return (typeof(Pt).getMethod("name").invoke(p, A.a0()) as StringBox).value; } })"), "pt3");
  CHECK_INT(with(R"(class Main { static int f() { MethodInfo m = typeof(Pt).getMethod("add"); return m.getParameterCount() * 10 + (m.isStatic() ? 1 : 0) + typeof(Pt).getMethod("twice").getParameterCount(); } })"), 11);
  CHECK_STR(with(R"(class Main { static string f() { MethodInfo m = typeof(Pt).getMethod("scale"); return m.getName() + ":" + m.getReturnTypeName(); } })"), "scale:float");
}

TEST(VmReflect, InvokeErrors) {
  CHECK_THROWS(with(R"(class Main { static int f() { Pt p = new Pt(); typeof(Pt).getMethod("add").invoke(p, A.a0()); return 0; } })"), "mingol.ArgumentError");
  CHECK_THROWS(with(R"(class Main { static int f() { Pt p = new Pt(); typeof(Pt).getMethod("add").invoke(p, A.a1(new StringBox("x"))); return 0; } })"), "mingol.ArgumentError");
  CHECK_THROWS(with(R"(class Main { static int f() { typeof(Pt).getMethod("add").invoke(null, A.a1(new IntBox(1))); return 0; } })"), "mingol.NullReferenceError");
  CHECK_THROWS(with(R"(class Main { static int f() { typeof(Pt).getMethod("add").invoke(new Thrower(), A.a1(new IntBox(1))); return 0; } })"), "mingol.ArgumentError");
  // exceptions thrown by the invoked method propagate unchanged
  CHECK_STR(with(R"(class Main { static string f() { try { typeof(Thrower).getMethod("boom").invoke(new Thrower(), A.a0()); } catch (Exception e) { return e.getMessage(); } return "none"; } })"), "from reflection");
}

TEST(VmReflect, NewInstance) {
  CHECK_INT(with(R"(class Main { static int f() { Pt p = typeof(Pt3).newInstance() as Pt; return p.x * 100 + (p as Pt3).z; } })"), 3 * 100 + 7);
  CHECK_THROWS(with(R"(class Main { static int f() { typeof(Abs).newInstance(); return 0; } })"), "mingol.ArgumentError");
  CHECK_THROWS(with(R"(class Main { static int f() { typeof(NoDefault).newInstance(); return 0; } })"), "mingol.ArgumentError");
  CHECK_THROWS(with(R"(class Main { static int f() { typeof(Named).newInstance(); return 0; } })"), "mingol.ArgumentError");
}

TEST(VmReflect, PreludeTypesCannotBeConstructedByScripts) {
  auto l = load("class Main { static int f() { Type t = new Type(); return 0; } }");
  CHECK(!l.compileErrors.empty());
  auto l2 = load("class Main { static int f() { Coroutine c = new Coroutine(); return 0; } }");
  CHECK(!l2.compileErrors.empty());
}

// ---- host API -------------------------------------------------------------------------------------------

TEST(VmHost, CallConstructAndFields) {
  auto l = load(R"(
class Counter {
  int n; string tag = "t";
  Counter(int start) { n = start; }
  int bump(int by) { n += by; return n; }
  static int twice(int a) { return a * 2; }
}
class Main { static string greet(string who) { return "hi " + who; } }
)");
  REQUIRE(l.ok());
  VM& vm = *l.vm;
  const ClassSym* cc = l.program->findClass("Counter");
  REQUIRE(cc != nullptr);
  const MethodSym* ctor = nullptr;
  for (const auto& m : cc->methods) if (m->mkind == MethodKind::Constructor) ctor = m.get();
  REQUIRE(ctor != nullptr);
  ScriptException ex;
  Value obj;
  REQUIRE(vm.construct(*cc, *ctor, std::vector<Value>{makeInt(10)}, obj, ex));
  Rooted keep(vm, obj);
  const MethodSym* bump = l.program->findMethod("Counter", "bump");
  Value r;
  REQUIRE(vm.call(*bump, keep.get(), std::vector<Value>{makeInt(5)}, r, ex));
  CHECK_EQ(r.u.i, 15);
  const FieldSym* n = cc->findField("n");
  CHECK_EQ(vm.getField(keep.get(), *n).u.i, 15);
  vm.setField(keep.get(), *n, makeInt(100));
  REQUIRE(vm.call(*bump, keep.get(), std::vector<Value>{makeInt(1)}, r, ex));
  CHECK_EQ(r.u.i, 101);
  const FieldSym* tag = cc->findField("tag");
  CHECK_EQ(std::string(vm.stringValue(vm.getField(keep.get(), *tag))), "t");
  REQUIRE(vm.call(*l.program->findMethod("Counter", "twice"), makeNull(), std::vector<Value>{makeInt(21)}, r, ex));
  CHECK_EQ(r.u.i, 42);
  Value s = vm.newString("bob");
  Rooted keepS(vm, s);
  REQUIRE(vm.call(*l.program->findMethod("Main", "greet"), makeNull(), std::vector<Value>{keepS.get()}, r, ex));
  CHECK_EQ(std::string(vm.stringValue(r)), "hi bob");
  CHECK(vm.classOf(keep.get()) == cc);
}

TEST(VmHost, ErrorsAreReportedNotCrashes) {
  auto l = load("class Main { static int boom(int a) { return 10 / a; } int inst() { return 1; } }");
  REQUIRE(l.ok());
  ScriptException ex;
  Value r;
  CHECK(!l.vm->call(*l.program->findMethod("Main", "boom"), makeNull(), std::vector<Value>{makeInt(0)}, r, ex));
  CHECK_EQ(ex.type, "mingol.DivideByZeroError");
  CHECK(!l.vm->call(*l.program->findMethod("Main", "boom"), makeNull(), std::vector<Value>{}, r, ex));  // wrong argc
  CHECK_EQ(ex.type, "mingol.ArgumentError");
  CHECK(!l.vm->call(*l.program->findMethod("Main", "inst"), makeNull(), std::vector<Value>{}, r, ex));  // null receiver
  CHECK_EQ(ex.type, "mingol.NullReferenceError");
  // the VM is still usable afterwards
  CHECK(l.vm->call(*l.program->findMethod("Main", "boom"), makeNull(), std::vector<Value>{makeInt(5)}, r, ex));
  CHECK_EQ(r.u.i, 2);
}

TEST(VmHost, CallClosureFromHost) {
  auto l = load("class Main { static fn(int):int make(int k) { return (int x) => x * k; } static fn(int):int bound; static void init() { bound = new Main().m; } int base = 5; int m(int x) { return x + base; } }");
  REQUIRE(l.ok());
  ScriptException ex;
  Value clo;
  REQUIRE(l.vm->call(*l.program->findMethod("Main", "make"), makeNull(), std::vector<Value>{makeInt(6)}, clo, ex));
  Rooted keep(*l.vm, clo);
  Value r;
  REQUIRE(l.vm->callClosure(keep.get(), std::vector<Value>{makeInt(7)}, r, ex));
  CHECK_EQ(r.u.i, 42);
  REQUIRE(l.vm->call(*l.program->findMethod("Main", "init"), makeNull(), {}, r, ex));
  const FieldSym* bf = l.program->findClass("Main")->findField("bound");
  REQUIRE(bf != nullptr);
  REQUIRE(l.vm->callClosure(l.vm->getStatic(*bf), std::vector<Value>{makeInt(1)}, r, ex));
  CHECK_EQ(r.u.i, 6);  // bound method value keeps its receiver
  CHECK(!l.vm->callClosure(makeNull(), {}, r, ex));
}

// ---- garbage collector ----------------------------------------------------------------------------------

TEST(VmGc, CollectsUnreachableObjects) {
  VMOptions opt;
  opt.gcThresholdBytes = 64u << 20;  // no automatic collection in this test
  auto l = load("class Node { Node next; int v; } class Main { static int make(int n) { Node head = null; for (int i = 0; i < n; i++) { Node x = new Node(); x.v = i; x.next = head; head = x; } return head.v; } }", opt);
  REQUIRE(l.ok());
  l.vm->collectGarbage();
  const auto base = l.vm->heapStats();
  auto o = callStatic(l, "Main", "make", {makeInt(1000)});
  REQUIRE(o.ok);
  CHECK_EQ(o.value.u.i, 999);
  const auto grown = l.vm->heapStats();
  CHECK(grown.liveObjects >= base.liveObjects + 1000);
  l.vm->collectGarbage();
  const auto after = l.vm->heapStats();
  CHECK(after.liveObjects <= base.liveObjects + 2);
  CHECK(after.collections > base.collections);
}

TEST(VmGc, StaticsPinsAndStacksKeepObjectsAlive) {
  auto l = load("class Box { int v; string s = \"x\"; } class Main { static Box keep; static void put(int v) { keep = new Box(); keep.v = v; keep.s = \"kept\" + v; } static Box make() { Box b = new Box(); b.v = 5; return b; } static int get() { return keep.v; } }");
  REQUIRE(l.ok());
  REQUIRE(callStatic(l, "Main", "put", {makeInt(77)}).ok);
  auto made = callStatic(l, "Main", "make");
  REQUIRE(made.ok);
  Rooted pinned(*l.vm, made.value);
  for (int i = 0; i < 3; ++i) l.vm->collectGarbage();
  auto g = callStatic(l, "Main", "get");
  REQUIRE(g.ok);
  CHECK_EQ(g.value.u.i, 77);
  const FieldSym* v = l.program->findClass("Box")->findField("v");
  CHECK_EQ(l.vm->getField(pinned.get(), *v).u.i, 5);
  const FieldSym* s = l.program->findClass("Main")->findField("keep");
  const FieldSym* bs = l.program->findClass("Box")->findField("s");
  CHECK_EQ(std::string(l.vm->stringValue(l.vm->getField(l.vm->getStatic(*s), *bs))), "kept77");
}

TEST(VmGc, StressWithAutomaticCollectionKeepsLiveDataIntact) {
  VMOptions opt;
  opt.gcThresholdBytes = 4096;  // collect extremely often
  auto l = load(R"(
class Node { Node next; int v; string tag; int[] data; fn():int get; }
class Main {
  static int f() {
    Node head = null;
    int checksum = 0;
    for (int round = 0; round < 200; round++) {
      Node n = new Node();
      n.v = round; n.tag = "n" + round; n.data = new int[8]; n.data[3] = round * 2;
      int captured = round * 3;
      n.get = () => captured + n.v;
      n.next = head;
      head = n;
      if (round % 50 == 49 && round < 199) { head = null; }
      string junk = "";
      for (int j = 0; j < 20; j++) { junk += j; }
    }
    int count = 0;
    for (Node p = head; p != null; p = p.next) { checksum += p.v + p.data[3] + p.get() + p.tag.length; count++; }
    return checksum * 1000 + count;
  }
})", opt);
  REQUIRE(l.ok());
  auto o = runMain(l);
  REQUIRE(o.ok);
  // reference computed independently: the live list is rounds 150..199
  std::int64_t checksum = 0;
  for (int r = 150; r < 200; ++r) checksum += r + r * 2 + (r * 3 + r) + static_cast<std::int64_t>(std::to_string(r).size() + 1);
  CHECK_EQ(o.value.u.i, checksum * 1000 + 50);
  CHECK(l.vm->heapStats().collections > 20);
}

TEST(VmGc, CyclesAreCollected) {
  VMOptions opt;
  opt.gcThresholdBytes = 1u << 30;
  auto l = load("class N { N other; } class Main { static void churn(int n) { for (int i = 0; i < n; i++) { N a = new N(); N b = new N(); a.other = b; b.other = a; } } }", opt);
  REQUIRE(l.ok());
  l.vm->collectGarbage();
  const auto base = l.vm->heapStats().liveObjects;
  REQUIRE(callStatic(l, "Main", "churn", {makeInt(500)}).ok);
  l.vm->collectGarbage();
  CHECK(l.vm->heapStats().liveObjects <= base + 1);
}

TEST(VmGc, CoroutineStateSurvivesCollection) {
  VMOptions opt;
  opt.gcThresholdBytes = 2048;
  auto l = load(R"(
class Main {
  static coroutine void co(int n) { string acc = ""; int[] arr = new int[4]; for (int i = 0; i < n; i++) { acc += i; arr[i % 4] += i; string junk = ""; for (int j = 0; j < 30; j++) { junk += "x"; } yield acc.length; } yield arr[0] * 1000 + arr[1]; }
  static int f() {
    Coroutine c = co(6); int last = 0;
    while (!c.isDone()) { c.resume(); if (!c.isDone()) last = c.yieldedInt(); }
    return last;
  }
})", opt);
  REQUIRE(l.ok());
  auto o = runMain(l);
  REQUIRE(o.ok);
  // arr[0] = 0+4 = 4, arr[1] = 1+5 = 6
  CHECK_EQ(o.value.u.i, 4 * 1000 + 6);
}

TEST(VmGc, OutOfMemoryIsAnExceptionNotACrash) {
  VMOptions opt;
  opt.maxHeapBytes = 2u << 20;
  auto l = load("class Main { static int f() { int[][] keep = new int[100][]; for (int i = 0; i < 100; i++) { keep[i] = new int[100000]; } return 1; } }", opt);
  REQUIRE(l.ok());
  auto o = runMain(l);
  CHECK(!o.ok);
  CHECK_EQ(o.ex.type, "mingol.OutOfMemoryError");
}
