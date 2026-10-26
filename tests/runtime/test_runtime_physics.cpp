// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>
#include <stdexcept>

#include "mingol/runtime/runtime.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::runtime;
using namespace mingol::scene;
using mingol::test::fail;

namespace {

std::unique_ptr<Runtime> makeRuntime(RuntimeDesc d = {}) {
  auto r = Runtime::create(d);
  if (!r.ok()) {
    fail(__FILE__, __LINE__, "Runtime::create: " + r.error().describe());
    return nullptr;
  }
  return std::move(r).value();
}

GameObject* makeGround(Runtime& rt) {
  GameObject* g = rt.scene().createObject("ground");
  g->transform().setLocalPosition({0, -0.5F, 0});
  Collider::addBox(*g, {20, 0.5F, 20});
  auto* rb = g->addComponent<Rigidbody>();
  rb->type = physics::BodyType::Static;
  return g;
}

GameObject* makeBox(Runtime& rt, Vec3 pos, const char* name = "box", GameObject* parent = nullptr) {
  GameObject* b = rt.scene().createObject(name, parent);
  b->transform().setLocalPosition(pos);
  Collider::addBox(*b, {0.5F, 0.5F, 0.5F});
  b->addComponent<Rigidbody>();
  return b;
}

void run(Runtime& rt, int frames, float dt = 1.0F / 60.0F) {
  for (int i = 0; i < frames; ++i) rt.tick(dt);
}

struct Recorder final : Component, ContactListener {
  const char* typeName() const override { return "Recorder"; }
  int enters = 0, exits = 0, triggerEnters = 0, triggerExits = 0;
  GameObject* lastOther = nullptr;
  Vec3 lastNormal{0, 0, 0};
  bool throwOnEnter = false;
  void onCollisionEnter(GameObject& o, const physics::ContactEvent& e) override {
    ++enters;
    lastOther = &o;
    lastNormal = e.normal;
    if (throwOnEnter) throw std::runtime_error("boom");
  }
  void onCollisionExit(GameObject&) override { ++exits; }
  void onTriggerEnter(GameObject& o) override { ++triggerEnters; lastOther = &o; }
  void onTriggerExit(GameObject&) override { ++triggerExits; }
};

}  // namespace

TEST(RuntimePhysics, DynamicBodyFallsAndTransformFollows) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  makeGround(*rt);
  GameObject* box = makeBox(*rt, {0, 3, 0});
  run(*rt, 1);
  const float y1 = box->transform().worldPosition().y;
  CHECK(y1 < 3.0F);
  run(*rt, 180);
  // Resting on the ground: centre at 0.5 above the ground top (y = 0).
  CHECK_NEAR(box->transform().worldPosition().y, 0.5F, 0.02F);
  CHECK_NEAR(box->transform().worldPosition().x, 0.0F, 1e-3F);
  CHECK_EQ(rt->physics().bodyCount(), std::size_t{2});
}

TEST(RuntimePhysics, ChildBodyReportsWorldAndLocalCorrectly) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  makeGround(*rt);
  GameObject* parent = rt->scene().createObject("parent");
  parent->transform().setLocalPosition({5, 0, 0});
  GameObject* box = makeBox(*rt, {0, 2, 0}, "child", parent);
  run(*rt, 200);
  CHECK_NEAR(box->transform().worldPosition().x, 5.0F, 1e-3F);
  CHECK_NEAR(box->transform().worldPosition().y, 0.5F, 0.02F);
  CHECK_NEAR(box->transform().localPosition().x, 0.0F, 1e-3F);  // local is relative to the moved parent
}

TEST(RuntimePhysics, EditingTheTransformTeleportsTheBody) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  RuntimeDesc d;
  makeGround(*rt);
  GameObject* box = makeBox(*rt, {0, 0.5F, 0});
  run(*rt, 30);
  box->transform().setLocalPosition({7, 4, -2});
  run(*rt, 1);
  auto s = box->getComponent<Rigidbody>()->state();
  REQUIRE(s.has_value());
  CHECK_NEAR(s->position.x, 7.0F, 0.01F);
  CHECK_NEAR(s->position.z, -2.0F, 0.01F);
  CHECK(s->position.y > 3.5F);
}

TEST(RuntimePhysics, ScaleIsBakedIntoTheShape) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  makeGround(*rt);
  GameObject* box = makeBox(*rt, {0, 4, 0});
  box->transform().setLocalScale({2, 2, 2});  // half extent 0.5 -> 1.0
  run(*rt, 240);
  CHECK_NEAR(box->transform().worldPosition().y, 1.0F, 0.03F);
}

TEST(RuntimePhysics, KinematicBodyFollowsTransformAndPushesNothingBack) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  GameObject* k = rt->scene().createObject("k");
  Collider::addBox(*k, {0.5F, 0.5F, 0.5F});
  auto* rb = k->addComponent<Rigidbody>();
  rb->type = physics::BodyType::Kinematic;
  GameObject* dyn = makeBox(*rt, {0, 1.2F, 0}, "dyn");
  run(*rt, 120);
  CHECK(dyn->transform().worldPosition().y > 0.0F);  // is carried by the kinematic body, not falling through
  k->transform().setLocalPosition({3, 0, 0});
  run(*rt, 1);
  CHECK_NEAR(k->transform().worldPosition().x, 3.0F, 1e-4F);
  CHECK_NEAR(rb->state()->position.x, 3.0F, 0.01F);
}

TEST(RuntimePhysics, CollisionCallbacksReachBothObjectsWithTheOther) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  GameObject* ground = makeGround(*rt);
  auto* gr = ground->addComponent<Recorder>();
  GameObject* box = makeBox(*rt, {0, 2, 0});
  auto* br = box->addComponent<Recorder>();
  run(*rt, 120);
  CHECK_EQ(br->enters, 1);
  CHECK_EQ(gr->enters, 1);
  CHECK(br->lastOther == ground);
  CHECK(gr->lastOther == box);
  // Both are told the same pair normal; it points between the two bodies along y.
  CHECK(std::fabs(br->lastNormal.y) > 0.9F);
  box->transform().setLocalPosition({0, 10, 0});
  run(*rt, 2);
  CHECK_EQ(br->exits, 1);
}

TEST(RuntimePhysics, TriggerEnterExit) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  GameObject* zone = rt->scene().createObject("zone");
  zone->transform().setLocalPosition({0, 0, 0});
  Collider::addBox(*zone, {2, 2, 2});
  auto* zrb = zone->addComponent<Rigidbody>();
  zrb->type = physics::BodyType::Static;
  zrb->isTrigger = true;
  auto* zr = zone->addComponent<Recorder>();

  GameObject* ball = rt->scene().createObject("ball");
  ball->transform().setLocalPosition({0, 10, 0});
  Collider::addSphere(*ball, 0.25F);
  auto* brb = ball->addComponent<Rigidbody>();
  brb->mass = 1;
  auto* br = ball->addComponent<Recorder>();
  run(*rt, 120);  // falls through the zone
  CHECK_EQ(zr->triggerEnters, 1);
  CHECK_EQ(zr->triggerExits, 1);
  CHECK_EQ(br->triggerEnters, 1);
  CHECK_EQ(br->triggerExits, 1);
  CHECK_EQ(zr->enters, 0);  // triggers never produce collisions
  CHECK(ball->transform().worldPosition().y < -2.0F);  // not blocked
}

TEST(RuntimePhysics, RaycastAndOverlapReturnGameObjects) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  GameObject* ground = makeGround(*rt);
  GameObject* box = makeBox(*rt, {4, 0.5F, 0});
  run(*rt, 1);
  auto hit = rt->physics().raycast(rt->scene(), {4, 5, 0}, {0, -1, 0}, 20.0F);
  REQUIRE(hit.has_value());
  CHECK(hit->object == box);
  CHECK_NEAR(hit->distance, 4.0F, 0.02F);
  auto hit2 = rt->physics().raycast(rt->scene(), {-4, 5, 0}, {0, -1, 0}, 20.0F);
  REQUIRE(hit2.has_value());
  CHECK(hit2->object == ground);
  CHECK(!rt->physics().raycast(rt->scene(), {-4, 5, 0}, {0, 1, 0}, 20.0F).has_value());
  auto near = rt->physics().overlapSphere(rt->scene(), {4, 0.5F, 0}, 0.4F);
  CHECK_EQ(near.size(), std::size_t{1});
  if (!near.empty()) CHECK(near[0] == box);
}

TEST(RuntimePhysics, ThrowingCallbackIsContainedAndDisablesOnlyThatComponent) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  makeGround(*rt);
  GameObject* box = makeBox(*rt, {0, 2, 0});
  auto* bad = box->addComponent<Recorder>();
  bad->throwOnEnter = true;
  auto* good = box->addComponent<Recorder>();
  run(*rt, 120);
  CHECK_EQ(rt->physics().callbackFailures(), std::uint64_t{1});
  CHECK(!bad->enabled());
  CHECK(good->enabled());
  CHECK_EQ(good->enters, 1);  // the second listener still got the event
  CHECK(box->getComponent<Rigidbody>()->hasBody());  // the simulation is unaffected
}

TEST(RuntimePhysics, DisableAndDestroyRemoveTheBody) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  GameObject* a = makeBox(*rt, {0, 5, 0}, "a");
  GameObject* b = makeBox(*rt, {3, 5, 0}, "b");
  rt->tick(0.0F);  // bodies are created when the components start
  CHECK_EQ(rt->physics().bodyCount(), std::size_t{2});
  a->getComponent<Rigidbody>()->setEnabled(false);
  CHECK_EQ(rt->physics().bodyCount(), std::size_t{1});
  CHECK(!a->getComponent<Rigidbody>()->hasBody());
  CHECK(!a->getComponent<Rigidbody>()->setVelocity({1, 0, 0}).ok());
  a->getComponent<Rigidbody>()->setEnabled(true);  // rebuilds the body
  CHECK_EQ(rt->physics().bodyCount(), std::size_t{2});
  CHECK(a->getComponent<Rigidbody>()->setVelocity({1, 0, 0}).ok());
  a->getComponent<Rigidbody>()->setEnabled(false);
  CHECK_EQ(rt->physics().bodyCount(), std::size_t{1});
  rt->scene().destroy(b);
  run(*rt, 2);
  CHECK_EQ(rt->physics().bodyCount(), std::size_t{0});
  run(*rt, 5);  // stepping an empty world is fine
}

TEST(RuntimePhysics, ImpulseAndVelocityApplyThroughTheComponent) {
  auto rt = makeRuntime();
  REQUIRE(rt != nullptr);
  CHECK(rt->physics().world().setGravity({0, 0, 0}).ok());
  GameObject* box = makeBox(*rt, {0, 0, 0});
  auto* rb = box->getComponent<Rigidbody>();
  rt->tick(0.0F);
  CHECK(rb->addImpulse({2, 0, 0}).ok());  // mass 1 -> v = 2 m/s
  run(*rt, 60);
  CHECK_NEAR(box->transform().worldPosition().x, 2.0F, 0.05F);
  CHECK(rb->setVelocity({0, 0, 0}).ok());
  run(*rt, 30);
  CHECK_NEAR(box->transform().worldPosition().x, 2.0F, 0.05F);
}

TEST(RuntimePhysics, NoPhysicsServiceFailsGracefully) {
  Scene scene;  // no PhysicsSystem registered
  GameObject* go = scene.createObject("x");
  Collider::addBox(*go, {1, 1, 1});
  auto* rb = go->addComponent<Rigidbody>();
  CHECK(!rb->hasBody());
  CHECK(!rb->setVelocity({1, 0, 0}).ok());
}

TEST(RuntimePhysics, ScenesCreatedAndDestroyedRepeatedlyLeaveNothingBehind) {
  for (int round = 0; round < 20; ++round) {
    auto rt = makeRuntime();
    REQUIRE(rt != nullptr);
    makeGround(*rt);
    for (int i = 0; i < 10; ++i) makeBox(*rt, {static_cast<float>(i), 2.0F + static_cast<float>(i), 0});
    run(*rt, 10);
    CHECK_EQ(rt->physics().bodyCount(), std::size_t{11});
    rt->scene().clear();
    CHECK_EQ(rt->physics().bodyCount(), std::size_t{0});
  }
}
