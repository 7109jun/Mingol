// SPDX-License-Identifier: GPL-2.0-only
// Hot reload: recompile and migrate live script objects by field name and type.
#include "scripting_util.hpp"

using namespace mingol;
using namespace mingol::test;
using namespace mingol::scripting;

namespace {
const char* kV1 = R"(
class Trace { static string s = ""; }
class Walker extends Script {
  public int steps = 0;
  public float speed = 1.0;
  public string label = "v1";
  public bool flag = false;
  public int[] history;
  override void onAwake() { history = new int[3]; }
  override void onUpdate(float dt) {
    steps++;
    history[steps % 3] = steps;
    gameObject.setPosition(new Vec3(speed * steps, 0.0, 0.0));
  }
}
)";
}  // namespace

TEST(Reload, MigratesFieldsAndSwitchesBehaviour) {
  auto host = makeHost(kV1);
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("w");
  auto* c = host->addScript(*go, "Walker").value();
  REQUIRE(c->setFloat("speed", 2.0).ok());
  REQUIRE(c->setString("label", "custom").ok());
  REQUIRE(c->setBool("flag", true).ok());
  for (int i = 0; i < 4; ++i) sc.update(0.016F);   // steps == 4
  CHECK_EQ(c->getInt("steps").value(), std::int64_t{4});

  const char* v2 = R"(
class Trace { static string s = ""; }
class Walker extends Script {
  public int steps = 100;          // initial value must NOT override the migrated one
  public float speed = 1.0;
  public string label = "v2";
  public bool flag = false;
  public int[] history;
  public int extra = 7;            // new field: keeps its initialiser
  override void onUpdate(float dt) {
    steps += 10;                   // changed behaviour
    gameObject.setPosition(new Vec3(-1.0 * steps, 0.0, 0.0));
  }
}
)";
  auto rep = host->reload({{"test.mins", v2}});
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().componentsMigrated, std::size_t{1});
  CHECK_EQ(rep.value().componentsDisabled, std::size_t{0});
  CHECK(rep.value().fieldsRestored >= 5);
  CHECK_EQ(c->getInt("steps").value(), std::int64_t{4});
  CHECK_EQ(c->getFloat("speed").value(), 2.0);
  CHECK_EQ(c->getString("label").value(), "custom");
  CHECK(c->getBool("flag").value());
  CHECK_EQ(c->getInt("extra").value(), std::int64_t{7});
  sc.update(0.016F);
  CHECK_EQ(c->getInt("steps").value(), std::int64_t{14});
  CHECK_NEAR(go->transform().localPosition().x, -14.0F, 1e-4F);   // the new code ran, with a working gameObject handle
  CHECK_EQ(sc.callbackFailures(), std::size_t{0});
}

TEST(Reload, ArraysAreMigrated) {
  auto host = makeHost(R"(
class Keep extends Script {
  public int[] a; public string[] names; public float[] fs;
  override void onAwake() { a = new int[3]; a[0] = 5; a[2] = 9; names = new string[2]; names[0] = "x"; names[1] = "yé"; fs = new float[1]; fs[0] = 0.5; }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* c = host->addScript(*sc.createObject("k"), "Keep").value();
  auto rep = host->reload({{"test.mins", R"(
class Keep extends Script {
  public int[] a; public string[] names; public float[] fs;
  public string dump() { return a.length + ":" + a[0] + a[1] + a[2] + ":" + names[0] + names[1] + ":" + fs[0]; }
}
)"}});
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().fieldsRestored, std::size_t{3});
  // call the new method through the component API
  REQUIRE(c->hasInstance());
  const auto* m = host->program().findMethod("Keep", "dump");
  REQUIRE(m != nullptr);
  script::Value r;
  script::ScriptException ex;
  REQUIRE(host->vm().call(*m, c->instance(), {}, r, ex));
  CHECK_EQ(std::string(host->vm().stringValue(r)), "3:509:xyé:0.5");
}

TEST(Reload, RetypedAndRenamedFieldsAreSkippedAndReported) {
  auto host = makeHost("class R extends Script { public int a = 1; public float b = 2.0; public int gone = 3; }");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* c = host->addScript(*sc.createObject("r"), "R").value();
  REQUIRE(c->setInt("a", 11).ok());
  REQUIRE(c->setInt("gone", 33).ok());
  auto rep = host->reload({{"test.mins", "class R extends Script { public string a = \"s\"; public float b = 9.0; public int renamed = 4; }"}});
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().fieldsRestored, std::size_t{1});   // b
  CHECK_EQ(rep.value().fieldsSkipped, std::size_t{2});    // a (retyped), gone (missing)
  CHECK_EQ(c->getString("a").value(), "s");
  CHECK_EQ(c->getFloat("b").value(), 2.0);
  CHECK_EQ(c->getInt("renamed").value(), std::int64_t{4});
}

TEST(Reload, CompileErrorLeavesEverythingRunning) {
  auto host = makeHost(kV1);
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("w");
  auto* c = host->addScript(*go, "Walker").value();
  sc.update(0.016F);
  sc.update(0.016F);
  const auto steps = c->getInt("steps").value();
  auto rep = host->reload({{"test.mins", "class Walker extends Script { this is not valid }"}});
  CHECK(!rep.ok());
  CHECK(rep.error().message.find("test.mins:1") != std::string::npos);
  CHECK(c->hasInstance());
  sc.update(0.016F);
  CHECK_EQ(c->getInt("steps").value(), steps + 1);   // old code still runs
  CHECK(c->enabled());
}

TEST(Reload, RemovedClassDisablesOnlyItsComponents) {
  auto host = makeHost(R"(
class A extends Script { public int n; override void onUpdate(float dt) { n++; } }
class B extends Script { public int n; override void onUpdate(float dt) { n++; } }
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* a = host->addScript(*sc.createObject("a"), "A").value();
  auto* b = host->addScript(*sc.createObject("b"), "B").value();
  sc.update(0.01F);
  sc.update(0.01F);
  auto rep = host->reload({{"test.mins", "class A extends Script { public int n; override void onUpdate(float dt) { n += 100; } }"}});
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().componentsMigrated, std::size_t{1});
  CHECK_EQ(rep.value().componentsDisabled, std::size_t{1});
  CHECK(rep.value().messages.size() == 1);
  CHECK(!b->enabled());
  CHECK(!b->hasInstance());
  sc.update(0.01F);
  CHECK_EQ(a->getInt("n").value(), std::int64_t{102});
  CHECK_EQ(sc.callbackFailures(), std::size_t{0});
}

TEST(Reload, StaticsAreResetAndStaticInitialisersRunAgain) {
  auto host = makeHost("class Counter { static int n = 5; } class S extends Script { override void onUpdate(float dt) { Counter.n++; } }");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  REQUIRE(host->addScript(*sc.createObject("s"), "S").ok());
  sc.update(0.01F); sc.update(0.01F); sc.update(0.01F);
  const auto* f = host->program().findClass("Counter")->findField("n");
  CHECK_EQ(host->vm().getStatic(*f).u.i, std::int64_t{8});
  REQUIRE(host->reload({{"test.mins", "class Counter { static int n = 50; } class S extends Script { override void onUpdate(float dt) { Counter.n++; } }"}}).ok());
  f = host->program().findClass("Counter")->findField("n");
  CHECK_EQ(host->vm().getStatic(*f).u.i, std::int64_t{50});
}

TEST(Reload, LifecycleStateIsNotRepeated) {
  auto host = makeHost(R"(
class Trace { static string s = ""; }
class L extends Script {
  override void onAwake() { Trace.s += "A"; } override void onStart() { Trace.s += "S"; } override void onUpdate(float dt) { Trace.s += "U"; }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  REQUIRE(host->addScript(*sc.createObject("l"), "L").ok());
  sc.update(0.01F); sc.update(0.01F);
  // statics are reset by a reload, so only what happens afterwards is traced
  REQUIRE(host->reload({{"test.mins", R"(
class Trace { static string s = ""; }
class L extends Script {
  override void onAwake() { Trace.s += "A"; } override void onStart() { Trace.s += "S"; } override void onUpdate(float dt) { Trace.s += "U"; }
}
)"}}).ok());
  sc.update(0.01F);
  CHECK_EQ(traceOf(*host), "U");   // no second onAwake/onStart
}

TEST(Reload, RepeatedReloadsUnderGcPressureAndDestroyAfterReload) {
  HostDesc desc;
  desc.vm.gcThresholdBytes = 4096;
  auto host = makeHost(kV1, desc);
  REQUIRE(host != nullptr);
  scene::Scene sc;
  std::vector<ScriptComponent*> cs;
  for (int i = 0; i < 20; ++i) cs.push_back(host->addScript(*sc.createObject("w" + std::to_string(i)), "Walker").value());
  for (int round = 0; round < 25; ++round) {
    sc.update(0.016F);
    auto rep = host->reload({{"test.mins", kV1}});
    REQUIRE(rep.ok());
    CHECK_EQ(rep.value().componentsMigrated, std::size_t{20});
  }
  for (auto* c : cs) CHECK_EQ(c->getInt("steps").value(), std::int64_t{25});
  CHECK_EQ(sc.callbackFailures(), std::size_t{0});
  // destroying objects after reloads must work (onDestroy in the new VM)
  for (int i = 0; i < 20; ++i) sc.destroy(sc.find("w" + std::to_string(i)));
  sc.flushDestroyed();
  CHECK_EQ(sc.objectCount(), std::size_t{0});
}

TEST(Reload, ReloadWithAScriptThrowingInTheConstructorDisablesTheComponent) {
  auto host = makeHost("class C extends Script { public int n = 1; }");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* c = host->addScript(*sc.createObject("c"), "C").value();
  auto rep = host->reload({{"test.mins", "class C extends Script { public int n = 1; C() { throw new Exception(\"ctor fails\"); } }"}});
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().componentsDisabled, std::size_t{1});
  CHECK(!c->enabled());
  CHECK(!c->hasInstance());
  // a later good reload does not resurrect it by itself, but must not crash either
  REQUIRE(host->reload({{"test.mins", "class C extends Script { public int n = 1; }"}}).ok());
}

TEST(Reload, StaticInitFailureRejectsTheReload) {
  auto host = makeHost("class K { static int v = 1; } class C extends Script { public int n = 3; }");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* c = host->addScript(*sc.createObject("c"), "C").value();
  auto rep = host->reload({{"test.mins", "class K { static int v = 1 / (1 - 1); } class C extends Script { public int n = 3; }"}});
  CHECK(!rep.ok());
  CHECK(c->hasInstance());
  CHECK_EQ(c->getInt("n").value(), std::int64_t{3});
}
