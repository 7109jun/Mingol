// SPDX-License-Identifier: GPL-2.0-only
// .minseen round trips.
#include <cmath>
#include <filesystem>

#include "sceneio_util.hpp"

using namespace mingol;
using namespace mingol::test;
using namespace mingol::sceneio;

namespace {
LoadOptions replaceOpts() { LoadOptions o; o.replaceExisting = true; return o; }
const char* kScripts = R"(
enum Mode { Idle, Run = 4 }
class Trace { static string s = ""; }
class Hero extends Script {
  public int hp = 100;
  public float speed = 2.5;
  public string title = "hero";
  public bool alive = true;
  public Mode mode = Mode.Run;
  public int[] inventory;
  public string[] tags;
  public float[] weights;
  public GameObject target;            // object fields are not saved
  static int counter;
  override void onAwake() { Trace.s += "A"; inventory = new int[2]; }
  override void onStart() { Trace.s += "S"; }
  override void onEnable() { Trace.s += "E"; }
}
)";

struct Fixture {
  std::unique_ptr<scripting::ScriptHost> host;
  AssetTable assets;
  ComponentRegistry registry = ComponentRegistry::withBuiltins();
  SceneIoContext ctx;
  std::shared_ptr<const assets::AudioClip> clip = std::make_shared<assets::AudioClip>();
  std::shared_ptr<const physics::MeshShapeData> collMesh;
  std::shared_ptr<const renderer::Skeleton> skel = testSkeleton();
  std::shared_ptr<const renderer::AnimationClip> walk = testClip("walk", 1.0F), run = testClip("run", 3.0F);

  Fixture() {
    auto h = scripting::ScriptHost::create({{"hero.mins", kScripts}});
    REQUIRE(h.ok());
    host = std::move(h).value();
    auto c = std::make_shared<assets::AudioClip>();
    c->sampleRate = 48000; c->channels = 1; c->samples.assign(480, 0.0F);
    clip = c;
    auto m = std::make_shared<physics::MeshShapeData>();
    m->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    m->indices = {0, 1, 2};
    collMesh = m;
    assets.addAudioClip("sfx/ping", clip);
    assets.addCollisionMesh("level/collision", collMesh);
    assets.addSkeleton("rig/hero", skel);
    assets.addAnimationClip("anim/walk", walk);
    assets.addAnimationClip("anim/run", run);
    ctx.assets = &assets;
    ctx.scripts = host.get();
  }
};

// A scene touching every built-in component type. Objects are created inactive where a component would
// otherwise need engine services (physics/audio), so the same scene works without a Runtime.
void buildSample(Fixture& f, scene::Scene& sc) {
  sc.environment.ambient = {0.1F, 0.2F, 0.3F};
  sc.environment.clearColor[0] = 0.25F; sc.environment.clearColor[3] = 1.0F;
  auto* root = sc.createObject("root");
  root->transform().setLocal({1.5F, -2.0F, 0.1F}, Quat::fromAxisAngle({0, 1, 0}, 0.7F), {1, 2, 3});
  auto* cam = sc.createObject("cam", root);
  auto* c = cam->addComponent<scene::CameraComponent>();
  c->fovYRadians = 0.9F; c->nearZ = 0.3F; c->farZ = 500.0F; c->priority = 3;
  auto* sun = sc.createObject("sun");
  auto* l = sun->addComponent<scene::LightComponent>();
  l->type = renderer::LightType::Spot; l->color = {1, 0.5F, 0.25F}; l->intensity = 3.5F; l->range = 17.0F;
  l->innerConeRadians = 0.2F; l->outerConeRadians = 0.5F; l->castShadow = true;

  auto* body = sc.createObject("body", root);
  body->setActive(false);
  auto* rb = body->addComponent<runtime::Rigidbody>();
  rb->type = physics::BodyType::Kinematic; rb->mass = 3.0F; rb->friction = 0.9F; rb->restitution = 0.25F;
  rb->linearDamping = 0.1F; rb->angularDamping = 0.2F; rb->isTrigger = true; rb->allowSleep = false;
  rb->layer = 4; rb->mask = 0xF0F0; rb->lockRotationY = true; rb->lockPositionZ = true;
  runtime::Collider::addBox(*body, {1, 2, 3}, {0, 1, 0});
  runtime::Collider::addSphere(*body, 0.75F);
  runtime::Collider::addCapsule(*body, 0.4F, 1.2F, {0, 0, 1});
  auto* lvl = sc.createObject("level");
  lvl->setActive(false);
  runtime::Collider::addMesh(*lvl, f.collMesh);

  auto* audio = sc.createObject("audio");
  audio->setActive(false);
  auto* as = audio->addComponent<runtime::AudioSource>();
  as->clip = f.clip; as->volume = 0.5F; as->pitch = 1.5F; as->pan = -0.25F; as->loop = true; as->playOnEnable = false;
  as->priority = 2; as->spatial = true; as->minDistance = 2; as->maxDistance = 30; as->rolloff = 0.5F;
  audio->addComponent<runtime::AudioListener>();

  auto* anim = sc.createObject("anim");
  auto* an = anim->addComponent<scene::Animator>();
  REQUIRE(an->setSkeleton(f.skel).ok());
  REQUIRE(an->addClip(f.walk).ok());
  REQUIRE(an->addClip(f.run).ok());
  an->speed = 2.0F;
  REQUIRE(an->play("run", false).ok());
  an->advance(0.25F);

  auto* hero = sc.createObject("hero");
  auto r = f.host->addScript(*hero, "Hero");
  REQUIRE(r.ok());
  REQUIRE(r.value()->setInt("hp", 37).ok());
  REQUIRE(r.value()->setFloat("speed", 0.1).ok());
  REQUIRE(r.value()->setString("title", "Ünï \"quoted\"\n").ok());
  REQUIRE(r.value()->setBool("alive", false).ok());
  hero->getComponent<scripting::ScriptComponent>()->setEnabled(false);
}
}  // namespace

TEST(SceneIo, RoundTripIsStable) {
  Fixture f;
  scene::Scene a;
  buildSample(f, a);
  auto t1 = saveScene(a, f.ctx, f.registry, SaveOptions{.sceneName = "sample", .pretty = true});
  REQUIRE(t1.ok());
  scene::Scene b;
  auto rep = loadScene(b, t1.value(), f.ctx, f.registry);
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().objectsCreated, std::size_t{8});
  CHECK_EQ(rep.value().componentsCreated, std::size_t{11});
  CHECK(rep.value().warnings.empty());
  auto t2 = saveScene(b, f.ctx, f.registry, SaveOptions{.sceneName = "sample", .pretty = true});
  REQUIRE(t2.ok());
  CHECK_MSG(t1.value() == t2.value(), "save(load(save(x))) differs: " + firstDiff(t1.value(), t2.value()));
}

TEST(SceneIo, EveryPropertySurvives) {
  Fixture f;
  scene::Scene a;
  buildSample(f, a);
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  scene::Scene b;
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry).ok());

  CHECK_EQ(b.environment.ambient.y, 0.2F);
  CHECK_EQ(b.environment.clearColor[0], 0.25F);
  auto* root = b.find("root");
  REQUIRE(root != nullptr);
  CHECK_EQ(root->transform().localPosition().z, 0.1F);   // exact float round trip
  CHECK_EQ(root->transform().localScale().z, 3.0F);
  CHECK_NEAR(root->transform().localRotation().y, std::sin(0.35F), 1e-6F);
  auto* cam = b.find("cam");
  REQUIRE(cam != nullptr);
  CHECK(cam->parent() == root);
  auto* cc = cam->getComponent<scene::CameraComponent>();
  REQUIRE(cc != nullptr);
  CHECK_EQ(cc->fovYRadians, 0.9F);
  CHECK_EQ(cc->farZ, 500.0F);
  CHECK_EQ(cc->priority, 3);
  auto* lc = b.find("sun")->getComponent<scene::LightComponent>();
  REQUIRE(lc != nullptr);
  CHECK(lc->type == renderer::LightType::Spot);
  CHECK_EQ(lc->color.y, 0.5F);
  CHECK(lc->castShadow);
  CHECK_EQ(lc->outerConeRadians, 0.5F);

  auto* body = b.find("body");
  REQUIRE(body != nullptr);
  CHECK(!body->activeSelf());
  auto* rb = body->getComponent<runtime::Rigidbody>();
  REQUIRE(rb != nullptr);
  CHECK(rb->type == physics::BodyType::Kinematic);
  CHECK_EQ(rb->mass, 3.0F);
  CHECK_EQ(rb->friction, 0.9F);
  CHECK(rb->isTrigger);
  CHECK(!rb->allowSleep);
  CHECK_EQ(rb->layer, 4u);
  CHECK_EQ(rb->mask, 0xF0F0u);
  CHECK(rb->lockRotationY && !rb->lockRotationX && rb->lockPositionZ && !rb->lockPositionX);
  auto cols = body->getComponents<runtime::Collider>();
  REQUIRE(cols.size() == 3);
  CHECK(cols[0]->shape.type == physics::ShapeType::Box);
  CHECK_EQ(cols[0]->shape.halfExtents.z, 3.0F);
  CHECK_EQ(cols[0]->shape.offset.y, 1.0F);
  CHECK(cols[1]->shape.type == physics::ShapeType::Sphere);
  CHECK_EQ(cols[1]->shape.radius, 0.75F);
  CHECK(cols[2]->shape.type == physics::ShapeType::Capsule);
  CHECK_EQ(cols[2]->shape.height, 1.2F);
  auto* lvl = b.find("level")->getComponent<runtime::Collider>();
  REQUIRE(lvl != nullptr);
  CHECK(lvl->shape.mesh == f.collMesh);   // the very same shared asset

  auto* as = b.find("audio")->getComponent<runtime::AudioSource>();
  REQUIRE(as != nullptr);
  CHECK(as->clip == f.clip);
  CHECK_EQ(as->pan, -0.25F);
  CHECK(as->loop && as->spatial && !as->playOnEnable);
  CHECK_EQ(as->maxDistance, 30.0F);
  CHECK(b.find("audio")->getComponent<runtime::AudioListener>() != nullptr);

  auto* an = b.find("anim")->getComponent<scene::Animator>();
  REQUIRE(an != nullptr);
  CHECK(an->skeleton() == f.skel);
  CHECK_EQ(an->clips().size(), std::size_t{2});
  CHECK_EQ(an->speed, 2.0F);
  CHECK(an->playing());
  CHECK_EQ(an->currentClip(), "run");
  CHECK(!an->looping());
  CHECK_NEAR(an->currentTime(), 0.5F, 1e-6F);   // advance(0.25) at speed 2

  auto* hero = b.find("hero");
  auto* sc = hero->getComponent<scripting::ScriptComponent>();
  REQUIRE(sc != nullptr);
  CHECK(!sc->enabled());
  CHECK_EQ(sc->getInt("hp").value(), std::int64_t{37});
  CHECK_EQ(sc->getFloat("speed").value(), 0.1);
  CHECK_EQ(sc->getString("title").value(), "Ünï \"quoted\"\n");
  CHECK(!sc->getBool("alive").value());
}

TEST(SceneIo, ScriptArraysEnumsAndInitialisersRoundTrip) {
  Fixture f;
  scene::Scene a;
  auto* go = a.createObject("h");
  auto* s = f.host->addScript(*go, "Hero").value();
  // arrays are created by the script (onAwake made `inventory`); fill the rest through the VM
  auto& vm = f.host->vm();
  const auto* api = script::hostApi();
  const auto* cls = f.host->program().findClass("Hero");
  {
    MsValue arr = api->newArray(&vm, MS_ELEM_STRING, 2);
    script::Rooted keep(vm, arr);
    api->arraySet(&vm, keep.get(), 0, vm.newString("a"));
    api->arraySet(&vm, keep.get(), 1, vm.newString("béta"));
    vm.setField(s->instance(), *cls->findField("tags"), keep.get());
    MsValue fl = api->newArray(&vm, MS_ELEM_FLOAT, 1);
    script::Rooted keepF(vm, fl);
    api->arraySet(&vm, keepF.get(), 0, script::makeFloat(0.125));
    vm.setField(s->instance(), *cls->findField("weights"), keepF.get());
  }
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  CHECK(text.value().find("\"mode\": 4") != std::string::npos);
  CHECK(text.value().find("\"target\"") == std::string::npos);   // object fields are not saved
  CHECK(text.value().find("\"gameObject\"") == std::string::npos);
  CHECK(text.value().find("counter") == std::string::npos);      // statics are not saved
  scene::Scene b;
  auto rep = loadScene(b, text.value(), f.ctx, f.registry);
  REQUIRE(rep.ok());
  CHECK(rep.value().warnings.empty());
  auto* s2 = b.find("h")->getComponent<scripting::ScriptComponent>();
  auto j = s2->serializeFields();
  REQUIRE(j.ok());
  auto j1 = s->serializeFields();
  REQUIRE(j1.ok());
  CHECK(j.value() == j1.value());
}

TEST(SceneIo, LoadingRunsNoCallbacksUntilTheEnd) {
  Fixture f;
  scene::Scene a;
  auto* go = a.createObject("h");
  REQUIRE(f.host->addScript(*go, "Hero").ok());
  auto* child = a.createObject("c", go);
  REQUIRE(f.host->addScript(*child, "Hero").ok());
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  const auto* tcls = f.host->program().findClass("Trace");
  const auto* tf = tcls->findField("s");
  // reset trace
  f.host->vm().setStatic(*tf, f.host->vm().newString(""));
  scene::Scene b;
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry).ok());
  // both objects became active at the very end: Awake+Enable each (Start comes with the first update)
  CHECK_EQ(std::string(f.host->vm().stringValue(f.host->vm().getStatic(*tf))), "AEAE");
}

TEST(SceneIo, InactiveObjectsStayInactiveAndParentsGateChildren) {
  Fixture f;
  scene::Scene a;
  auto* p = a.createObject("p");
  auto* c = a.createObject("c", p);
  p->setActive(false);
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  scene::Scene b;
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry).ok());
  CHECK(!b.find("p")->activeSelf());
  CHECK(b.find("c")->activeSelf());
  CHECK(!b.find("c")->activeInHierarchy());
  (void)c;
}

TEST(SceneIo, FloatsRoundTripExactly) {
  Fixture f;
  scene::Scene a;
  const float vals[] = {0.1F, 1e-7F, 123456.78F, -3.4e30F, 1.17549435e-38F, 0.3333333F, 16777217.0F};
  for (int i = 0; i < 7; ++i) a.createObject("o" + std::to_string(i))->transform().setLocalPosition({vals[i], -vals[i], vals[i] * 0.5F});
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  scene::Scene b;
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry).ok());
  for (int i = 0; i < 7; ++i) {
    const Vec3 p = b.find("o" + std::to_string(i))->transform().localPosition();
    CHECK_EQ(p.x, vals[i]);
    CHECK_EQ(p.y, -vals[i]);
    CHECK_EQ(p.z, vals[i] * 0.5F);
  }
}

TEST(SceneIo, DestroyedObjectsAreNotSavedAndOrderIsParentFirst) {
  Fixture f;
  scene::Scene a;
  auto* r1 = a.createObject("r1");
  auto* kid = a.createObject("kid", r1);
  auto* r2 = a.createObject("r2");
  a.createObject("grand", kid);
  a.destroy(r2);   // pending, not flushed yet
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  CHECK(text.value().find("\"r2\"") == std::string::npos);
  scene::Scene b;
  auto rep = loadScene(b, text.value(), f.ctx, f.registry);
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().objectsCreated, std::size_t{3});
  CHECK(b.find("grand")->parent() == b.find("kid"));
  a.flushDestroyed();
}

TEST(SceneIo, ReplaceExistingAndAppend) {
  Fixture f;
  scene::Scene a;
  a.createObject("x");
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  scene::Scene b;
  b.createObject("old");
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry).ok());   // append
  CHECK(b.find("old") != nullptr);
  CHECK(b.find("x") != nullptr);
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry, replaceOpts()).ok());
  CHECK(b.find("old") == nullptr);
  CHECK_EQ(b.objectCount(), std::size_t{1});
}

TEST(SceneIo, FilesAreWrittenAtomicallyAndReadBack) {
  Fixture f;
  scene::Scene a;
  buildSample(f, a);
  const auto dir = std::filesystem::temp_directory_path() / "mingol_sceneio_test";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "level.minseen").string();
  REQUIRE(saveSceneFile(path, a, f.ctx, f.registry, SaveOptions{.sceneName = "level", .pretty = true}).ok());
  scene::Scene b;
  auto rep = loadSceneFile(path, b, f.ctx, f.registry);
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().objectsCreated, std::size_t{8});
  // no temp files left behind
  std::size_t n = 0;
  for (const auto& e : std::filesystem::directory_iterator(dir)) { (void)e; ++n; }
  CHECK_EQ(n, std::size_t{1});
  CHECK(!loadSceneFile((dir / "missing.minseen").string(), b, f.ctx, f.registry).ok());
  std::filesystem::remove_all(dir);
}

TEST(SceneIo, CustomComponentTypesCanBeRegistered) {
  struct Tag final : scene::Component {
    const char* typeName() const override { return "Tag"; }
    std::string label;
  };
  Fixture f;
  CHECK(f.registry.registerType("Tag",
      [](const scene::Component& c, const SceneIoContext&) -> Result<json::Value> {
        json::Object o; o.set("label", static_cast<const Tag&>(c).label); return json::Value(std::move(o)); },
      [](scene::GameObject& go, const json::Value& v, const SceneIoContext&, LoadReport&) -> Result<void> {
        if (!v.isObject() || v.asObject().find("label") == nullptr || !v.asObject().find("label")->isString()) return Error(ErrorCode::Corrupt, "bad Tag");
        go.addComponent<Tag>()->label = v.asObject().find("label")->asString(); return {}; }).ok());
  CHECK(!f.registry.registerType("Tag", nullptr, nullptr).ok());
  scene::Scene a;
  a.createObject("t")->addComponent<Tag>()->label = "hello";
  auto text = saveScene(a, f.ctx, f.registry);
  REQUIRE(text.ok());
  scene::Scene b;
  REQUIRE(loadScene(b, text.value(), f.ctx, f.registry).ok());
  CHECK_EQ(b.find("t")->getComponent<Tag>()->label, "hello");
}

TEST(SceneIo, LargeScene) {
  Fixture f;
  scene::Scene a;
  scene::GameObject* prev = nullptr;
  for (int i = 0; i < 5000; ++i) {
    auto* go = a.createObject("n" + std::to_string(i), (i % 50 == 0) ? nullptr : prev);   // many chains of depth 50
    go->transform().setLocalPosition({static_cast<float>(i), 0, 0});
    go->addComponent<scene::CameraComponent>()->priority = i;
    prev = go;
  }
  auto text = saveScene(a, f.ctx, f.registry, SaveOptions{.sceneName = {}, .pretty = false});
  REQUIRE(text.ok());
  scene::Scene b;
  auto rep = loadScene(b, text.value(), f.ctx, f.registry);
  REQUIRE(rep.ok());
  CHECK_EQ(rep.value().objectsCreated, std::size_t{5000});
  CHECK_EQ(b.find("n4999")->getComponent<scene::CameraComponent>()->priority, 4999);
  CHECK(b.find("n4999")->parent() == b.find("n4998"));
}
