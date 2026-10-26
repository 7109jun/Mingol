// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>
#include <stdexcept>
#include <string>

#include "mingol/scene/components.hpp"
#include "mingol/scene/scene.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::scene;

namespace {

bool near3(Vec3 a, Vec3 b, float eps = 1e-4F) {
  return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

struct Log {
  std::string s;
  void add(const std::string& e) { s += (s.empty() ? "" : " ") + e; }
};

// Records every callback as "<tag>:<callback>".
class Probe : public Component {
 public:
  Probe(Log& log, std::string tag) : log_(log), tag_(std::move(tag)) {}
  const char* typeName() const override { return "Probe"; }

 protected:
  void onAwake() override { log_.add(tag_ + ":awake"); }
  void onEnable() override { log_.add(tag_ + ":enable"); }
  void onStart() override { log_.add(tag_ + ":start"); }
  void onFixedUpdate(float) override { log_.add(tag_ + ":fixed"); }
  void onUpdate(float) override { log_.add(tag_ + ":update"); }
  void onLateUpdate(float) override { log_.add(tag_ + ":late"); }
  void onDisable() override { log_.add(tag_ + ":disable"); }
  void onDestroy() override { log_.add(tag_ + ":destroy"); }

 private:
  Log& log_;
  std::string tag_;
};

class Thrower : public Component {
 public:
  const char* typeName() const override { return "Thrower"; }
  int updates = 0;
  int disables = 0;

 protected:
  void onUpdate(float) override {
    ++updates;
    throw std::runtime_error("boom");
  }
  void onDisable() override { ++disables; }
};

class Spawner : public Component {
 public:
  const char* typeName() const override { return "Spawner"; }
  int spawned = 0;

 protected:
  void onUpdate(float) override {
    GameObject* child = scene().createObject("spawned" + std::to_string(spawned++), &gameObject());
    child->addComponent<Probe>(log, "s");
  }

 public:
  Log log;
};

class SelfDestroyer : public Component {
 public:
  const char* typeName() const override { return "SelfDestroyer"; }
  int updatesAfterDestroy = 0;
  bool destroyed = false;

 protected:
  void onUpdate(float) override {
    if (destroyed) ++updatesAfterDestroy;
    destroyed = true;
    scene().destroy(&gameObject());
  }
};

}  // namespace

// ---- Transform ------------------------------------------------------------------------------

TEST(Scene, transform_hierarchy_math) {
  Scene s;
  GameObject* a = s.createObject("a");
  GameObject* b = s.createObject("b", a);
  a->transform().setLocalPosition({1, 2, 3});
  a->transform().setLocalRotation(Quat::fromAxisAngle({0, 1, 0}, 1.5707963F));  // +90 deg about Y
  a->transform().setLocalScale({2, 2, 2});
  b->transform().setLocalPosition({1, 0, 0});
  // b's local +X maps to a's rotated, scaled +X: Y-rotation of +90 deg sends +X to -Z.
  CHECK(near3(b->transform().worldPosition(), {1, 2, 3 - 2}));
  CHECK(near3(b->transform().worldScale(), {2, 2, 2}));
  CHECK(near3(transformPoint(b->transform().worldMatrix(), {0, 0, 0}), b->transform().worldPosition()));
}

TEST(Scene, transform_dirty_propagation) {
  Scene s;
  GameObject* a = s.createObject("a");
  GameObject* b = s.createObject("b", a);
  GameObject* c = s.createObject("c", b);
  c->transform().setLocalPosition({0, 0, 1});
  CHECK(near3(c->transform().worldPosition(), {0, 0, 1}));  // caches
  a->transform().setLocalPosition({5, 0, 0});              // must invalidate b and c
  CHECK(near3(c->transform().worldPosition(), {5, 0, 1}));
  b->transform().setLocalPosition({0, 7, 0});
  CHECK(near3(c->transform().worldPosition(), {5, 7, 1}));
  // Reading the grandchild first must also refresh the stale ancestors consistently.
  a->transform().setLocalPosition({0, 0, 0});
  CHECK(near3(c->transform().worldPosition(), {0, 7, 1}));
  CHECK(near3(b->transform().worldPosition(), {0, 7, 0}));
}

TEST(Scene, transform_world_setters_roundtrip) {
  Scene s;
  GameObject* a = s.createObject("a");
  GameObject* b = s.createObject("b", a);
  a->transform().setLocalPosition({3, 0, 0});
  a->transform().setLocalRotation(Quat::fromAxisAngle({0, 0, 1}, 0.7F));
  b->transform().setWorldPosition({10, 20, 30});
  CHECK(near3(b->transform().worldPosition(), {10, 20, 30}, 1e-3F));
  const Quat target = Quat::fromAxisAngle(normalize(Vec3{1, 1, 0}), 0.9F);
  b->transform().setWorldRotation(target);
  const Quat got = b->transform().worldRotation();
  const float d = std::fabs(got.x * target.x + got.y * target.y + got.z * target.z + got.w * target.w);
  CHECK(d > 0.99999F);
}

TEST(Scene, transform_lookat) {
  Scene s;
  GameObject* o = s.createObject("o");
  o->transform().setLocalPosition({0, 0, 5});
  o->transform().lookAt({0, 0, 0});
  CHECK(near3(o->transform().forward(), {0, 0, -1}));
  o->transform().lookAt({5, 0, 5});
  CHECK(near3(o->transform().forward(), {1, 0, 0}));
  CHECK(near3(o->transform().up(), {0, 1, 0}));
  // Target at the object's own position: nothing to look at, rotation unchanged.
  const Vec3 fwd = o->transform().forward();
  o->transform().lookAt(o->transform().worldPosition());
  CHECK(near3(o->transform().forward(), fwd));
  // Up hint parallel to the view direction still yields a valid orthonormal orientation.
  for (const Vec3 dir : {Vec3{0, 1, 0}, Vec3{0, -1, 0}}) {
    o->transform().lookAt(o->transform().worldPosition() + dir * 100.0F, {0, 1, 0});
    CHECK(near3(o->transform().forward(), dir));
    CHECK_NEAR(dot(o->transform().right(), o->transform().up()), 0.0, 1e-4);
    CHECK_NEAR(length(o->transform().right()), 1.0, 1e-4);
  }

}

TEST(Scene, decompose_roundtrip) {
  const Vec3 t{1, -2, 3};
  const Quat r = Quat::fromAxisAngle(normalize(Vec3{1, 2, 3}), 2.1F);
  const Vec3 sc{2, 0.5F, 3};
  Vec3 t2, s2;
  Quat r2;
  decompose(Mat4::trs(t, r, sc), t2, r2, s2);
  CHECK(near3(t2, t));
  CHECK(near3(s2, sc));
  CHECK(std::fabs(r2.x * r.x + r2.y * r.y + r2.z * r.z + r2.w * r.w) > 0.99999F);
  // All four branches of the matrix->quaternion conversion.
  for (const Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
    const Quat q = Quat::fromAxisAngle(axis, 3.0F);  // ~172 deg: trace < 0
    const Quat back = quatFromMatrix(Mat4::trs({0, 0, 0}, q, {1, 1, 1}));
    CHECK(std::fabs(back.x * q.x + back.y * q.y + back.z * q.z + back.w * q.w) > 0.9999F);
  }
}

// ---- Hierarchy ------------------------------------------------------------------------------

TEST(Scene, reparent_keep_world) {
  Scene s;
  GameObject* p1 = s.createObject("p1");
  GameObject* p2 = s.createObject("p2");
  GameObject* c = s.createObject("c", p1);
  p1->transform().setLocalPosition({10, 0, 0});
  p2->transform().setLocalPosition({0, 10, 0});
  c->transform().setLocalPosition({1, 1, 1});
  const Vec3 before = c->transform().worldPosition();
  REQUIRE(c->setParent(p2, true).ok());
  CHECK(near3(c->transform().worldPosition(), before, 1e-4F));
  CHECK(near3(c->transform().localPosition(), {11, -9, 1}, 1e-4F));
  REQUIRE(c->setParent(p1, false).ok());
  CHECK(near3(c->transform().worldPosition(), {21, -9, 1}, 1e-4F));  // local kept, new parent applies
  CHECK_EQ(p2->children().size(), std::size_t{0});
  CHECK_EQ(p1->children().size(), std::size_t{1});
  REQUIRE(c->setParent(nullptr, true).ok());
  CHECK(c->parent() == nullptr);
  CHECK_EQ(s.rootObjects().size(), std::size_t{3});
}

TEST(Scene, reparent_rejects_cycles) {
  Scene s;
  GameObject* a = s.createObject("a");
  GameObject* b = s.createObject("b", a);
  GameObject* c = s.createObject("c", b);
  CHECK(!a->setParent(c).ok());
  CHECK(!a->setParent(a).ok());
  CHECK(a->parent() == nullptr);
  Scene other;
  GameObject* x = other.createObject("x");
  CHECK(!a->setParent(x).ok());
}

TEST(Scene, find_and_roots) {
  Scene s;
  GameObject* a = s.createObject("alpha");
  s.createObject("beta", a);
  CHECK(s.find("beta") != nullptr);
  CHECK(s.find("nope") == nullptr);
  CHECK_EQ(s.objectCount(), std::size_t{2});
  CHECK_EQ(s.rootObjects().size(), std::size_t{1});
}

TEST(Scene, handles_go_stale_and_slots_are_reused) {
  Scene s;
  GameObject* a = s.createObject("a");
  const GameObjectId id = a->id();
  CHECK(s.get(id) == a);
  s.destroy(a);
  CHECK(s.get(id) == nullptr);
  GameObject* b = s.createObject("b");  // reuses the slot with a new generation
  CHECK(b->id().index == id.index);
  CHECK(b->id().generation != id.generation);
  CHECK(s.get(id) == nullptr);
  CHECK(s.get(b->id()) == b);
  CHECK(s.get(GameObjectId{}) == nullptr);
  CHECK(s.get(GameObjectId{9999, 1}) == nullptr);
}

TEST(Scene, destroy_takes_descendants) {
  Scene s;
  GameObject* a = s.createObject("a");
  GameObject* b = s.createObject("b", a);
  GameObject* c = s.createObject("c", b);
  const GameObjectId cid = c->id();
  s.createObject("other");
  s.destroy(a);
  CHECK_EQ(s.objectCount(), std::size_t{1});
  CHECK(s.get(cid) == nullptr);
  s.destroy(nullptr);  // harmless
}

TEST(Scene, deep_hierarchy_no_stack_overflow) {
  constexpr int kDepth = 10000;
  Scene s;
  GameObject* root = s.createObject("root");
  GameObject* cur = root;
  for (int i = 0; i < kDepth; ++i) {
    cur = s.createObject("n", cur);
    cur->transform().setLocalPosition({0, 1, 0});
  }
  CHECK_NEAR(cur->transform().worldPosition().y, kDepth, 0.5);
  root->transform().setLocalPosition({1, 0, 0});  // iterative dirty propagation over the chain
  CHECK_NEAR(cur->transform().worldPosition().x, 1.0, 1e-3);
  cur->setActive(false);
  root->setActive(false);
  root->setActive(true);
  s.destroy(root);
  CHECK_EQ(s.objectCount(), std::size_t{0});
}

// ---- Lifecycle ------------------------------------------------------------------------------

TEST(Scene, lifecycle_order) {
  Log log;
  {
    Scene s;
    GameObject* o = s.createObject("o");
    o->addComponent<Probe>(log, "p");
    CHECK_EQ(log.s, std::string("p:awake p:enable"));
    s.fixedUpdate(0.02F);
    s.update(0.016F);
    s.lateUpdate(0.016F);
    s.update(0.016F);
    CHECK_EQ(log.s, std::string("p:awake p:enable p:start p:fixed p:update p:late p:update"));
    log.s.clear();
    s.destroy(o);
    CHECK_EQ(log.s, std::string("p:disable p:destroy"));
  }
}

TEST(Scene, start_called_once_even_after_reenable) {
  Log log;
  Scene s;
  GameObject* o = s.createObject("o");
  Probe* p = o->addComponent<Probe>(log, "p");
  s.update(0.1F);
  p->setEnabled(false);
  s.update(0.1F);
  p->setEnabled(true);
  s.update(0.1F);
  CHECK_EQ(log.s, std::string("p:awake p:enable p:start p:update p:disable p:enable p:update"));
}

TEST(Scene, inactive_objects_skip_everything) {
  Log log;
  Scene s;
  GameObject* parent = s.createObject("parent");
  GameObject* child = s.createObject("child", parent);
  parent->setActive(false);
  child->addComponent<Probe>(log, "c");
  s.update(0.1F);
  CHECK_EQ(log.s, std::string(""));  // inactive in hierarchy: not even awake
  CHECK(!child->activeInHierarchy());
  parent->setActive(true);
  CHECK_EQ(log.s, std::string("c:awake c:enable"));
  s.update(0.1F);
  parent->setActive(false);
  CHECK_EQ(log.s, std::string("c:awake c:enable c:start c:update c:disable"));
  s.update(0.1F);
  CHECK_EQ(log.s, std::string("c:awake c:enable c:start c:update c:disable"));
}

TEST(Scene, disabled_component_skipped) {
  Log log;
  Scene s;
  GameObject* o = s.createObject("o");
  Probe* p = o->addComponent<Probe>(log, "p");
  p->setEnabled(false);
  log.s.clear();
  s.update(0.1F);
  CHECK_EQ(log.s, std::string(""));
  CHECK(!p->activeAndEnabled());
}

TEST(Scene, component_add_get_remove) {
  Log log;
  Scene s;
  GameObject* o = s.createObject("o");
  CHECK(o->getComponent<Probe>() == nullptr);
  Probe* p = o->addComponent<Probe>(log, "a");
  o->addComponent<Probe>(log, "b");
  CHECK(o->getComponent<Probe>() == p);
  CHECK_EQ(o->getComponents<Probe>().size(), std::size_t{2});
  CHECK_EQ(o->componentCount(), std::size_t{2});
  log.s.clear();
  o->removeComponent(p);
  CHECK_EQ(log.s, std::string("a:disable a:destroy"));
  CHECK_EQ(o->componentCount(), std::size_t{1});
  CHECK_EQ(o->getComponents<Probe>().size(), std::size_t{1});
  o->removeComponent(nullptr);  // harmless
}

TEST(Scene, callback_exception_is_contained) {
  Log log;  // declared before the scene: components log while the scene is torn down
  Scene s;
  GameObject* o = s.createObject("o");
  Thrower* t = o->addComponent<Thrower>();
  o->addComponent<Probe>(log, "p");
  log.s.clear();
  s.update(0.1F);  // Thrower throws; Probe must still run
  CHECK_EQ(t->updates, 1);
  CHECK_EQ(t->disables, 1);
  CHECK(!t->enabled());
  CHECK_EQ(s.callbackFailures(), std::uint64_t{1});
  CHECK_EQ(log.s, std::string("p:start p:update"));
  s.update(0.1F);  // the faulty component stays disabled
  CHECK_EQ(t->updates, 1);
}

TEST(Scene, create_during_update) {
  Scene s;
  GameObject* o = s.createObject("o");
  Spawner* sp = o->addComponent<Spawner>();
  s.update(0.1F);
  CHECK_EQ(sp->spawned, 1);
  CHECK_EQ(s.objectCount(), std::size_t{2});
  CHECK_EQ(sp->log.s, std::string("s:awake s:enable"));  // new objects first update next frame
  s.update(0.1F);
  // Frame 2: the spawner (earlier in creation order) adds a second child before the first one starts.
  CHECK_EQ(sp->log.s, std::string("s:awake s:enable s:awake s:enable s:start s:update"));
}

TEST(Scene, destroy_during_update_is_deferred_and_final) {
  Log log;
  Scene s;
  GameObject* a = s.createObject("a");
  SelfDestroyer* d = a->addComponent<SelfDestroyer>();
  a->addComponent<Probe>(log, "p");  // later component of the same object still sees this update
  s.update(0.1F);
  CHECK_EQ(s.objectCount(), std::size_t{0});
  (void)d;
  s.update(0.1F);  // nothing left to run
  CHECK_EQ(s.objectCount(), std::size_t{0});
}

TEST(Scene, clear_runs_callbacks_and_empties) {
  Log log;
  Scene s;
  GameObject* a = s.createObject("a");
  GameObject* b = s.createObject("b", a);
  a->addComponent<Probe>(log, "a");
  b->addComponent<Probe>(log, "b");
  log.s.clear();
  s.clear();
  CHECK_EQ(s.objectCount(), std::size_t{0});
  CHECK_EQ(log.s, std::string("b:disable b:destroy a:disable a:destroy"));  // children first
  s.createObject("again");  // usable afterwards
  CHECK_EQ(s.objectCount(), std::size_t{1});
}

TEST(Scene, destructor_runs_destroy_callbacks) {
  Log log;
  {
    Scene s;
    s.createObject("a")->addComponent<Probe>(log, "a");
    log.s.clear();
  }
  CHECK_EQ(log.s, std::string("a:disable a:destroy"));
}

TEST(Scene, load_unload_does_not_leak) {
  const auto objects0 = GameObject::liveCount();
  const auto comps0 = Component::liveCount();
  Log log;
  for (int round = 0; round < 50; ++round) {
    Scene s;
    GameObject* root = s.createObject("root");
    for (int i = 0; i < 100; ++i) {
      GameObject* c = s.createObject("c", root);
      c->addComponent<Probe>(log, "x");
      c->addComponent<Probe>(log, "y");
    }
    s.update(0.01F);
    if (round % 2 == 0) {
      s.destroy(root);
      s.update(0.01F);
    }
    log.s.clear();
  }
  CHECK_EQ(GameObject::liveCount(), objects0);
  CHECK_EQ(Component::liveCount(), comps0);
}

// ---- Animator & buildRenderScene (CPU side) -------------------------------------------------

namespace {
std::shared_ptr<const renderer::Skeleton> armSkeleton() {
  std::vector<renderer::Joint> j(2);
  j[0].name = "root";
  j[1].name = "tip";
  j[1].parent = 0;
  j[1].bindLocal.translation = {0, 1, 0};
  auto s = renderer::Skeleton::create(std::move(j));
  CHECK(s.ok());
  if (!s.ok()) return nullptr;
  return std::make_shared<renderer::Skeleton>(std::move(s).value());
}

std::shared_ptr<const renderer::AnimationClip> moveClip(const char* name, float x) {
  renderer::Channel ch;
  ch.joint = 0;
  ch.translation.times = {0.0F, 1.0F};
  ch.translation.values = {{0, 0, 0}, {x, 0, 0}};
  auto c = renderer::AnimationClip::create(name, 1.0F, {ch});
  CHECK(c.ok());
  if (!c.ok()) return nullptr;
  return std::make_shared<renderer::AnimationClip>(std::move(c).value());
}

Vec3 rootOffset(const Animator& a) {
  auto m = a.skinMatrices();
  CHECK(m.ok());
  if (!m.ok()) return {};
  return {m.value()[0].m[12], m.value()[0].m[13], m.value()[0].m[14]};
}
}  // namespace

TEST(Scene, animator_play_loop_and_clamp) {
  Scene s;
  Animator* a = s.createObject("o")->addComponent<Animator>();
  CHECK(!a->play("x").ok());  // no skeleton, unknown clip
  REQUIRE(a->setSkeleton(armSkeleton()).ok());
  REQUIRE(a->addClip(moveClip("move", 4.0F)).ok());
  CHECK(!a->addClip(moveClip("move", 4.0F)).ok());  // duplicate
  CHECK(near3(rootOffset(*a), {0, 0, 0}));            // bind pose while idle
  REQUIRE(a->play("move", true).ok());
  a->advance(0.25F);
  CHECK(near3(rootOffset(*a), {1, 0, 0}));
  a->advance(1.0F);  // wraps to 0.25 again
  CHECK_NEAR(a->currentTime(), 0.25, 1e-4);
  REQUIRE(a->play("move", false).ok());
  a->advance(5.0F);
  CHECK(a->finished());
  CHECK(near3(rootOffset(*a), {4, 0, 0}));
  a->speed = 0.0F;
  REQUIRE(a->play("move", false).ok());
  a->advance(1.0F);
  CHECK_NEAR(a->currentTime(), 0.0, 1e-6);
}

TEST(Scene, animator_crossfade) {
  Scene s;
  Animator* a = s.createObject("o")->addComponent<Animator>();
  REQUIRE(a->setSkeleton(armSkeleton()).ok());
  REQUIRE(a->addClip(moveClip("a", 2.0F)).ok());
  REQUIRE(a->addClip(moveClip("b", -2.0F)).ok());
  REQUIRE(a->play("a", false).ok());
  a->advance(1.0F);  // a at its end: x = 2
  CHECK(near3(rootOffset(*a), {2, 0, 0}));
  REQUIRE(a->play("b", false, 1.0F).ok());
  CHECK(a->fading());
  // Immediately after the switch: all of the previous pose (x=2) and none of b.
  CHECK(near3(rootOffset(*a), {2, 0, 0}));
  a->advance(0.5F);  // b at 0.5 -> -1; a held at its end -> 2; blend 0.5 -> 0.5
  CHECK(near3(rootOffset(*a), {0.5F, 0, 0}));
  a->advance(0.5F);  // fade complete: only b (at its end, -2)
  CHECK(!a->fading());
  CHECK(near3(rootOffset(*a), {-2, 0, 0}));
}

TEST(Scene, build_render_scene_requires_camera) {
  Scene s;
  s.createObject("o");
  CHECK(!buildRenderScene(s, 1.0F).ok());
  s.createObject("cam")->addComponent<CameraComponent>();
  CHECK(buildRenderScene(s, 1.0F).ok());
  CHECK(!buildRenderScene(s, 0.0F).ok());
  CHECK(!buildRenderScene(s, std::nanf("")).ok());
}

TEST(Scene, build_render_scene_camera_and_lights) {
  Scene s;
  s.environment.ambient = {0.1F, 0.2F, 0.3F};
  GameObject* low = s.createObject("low");
  low->addComponent<CameraComponent>()->priority = 0;
  GameObject* high = s.createObject("high");
  high->addComponent<CameraComponent>()->priority = 5;
  high->transform().setLocalPosition({0, 0, 10});
  high->transform().setLocalScale({3, 3, 3});  // scale must not distort the view
  // A disabled higher-priority camera is ignored.
  GameObject* off = s.createObject("off");
  off->addComponent<CameraComponent>()->priority = 9;
  off->getComponent<CameraComponent>()->setEnabled(false);

  GameObject* sun = s.createObject("sun");
  LightComponent* l = sun->addComponent<LightComponent>();
  l->castShadow = true;
  sun->transform().lookAt(sun->transform().worldPosition() + Vec3{0, -1, 0});
  s.createObject("sun2")->addComponent<LightComponent>();  // second directional is dropped
  for (int i = 0; i < 12; ++i) {
    GameObject* pl = s.createObject("pl");
    pl->transform().setLocalPosition({static_cast<float>(i), 0, 0});
    LightComponent* lc = pl->addComponent<LightComponent>();
    lc->type = renderer::LightType::Point;
  }
  auto r = buildRenderScene(s, 2.0F);
  REQUIRE(r.ok());
  const renderer::RenderScene& rs = r.value();
  CHECK(near3(rs.camera.position, {0, 0, 10}));
  CHECK(near3(transformPoint(rs.camera.view, {0, 0, 10}), {0, 0, 0}));  // view maps the eye to the origin
  CHECK(near3(transformPoint(rs.camera.view, {0, 0, 7}), {0, 0, -3}));  // unscaled: 3 units ahead
  CHECK(near3(rs.ambient, {0.1F, 0.2F, 0.3F}));
  CHECK_EQ(rs.lights.size(), std::size_t{1 + renderer::kMaxPunctualLights});
  CHECK(rs.lights[0].type == renderer::LightType::Directional);
  CHECK(rs.lights[0].castShadow);
  CHECK(near3(rs.lights[0].direction, {0, -1, 0}));
  // Nearest to the camera at (0,0,10) are x = 0..7.
  float maxX = 0;
  for (std::size_t i = 1; i < rs.lights.size(); ++i) maxX = std::max(maxX, rs.lights[i].position.x);
  CHECK_NEAR(maxX, 7.0, 1e-5);
}

TEST(Scene, build_render_scene_skips_undrawable_and_inactive) {
  Scene s;
  s.createObject("cam")->addComponent<CameraComponent>();
  s.createObject("empty")->addComponent<MeshRenderer>();  // no mesh/material: skipped, not an error
  GameObject* hidden = s.createObject("hidden");
  hidden->addComponent<MeshRenderer>();
  hidden->setActive(false);
  auto r = buildRenderScene(s, 1.0F);
  REQUIRE(r.ok());
  CHECK_EQ(r.value().items.size(), std::size_t{0});
}

TEST(Scene, ServiceRegistry) {
  struct A { int v = 1; };
  struct B { int v = 2; };
  Scene s;
  CHECK(s.service<A>() == nullptr);
  A a;
  B b;
  s.setService(&a);
  s.setService(&b);
  CHECK(s.service<A>() == &a);
  CHECK(s.service<B>() == &b);
  A a2;
  s.setService(&a2);  // replaces
  CHECK(s.service<A>() == &a2);
  s.setService<A>(nullptr);
  CHECK(s.service<A>() == nullptr);
  CHECK(s.service<B>() == &b);  // unrelated entry untouched
}
