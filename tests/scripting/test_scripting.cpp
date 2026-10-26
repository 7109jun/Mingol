// SPDX-License-Identifier: GPL-2.0-only
// ScriptHost / ScriptComponent: lifecycle, engine API, error handling.
#include "scripting_util.hpp"

using namespace mingol;
using namespace mingol::test;
using namespace mingol::scripting;

TEST(Scripting, EngineApiCompilesAndUserScriptsAttach) {
  auto host = makeHost("class Spin extends Script { }");
  REQUIRE(host != nullptr);
  CHECK(host->isScriptClass("Spin"));
  CHECK(!host->isScriptClass("Script"));
  CHECK(!host->isScriptClass("Vec3"));
  CHECK(!host->isScriptClass("Nope"));
  scene::Scene sc;
  auto* go = sc.createObject("a");
  auto r = host->addScript(*go, "Spin");
  REQUIRE(r.ok());
  CHECK(r.value()->hasInstance());
  CHECK_EQ(go->getComponents<ScriptComponent>().size(), std::size_t{1});
}

TEST(Scripting, RejectsInvalidScriptClasses) {
  auto host = makeHost(R"(
abstract class Abs extends Script { }
class Plain { }
class NeedsArg extends Script { NeedsArg(int a) { } }
class PrivCtor extends Script { private PrivCtor() { } }
class Good extends Script { }
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("a");
  CHECK(!host->addScript(*go, "Abs").ok());
  CHECK(!host->addScript(*go, "Plain").ok());
  CHECK(!host->addScript(*go, "NeedsArg").ok());
  CHECK(!host->addScript(*go, "PrivCtor").ok());
  CHECK(!host->addScript(*go, "Missing").ok());
  CHECK(host->addScript(*go, "Good").ok());
  CHECK_EQ(go->componentCount(), std::size_t{1});
}

TEST(Scripting, CompileErrorsAreReported) {
  std::string msg;
  auto h = ScriptHost::create({{"bad.mins", "class X extends Script { void onUpdate(float dt) { int x = \"s\"; } }"}});
  CHECK(!h.ok());
  CHECK(h.error().message.find("bad.mins:1") != std::string::npos);
}

TEST(Scripting, LifecycleOrderMatchesTheScene) {
  auto host = makeHost(R"(
class Trace { static string s = ""; }
class L extends Script {
  override void onAwake() { Trace.s += "A"; }
  override void onEnable() { Trace.s += "E"; }
  override void onStart() { Trace.s += "S"; }
  override void onFixedUpdate(float dt) { Trace.s += "F"; }
  override void onUpdate(float dt) { Trace.s += "U"; }
  override void onLateUpdate(float dt) { Trace.s += "L"; }
  override void onDisable() { Trace.s += "d"; }
  override void onDestroy() { Trace.s += "X"; }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("a");
  auto* c = host->addScript(*go, "L").value();
  CHECK_EQ(traceOf(*host), "AE");
  sc.fixedUpdate(0.02F);
  sc.update(0.016F);
  sc.lateUpdate(0.016F);
  CHECK_EQ(traceOf(*host), "AESFUL");
  sc.destroy(go);
  sc.flushDestroyed();
  CHECK_EQ(traceOf(*host), "AESFULdX");
  (void)c;
}

TEST(Scripting, OnUpdateMovesTheObjectThroughTheEngineApi) {
  auto host = makeHost(R"(
class Mover extends Script {
  public float speed = 2.0;
  override void onUpdate(float dt) {
    Vec3 p = gameObject.getPosition();
    gameObject.setPosition(new Vec3(p.x + speed * dt, p.y, p.z));
    gameObject.rotate(new Vec3(0.0, 1.0, 0.0), 90.0 * dt);
  }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("m");
  auto* c = host->addScript(*go, "Mover").value();
  REQUIRE(c->setFloat("speed", 4.0).ok());
  for (int i = 0; i < 10; ++i) sc.update(0.1F);   // first update happens after onStart
  CHECK_NEAR(go->transform().localPosition().x, 4.0F, 1e-4F);
  // rotated 90 degrees about +Y in total: forward (-Z) now points along -X
  const Vec3 fwd = go->transform().forward();
  CHECK_NEAR(fwd.x, -1.0F, 1e-4F);
  CHECK_NEAR(fwd.z, 0.0F, 1e-4F);
}

TEST(Scripting, GameObjectApi) {
  auto host = makeHost(R"(
class Api extends Script {
  public string log = "";
  override void onAwake() {
    gameObject.setName("renamed");
    GameObject child = GameObject.create("kid");
    log += child.getName() + "," + (child.getParent() == null ? "root" : "has-parent") + ",";
    GameObject found = GameObject.find("kid");
    log += (found != null && found.same(child) ? "found" : "missing") + ",";
    log += (GameObject.find("nope") == null ? "none" : "some") + ",";
    child.setPosition(new Vec3(1.0, 2.0, 3.0));
    child.setScale(new Vec3(2.0, 2.0, 2.0));
    Vec3 s = child.getScale();
    log += s.x + ",";
    child.setActive(false);
    log += (child.isActive() ? "on" : "off") + ",";
    child.destroy();
  }
  override void onUpdate(float dt) {
    GameObject kid = GameObject.find("kid");
    log += (kid == null ? "gone" : "still");
  }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("orig");
  auto* c = host->addScript(*go, "Api").value();
  CHECK_EQ(go->name(), "renamed");
  sc.flushDestroyed();
  sc.update(0.016F);
  CHECK_EQ(c->getString("log").value(), "kid,root,found,none,2.0,off,gone");
  CHECK(sc.find("kid") == nullptr);
}

TEST(Scripting, DestroyedHandlesThrowAndReportNotExisting) {
  auto host = makeHost(R"(
class H extends Script {
  public string r = "";
  public GameObject saved;
  override void onAwake() { saved = GameObject.create("tmp"); saved.destroy(); }
  override void onUpdate(float dt) {
    r += saved.exists() ? "exists" : "dead";
    try { saved.getName(); r += ",no-throw"; } catch (NullReferenceError e) { r += ",threw"; }
  }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("a");
  auto* c = host->addScript(*go, "H").value();
  sc.flushDestroyed();
  sc.update(0.016F);
  CHECK_EQ(c->getString("r").value(), "dead,threw");
}

TEST(Scripting, ExceptionDisablesOnlyThatComponent) {
  auto host = makeHost(R"(
class Bad extends Script { public int n; override void onUpdate(float dt) { n++; throw new Exception("kaboom"); } }
class Good extends Script { public int n; override void onUpdate(float dt) { n++; } }
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* a = sc.createObject("a");
  auto* b = sc.createObject("b");
  auto* bad = host->addScript(*a, "Bad").value();
  auto* good = host->addScript(*b, "Good").value();
  for (int i = 0; i < 3; ++i) sc.update(0.016F);
  CHECK_EQ(bad->getInt("n").value(), std::int64_t{1});   // disabled after the first failure
  CHECK(!bad->enabled());
  CHECK_EQ(good->getInt("n").value(), std::int64_t{3});
  CHECK_EQ(sc.callbackFailures(), std::size_t{1});
}

TEST(Scripting, StackOverflowInScriptIsAContainedFailure) {
  auto host = makeHost("class Rec extends Script { int f(int n) { return f(n + 1) + 1; } override void onUpdate(float dt) { f(0); } }");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("a");
  auto* c = host->addScript(*go, "Rec").value();
  sc.update(0.016F);
  CHECK(!c->enabled());
  CHECK_EQ(sc.callbackFailures(), std::size_t{1});
}

TEST(Scripting, FieldAccessorsValidateNamesAndTypes) {
  auto host = makeHost("class F extends Script { public int i = 1; public float f = 1.5; public bool b; public string s = \"x\"; static int st; }");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("a");
  auto* c = host->addScript(*go, "F").value();
  CHECK_EQ(c->getInt("i").value(), std::int64_t{1});
  CHECK(c->setInt("i", 9).ok());
  CHECK_EQ(c->getInt("i").value(), std::int64_t{9});
  CHECK(c->setFloat("f", 2.5).ok());
  CHECK_EQ(c->getFloat("f").value(), 2.5);
  CHECK(c->setBool("b", true).ok());
  CHECK(c->getBool("b").value());
  CHECK(c->setString("s", "héllo").ok());
  CHECK_EQ(c->getString("s").value(), "héllo");
  CHECK(!c->setInt("f", 1).ok());       // wrong type
  CHECK(!c->setInt("nope", 1).ok());    // unknown
  CHECK(!c->setInt("st", 1).ok());      // static
  CHECK(!c->getString("i").ok());
}

TEST(Scripting, DeactivationAndActivationCallbacks) {
  auto host = makeHost(R"(
class T extends Script {
  public int enables; public int disables; public int updates;
  override void onEnable() { enables++; } override void onDisable() { disables++; } override void onUpdate(float dt) { updates++; }
}
)");
  REQUIRE(host != nullptr);
  scene::Scene sc;
  auto* go = sc.createObject("a");
  auto* c = host->addScript(*go, "T").value();
  sc.update(0.01F);
  go->setActive(false);
  sc.update(0.01F);
  go->setActive(true);
  sc.update(0.01F);
  CHECK_EQ(c->getInt("enables").value(), std::int64_t{2});
  CHECK_EQ(c->getInt("disables").value(), std::int64_t{1});
  CHECK_EQ(c->getInt("updates").value(), std::int64_t{2});
}

TEST(Scripting, ManyObjectsUnderGcPressure) {
  HostDesc desc;
  desc.vm.gcThresholdBytes = 8192;
  auto host = makeHost(R"(
class Churn extends Script {
  public int[] data; public string tag; public int ticks;
  override void onAwake() { data = new int[16]; tag = "obj"; }
  override void onUpdate(float dt) {
    string junk = ""; for (int i = 0; i < 10; i++) { junk += i; }
    data[ticks % 16] += ticks; ticks++; tag = "obj" + ticks;
  }
}
)", desc);
  REQUIRE(host != nullptr);
  scene::Scene sc;
  std::vector<ScriptComponent*> cs;
  for (int i = 0; i < 50; ++i) cs.push_back(host->addScript(*sc.createObject("o" + std::to_string(i)), "Churn").value());
  for (int f = 0; f < 40; ++f) sc.update(0.016F);
  for (auto* c : cs) {
    CHECK_EQ(c->getInt("ticks").value(), std::int64_t{40});  
    CHECK_EQ(c->getString("tag").value(), "obj40");
  }
  CHECK(host->vm().heapStats().collections > 3);
  CHECK_EQ(sc.callbackFailures(), std::size_t{0});
}

TEST(Scripting, NoLeaksOfComponents) {
  const auto before = scene::Component::liveCount();
  {
    auto host = makeHost("class A extends Script { override void onUpdate(float dt) { } }");
    scene::Scene sc;
    for (int i = 0; i < 20; ++i) REQUIRE(host->addScript(*sc.createObject("o"), "A").ok());
    sc.update(0.01F);
  }
  CHECK_EQ(scene::Component::liveCount(), before);
}
