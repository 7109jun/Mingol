// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>
#include <cstring>
#include <set>

#include "mingol/physics/physics.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::physics;

namespace {

std::unique_ptr<World> makeWorld(WorldDesc d = {}) {
  auto w = World::create(d);
  if (!w.ok()) {
    test::fail(__FILE__, __LINE__, "World::create: " + w.error().describe());
    return nullptr;
  }
  return std::move(w).value();
}

ShapeDesc box(Vec3 h) {
  ShapeDesc s;
  s.type = ShapeType::Box;
  s.halfExtents = h;
  return s;
}
ShapeDesc sphere(float r) {
  ShapeDesc s;
  s.type = ShapeType::Sphere;
  s.radius = r;
  return s;
}

BodyId add(World& w, const BodyDesc& d) {
  auto r = w.createBody(d);
  if (!r.ok()) {
    test::fail(__FILE__, __LINE__, "createBody: " + r.error().describe());
    return {};
  }
  return r.value();
}

// Static slab whose top face is at y = 0.
BodyId addFloor(World& w, std::uint32_t layer = 1, std::uint32_t mask = 0xFFFFFFFFu, float friction = 0.5F) {
  BodyDesc d;
  d.type = BodyType::Static;
  d.shapes = {box({50, 0.5F, 50})};
  d.position = {0, -0.5F, 0};
  d.layer = layer;
  d.mask = mask;
  d.friction = friction;
  return add(w, d);
}

BodyId addBall(World& w, Vec3 pos, float radius = 0.5F, float mass = 1.0F) {
  BodyDesc d;
  d.shapes = {sphere(radius)};
  d.mass = mass;
  d.position = pos;
  return add(w, d);
}

void run(World& w, int steps) {
  for (int i = 0; i < steps; ++i) w.stepOnce();
}

BodyState st(World& w, BodyId id) {
  auto s = w.state(id);
  CHECK(s.has_value());
  return s.value_or(BodyState{});
}

bool finiteState(const BodyState& s) {
  for (float f : {s.position.x, s.position.y, s.position.z, s.linearVelocity.x, s.linearVelocity.y, s.linearVelocity.z,
                  s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w}) {
    if (!std::isfinite(f)) return false;
  }
  return true;
}

}  // namespace

TEST(Physics, FreeFallMatchesTheIntegrator) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  const BodyId b = addBall(*w, {0, 100, 0});
  const int n = 60;
  run(*w, n);
  // Semi-implicit Euler: v_k = k g dt and y_n = y0 + g dt^2 n (n+1) / 2.
  const double dt = 1.0 / 60.0;
  const double expected = 100.0 - 9.81 * dt * dt * n * (n + 1) / 2.0;
  CHECK_NEAR(st(*w, b).position.y, expected, 2e-3);
  CHECK_NEAR(st(*w, b).linearVelocity.y, -9.81 * dt * n, 2e-3);
  CHECK_NEAR(st(*w, b).position.x, 0.0, 1e-6);
}

TEST(Physics, StepAccumulatesWholeFixedSteps) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  CHECK_EQ(w->step(1.0F / 60.0F), 1);
  CHECK_EQ(w->step(0.5F / 60.0F), 0);   // not enough time yet...
  CHECK_EQ(w->step(0.5F / 60.0F), 1);   // ...the remainder carries over
  CHECK_EQ(w->step(3.0F / 60.0F), 3);
  CHECK_EQ(w->step(0.0F), 0);
  CHECK_EQ(w->step(-1.0F), 0);
  CHECK_EQ(w->step(std::nanf("")), 0);
  CHECK_EQ(w->step(10.0F), 8);          // capped at maxSubSteps; the backlog is dropped, not replayed
  CHECK_EQ(w->step(1.0F / 60.0F), 1);
}

TEST(Physics, BallComesToRestOnTheFloor) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w);
  const BodyId ball = addBall(*w, {0, 3, 0});
  run(*w, 60 * 6);
  const BodyState s = st(*w, ball);
  CHECK_NEAR(s.position.y, 0.5, 0.03);  // radius above the surface, no tunnelling and no sinking
  CHECK(length(s.linearVelocity) < 0.05F);
  CHECK(s.sleeping);                    // islands go to sleep once settled
}

TEST(Physics, RestitutionControlsBounceHeight) {
  float apex[2] = {0, 0};
  for (int k = 0; k < 2; ++k) {
    auto w = makeWorld();
    REQUIRE(w != nullptr);
    BodyDesc f;
    f.type = BodyType::Static;
    f.shapes = {box({50, 0.5F, 50})};
    f.position = {0, -0.5F, 0};
    f.restitution = k == 0 ? 0.0F : 1.0F;
    add(*w, f);
    BodyDesc bd;
    bd.shapes = {sphere(0.5F)};
    bd.position = {0, 5, 0};
    bd.restitution = k == 0 ? 0.0F : 1.0F;
    const BodyId ball = add(*w, bd);
    bool bounced = false;
    for (int i = 0; i < 60 * 3; ++i) {
      w->stepOnce();
      const BodyState s = st(*w, ball);
      if (s.linearVelocity.y > 0.5F) bounced = true;
      if (bounced) apex[k] = std::max(apex[k], s.position.y);
    }
  }
  CHECK_MSG(apex[0] < 1.0F, "restitution 0 should not bounce, apex " + std::to_string(apex[0]));
  CHECK_MSG(apex[1] > 3.8F && apex[1] < 5.3F, "restitution 1 should rebound nearly to the drop height, apex " + std::to_string(apex[1]));
}

TEST(Physics, FrictionDeceleratesASlidingBox) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w, 1, 0xFFFFFFFFu, 1.0F);
  BodyDesc d;
  d.shapes = {box({0.5F, 0.5F, 0.5F})};
  d.position = {0, 0.5F, 0};
  d.friction = 0.5F;  // combined friction is the product: mu = 0.5 (a cube would tip over at mu >= 1)
  d.allowSleep = false;
  const BodyId b = add(*w, d);
  CHECK(w->setLinearVelocity(b, {5, 0, 0}).ok());
  run(*w, 15);  // 0.25 s: expected v ~ 5 - mu g t = 5 - 1.2
  const float v = st(*w, b).linearVelocity.x;
  CHECK_MSG(v > 3.5F && v < 4.2F, "sliding speed " + std::to_string(v));
  run(*w, 75);  // total 1.5 s > the 1.02 s a 5 m/s slide needs at 4.9 m/s^2
  CHECK_MSG(std::fabs(st(*w, b).linearVelocity.x) < 0.1F, "vx after 1.25s: " + std::to_string(st(*w, b).linearVelocity.x) + " x=" + std::to_string(st(*w, b).position.x));  // came to rest
}

TEST(Physics, ImpulseAndForce) {
  WorldDesc d;
  d.gravity = {0, 0, 0};
  auto w = makeWorld(d);
  REQUIRE(w != nullptr);
  BodyDesc bd;
  bd.shapes = {sphere(0.5F)};
  bd.mass = 2.0F;
  bd.allowSleep = false;
  const BodyId b = add(*w, bd);
  CHECK(w->applyImpulse(b, {4, 0, 0}).ok());
  CHECK_NEAR(st(*w, b).linearVelocity.x, 2.0, 1e-5);  // v = J / m
  CHECK(w->applyForce(b, {0, 6, 0}).ok());            // F = 6 N on 2 kg for one step of 1/60 s
  w->stepOnce();
  CHECK_NEAR(st(*w, b).linearVelocity.y, 3.0 / 60.0, 1e-4);
  CHECK(w->applyTorqueImpulse(b, {0, 0, 1}).ok());
  CHECK(st(*w, b).angularVelocity.z > 0.0F);
  // Invalid requests are errors, not silent no-ops.
  CHECK(!w->applyImpulse(b, {std::nanf(""), 0, 0}).ok());
  BodyDesc sd;
  sd.type = BodyType::Static;
  sd.shapes = {box({1, 1, 1})};
  const BodyId s = add(*w, sd);
  CHECK(!w->applyImpulse(s, {1, 0, 0}).ok());
}

TEST(Physics, RaycastFindsClosestSolidAndHonoursMasksAndIgnoresTriggers) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  const BodyId floor = addFloor(*w, 1);
  BodyDesc hi;
  hi.type = BodyType::Static;
  hi.shapes = {box({1, 0.5F, 1})};
  hi.position = {0, 2.5F, 0};  // spans y 2..3
  hi.layer = 2;
  hi.userData = 77;
  const BodyId platform = add(*w, hi);
  BodyDesc trig;
  trig.type = BodyType::Static;
  trig.isTrigger = true;
  trig.shapes = {box({1, 0.5F, 1})};
  trig.position = {0, 4.5F, 0};  // above the platform, would be hit first from y=10
  add(*w, trig);

  auto h = w->raycast({0, 10, 0}, {0, -1, 0}, 100);
  REQUIRE(h.has_value());
  CHECK(h->body == platform);
  CHECK_EQ(h->userData, std::uint64_t{77});
  CHECK_NEAR(h->distance, 7.0, 1e-3);  // trigger skipped; platform top at y=3
  CHECK_NEAR(h->point.y, 3.0, 1e-3);
  CHECK_NEAR(h->normal.y, 1.0, 1e-4);

  auto onlyLayer1 = w->raycast({0, 10, 0}, {0, -2, 0}, 100, 1);  // non-unit direction, layer 2 excluded
  REQUIRE(onlyLayer1.has_value());
  CHECK(onlyLayer1->body == floor);
  CHECK_NEAR(onlyLayer1->distance, 10.0, 1e-3);

  CHECK(!w->raycast({0, 10, 0}, {0, -1, 0}, 5).has_value());      // too short
  CHECK(!w->raycast({0, 10, 0}, {0, 1, 0}, 100).has_value());     // pointing away
  CHECK(!w->raycast({0, 10, 0}, {0, 0, 0}, 100).has_value());     // zero direction
  CHECK(!w->raycast({0, 10, 0}, {0, -1, 0}, 100, 8).has_value()); // no body on layer 8
}

TEST(Physics, OverlapSphereFindsSolidsAndTriggers) {
  WorldDesc wd;
  wd.gravity = {0, 0, 0};
  auto w = makeWorld(wd);
  REQUIRE(w != nullptr);
  const BodyId a = addBall(*w, {0, 0, 0});
  const BodyId b = addBall(*w, {3, 0, 0});
  BodyDesc t;
  t.type = BodyType::Static;
  t.isTrigger = true;
  t.shapes = {box({1, 1, 1})};
  t.position = {0.5F, 0, 0};
  const BodyId trig = add(*w, t);
  auto hits = w->overlapSphere({0, 0, 0}, 1.0F);
  std::set<std::uint32_t> got;
  for (BodyId id : hits) got.insert(id.index);
  CHECK(got.count(a.index) == 1);
  CHECK(got.count(trig.index) == 1);
  CHECK(got.count(b.index) == 0);
  CHECK(w->overlapSphere({0, 0, 0}, -1.0F).empty());
}

TEST(Physics, TriggerReportsEnterAndExitWithoutBlocking) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w);
  BodyDesc t;
  t.type = BodyType::Static;
  t.isTrigger = true;
  t.shapes = {box({2, 1, 2})};
  t.position = {0, 3, 0};
  t.userData = 9;
  const BodyId trig = add(*w, t);
  const BodyId ball = addBall(*w, {0, 8, 0});
  std::vector<ContactEvent> all;
  for (int i = 0; i < 60 * 3; ++i) {
    w->stepOnce();
    for (auto& e : w->drainEvents()) all.push_back(e);
  }
  CHECK(st(*w, ball).position.y < 1.0F);  // fell through the trigger onto the floor
  int enters = 0, exits = 0, enterIdx = -1, exitIdx = -1;
  for (std::size_t i = 0; i < all.size(); ++i) {
    const ContactEvent& e = all[i];
    if (e.type == ContactEventType::TriggerEnter) { ++enters; enterIdx = static_cast<int>(i); CHECK(e.a == trig); CHECK(e.b == ball); CHECK_EQ(e.userDataA, std::uint64_t{9}); }
    if (e.type == ContactEventType::TriggerExit) { ++exits; exitIdx = static_cast<int>(i); CHECK(e.a == trig); }
  }
  CHECK_EQ(enters, 1);
  CHECK_EQ(exits, 1);
  CHECK(enterIdx < exitIdx);
}

TEST(Physics, ContactBeginAndEndWithNormalFromAToB) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  const BodyId floor = addFloor(*w);
  const BodyId ball = addBall(*w, {0, 2, 0});
  std::vector<ContactEvent> ev;
  for (int i = 0; i < 90; ++i) {
    w->stepOnce();
    for (auto& e : w->drainEvents()) ev.push_back(e);
  }
  REQUIRE(!ev.empty());
  CHECK(ev[0].type == ContactEventType::ContactBegin);
  CHECK(ev[0].a == floor);  // a < b: the floor was created first
  CHECK(ev[0].b == ball);
  CHECK_MSG(ev[0].normal.y > 0.9F, "normal from floor (a) to ball (b) should point up");
  CHECK_NEAR(ev[0].point.y, 0.0, 0.05);
  int begins = 0;
  for (auto& e : ev) begins += e.type == ContactEventType::ContactBegin ? 1 : 0;
  CHECK_MSG(begins <= 3, "a ball that settles should not flood Begin events: " + std::to_string(begins));
  CHECK(w->setLinearVelocity(ball, {0, 8, 0}).ok());
  bool ended = false;
  for (int i = 0; i < 30; ++i) {
    w->stepOnce();
    for (auto& e : w->drainEvents()) ended = ended || e.type == ContactEventType::ContactEnd;
  }
  CHECK(ended);
}

TEST(Physics, DestroyingATouchingBodyEmitsTheEndEventFirst) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w);
  const BodyId ball = addBall(*w, {0, 0.5F, 0});
  run(*w, 20);
  w->drainEvents();
  CHECK(w->destroyBody(ball));
  auto ev = w->drainEvents();
  REQUIRE(ev.size() == 1);
  CHECK(ev[0].type == ContactEventType::ContactEnd);
  CHECK(ev[0].b == ball);  // the ids are still the (now dead) handles of that pair
  CHECK(!w->exists(ball));
}

TEST(Physics, LayersAndMasksFilterCollisions) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w, 2, ~4u);  // the floor ignores layer 4
  BodyDesc a;
  a.shapes = {sphere(0.5F)};
  a.position = {0, 2, 0};
  a.layer = 4;
  const BodyId ghost = add(*w, a);
  a.position = {5, 2, 0};
  a.layer = 8;
  const BodyId solid = add(*w, a);
  run(*w, 120);
  CHECK_MSG(st(*w, ghost).position.y < -3.0F, "layer 4 must fall through a floor that masks it out");
  CHECK_NEAR(st(*w, solid).position.y, 0.5, 0.05);
}

TEST(Physics, PlanarWorldKeepsBodiesInTheXyPlane) {
  WorldDesc d;
  d.planar2D = true;
  auto w = makeWorld(d);
  REQUIRE(w != nullptr);
  addFloor(*w);
  const BodyId b = [&] {
    BodyDesc bd;
    bd.shapes = {box({0.5F, 0.5F, 0.5F})};
    bd.position = {0, 3, 0};
    return add(*w, bd);
  }();
  CHECK(w->setLinearVelocity(b, {2, 0, 3}).ok());          // the Z component must be ignored
  CHECK(w->setAngularVelocity(b, {4, 5, 1}).ok());         // only rotation about Z survives
  run(*w, 180);
  const BodyState s = st(*w, b);
  CHECK_NEAR(s.position.z, 0.0, 1e-5);
  CHECK_NEAR(s.rotation.x, 0.0, 1e-5);
  CHECK_NEAR(s.rotation.y, 0.0, 1e-5);
  CHECK(s.position.x > 0.5F);                  // X motion is intact
  CHECK(std::fabs(s.linearVelocity.z) < 1e-5F);
}

TEST(Physics, AxisLocks) {
  WorldDesc wd;
  wd.gravity = {0, 0, 0};
  auto w = makeWorld(wd);
  REQUIRE(w != nullptr);
  BodyDesc d;
  d.shapes = {box({0.5F, 0.5F, 0.5F})};
  d.lockPositionX = true;
  d.lockRotationY = true;
  d.allowSleep = false;
  const BodyId b = add(*w, d);
  CHECK(w->setLinearVelocity(b, {5, 5, 5}).ok());
  CHECK(w->setAngularVelocity(b, {1, 1, 1}).ok());
  run(*w, 30);
  const BodyState s = st(*w, b);
  CHECK_NEAR(s.position.x, 0.0, 1e-6);
  CHECK(s.position.y > 1.0F && s.position.z > 1.0F);
  CHECK_NEAR(s.angularVelocity.y, 0.0, 1e-6);
}

TEST(Physics, KinematicBodyPushesButIsNotPushed) {
  WorldDesc wd;
  wd.gravity = {0, 0, 0};
  auto w = makeWorld(wd);
  REQUIRE(w != nullptr);
  BodyDesc k;
  k.type = BodyType::Kinematic;
  k.shapes = {box({0.5F, 0.5F, 0.5F})};
  k.position = {0, 0, 0};
  const BodyId kin = add(*w, k);
  const BodyId ball = addBall(*w, {2, 0, 0}, 0.5F, 1.0F);
  float x = 0;
  for (int i = 0; i < 120; ++i) {
    x += 0.05F;  // 3 m/s
    CHECK(w->setTransform(kin, {x, 0, 0}, Quat{}).ok());
    w->stepOnce();
  }
  CHECK_NEAR(st(*w, kin).position.x, x, 1e-5);       // never displaced by the ball
  CHECK_MSG(st(*w, ball).position.x > x, "ball should be ahead of the pusher");
  CHECK(st(*w, ball).position.x > 3.0F);
}

TEST(Physics, TriangleMeshFloorAndCompoundShapes) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  auto mesh = std::make_shared<MeshShapeData>();
  mesh->vertices = {{-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5}};
  mesh->indices = {0, 2, 1, 0, 3, 2};  // CCW seen from +Y
  BodyDesc f;
  f.type = BodyType::Static;
  ShapeDesc ms;
  ms.type = ShapeType::TriangleMesh;
  ms.mesh = mesh;
  f.shapes = {ms};
  add(*w, f);
  const BodyId ball = addBall(*w, {1, 2, 1});
  run(*w, 180);
  CHECK_NEAR(st(*w, ball).position.y, 0.5, 0.05);

  // A dumbbell: two boxes 4 m apart with nothing in between.
  WorldDesc g;
  g.gravity = {0, 0, 0};
  auto w2 = makeWorld(g);
  REQUIRE(w2 != nullptr);
  BodyDesc dd;
  ShapeDesc left = box({0.5F, 0.5F, 0.5F});
  left.offset = {-2, 0, 0};
  ShapeDesc right = box({0.5F, 0.5F, 0.5F});
  right.offset = {2, 0, 0};
  dd.shapes = {left, right};
  const BodyId bell = add(*w2, dd);
  auto hitRight = w2->raycast({2, 5, 0}, {0, -1, 0}, 20);
  REQUIRE(hitRight.has_value());
  CHECK(hitRight->body == bell);
  CHECK_NEAR(hitRight->point.y, 0.5, 1e-3);
  CHECK(!w2->raycast({0, 5, 0}, {0, -1, 0}, 20).has_value());  // the gap in the middle
}

TEST(Physics, CapsuleStandsOnTheFloorWhenRotationIsLocked) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w);
  BodyDesc d;
  ShapeDesc c;
  c.type = ShapeType::Capsule;
  c.radius = 0.4F;
  c.height = 1.0F;  // total height 1.8
  d.shapes = {c};
  d.position = {0, 3, 0};
  d.lockRotationX = d.lockRotationY = d.lockRotationZ = true;
  const BodyId b = add(*w, d);
  run(*w, 240);
  CHECK_NEAR(st(*w, b).position.y, 0.9, 0.04);
  CHECK_NEAR(st(*w, b).rotation.w, 1.0, 1e-4);
}

TEST(Physics, BodyHandlesGoStaleAndSlotsAreReused) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  const BodyId a = addBall(*w, {0, 0, 0});
  CHECK_EQ(w->bodyCount(), std::size_t{1});
  CHECK(w->destroyBody(a));
  CHECK(!w->destroyBody(a));  // double destroy is a harmless no-op
  CHECK(!w->exists(a));
  CHECK(!w->state(a).has_value());
  CHECK(w->setTransform(a, {1, 1, 1}, Quat{}).error().code == ErrorCode::NotFound);
  CHECK(!w->setLinearVelocity(a, {}).ok());
  const BodyId b = addBall(*w, {0, 0, 0});
  CHECK_EQ(b.index, a.index);
  CHECK(b.generation != a.generation);
  CHECK(!w->exists(a));
  CHECK(w->exists(b));
  CHECK(!w->destroyBody(BodyId{}));
  CHECK(!w->destroyBody(BodyId{999, 1}));
}

TEST(Physics, InvalidDescriptionsAreRejected) {
  CHECK(!World::create(WorldDesc{.gravity = {std::nanf(""), 0, 0}}).ok());
  CHECK(!World::create(WorldDesc{.fixedStep = 0.0F}).ok());
  CHECK(!World::create(WorldDesc{.maxSubSteps = 0}).ok());
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  BodyDesc d;
  CHECK(!w->createBody(d).ok());  // no shape
  d.shapes = {sphere(0.5F)};
  d.mass = 0.0F;
  CHECK(!w->createBody(d).ok());  // dynamic without mass
  d.mass = 1.0F;
  d.position = {std::nanf(""), 0, 0};
  CHECK(!w->createBody(d).ok());
  d.position = {};
  d.layer = 0;
  CHECK(!w->createBody(d).ok());
  d.layer = 1;
  d.linearDamping = 2.0F;
  CHECK(!w->createBody(d).ok());
  d.linearDamping = 0;
  d.shapes = {sphere(-1.0F)};
  CHECK(!w->createBody(d).ok());
  d.shapes = {box({0, 1, 1})};
  CHECK(!w->createBody(d).ok());
  ShapeDesc mesh;
  mesh.type = ShapeType::TriangleMesh;
  mesh.mesh = std::make_shared<MeshShapeData>(MeshShapeData{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2}});
  d.shapes = {mesh};
  CHECK(!w->createBody(d).ok());  // dynamic mesh
  d.type = BodyType::Static;
  CHECK(w->createBody(d).ok());
  mesh.mesh = std::make_shared<MeshShapeData>(MeshShapeData{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 5}});
  d.shapes = {mesh};
  CHECK(!w->createBody(d).ok());  // index out of range
  mesh.mesh = std::make_shared<MeshShapeData>(MeshShapeData{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1}});
  d.shapes = {mesh};
  CHECK(!w->createBody(d).ok());  // not a multiple of 3
  CHECK_EQ(w->bodyCount(), std::size_t{1});  // failed creations leave nothing behind
  CHECK(!w->setGravity({0, std::nanf(""), 0}).ok());
}

TEST(Physics, GravityCanBeChanged) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  const BodyId b = addBall(*w, {0, 0, 0});
  CHECK(w->setGravity({0, 0, -20}).ok());
  CHECK_NEAR(w->gravity().z, -20.0, 1e-6);
  run(*w, 30);
  CHECK(st(*w, b).position.z < -2.0F);
  CHECK_NEAR(st(*w, b).position.y, 0.0, 1e-4);
}

namespace {
std::vector<float> simulate(int bodies, int steps) {
  auto w = makeWorld();
  if (!w) return {};
  addFloor(*w);
  std::vector<BodyId> ids;
  for (int i = 0; i < bodies; ++i) {
    BodyDesc d;
    d.shapes = {i % 2 ? sphere(0.4F) : box({0.4F, 0.4F, 0.4F})};
    d.position = {static_cast<float>(i % 5) * 0.9F, 1.0F + static_cast<float>(i) * 0.9F, static_cast<float>((i * 7) % 3) * 0.3F};
    ids.push_back(add(*w, d));
  }
  for (int i = 0; i < steps; ++i) w->stepOnce();
  std::vector<float> out;
  for (BodyId id : ids) {
    const BodyState s = st(*w, id);
    out.insert(out.end(), {s.position.x, s.position.y, s.position.z, s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w});
  }
  return out;
}
}  // namespace

TEST(Physics, SimulationIsDeterministicOnOneMachine) {
  const auto a = simulate(40, 300);
  const auto b = simulate(40, 300);
  REQUIRE(a.size() == b.size() && !a.empty());
  CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
  bool anyMoved = false;
  for (std::size_t i = 1; i < a.size(); i += 7) anyMoved = anyMoved || a[i] < 30.0F;
  CHECK(anyMoved);
}

TEST(Physics, ManyBodiesStayFiniteAndSettle) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w);
  std::vector<BodyId> ids;
  for (int i = 0; i < 600; ++i) {
    BodyDesc d;
    d.shapes = {i % 3 == 0 ? sphere(0.3F) : box({0.3F, 0.3F, 0.3F})};
    d.position = {static_cast<float>(i % 10) * 0.7F - 3, 1.0F + static_cast<float>(i / 10) * 0.7F, static_cast<float>((i / 3) % 7) * 0.7F - 2};
    ids.push_back(add(*w, d));
  }
  for (int i = 0; i < 240; ++i) w->stepOnce();
  int below = 0;
  for (BodyId id : ids) {
    const BodyState s = st(*w, id);
    CHECK(finiteState(s));
    if (s.position.y < -1.0F) ++below;  // nothing may tunnel through the floor
  }
  CHECK_EQ(below, 0);
}

TEST(Physics, CreateAndDestroyChurnDoesNotLeakOrCorrupt) {
  auto w = makeWorld();
  REQUIRE(w != nullptr);
  addFloor(*w);
  for (int round = 0; round < 50; ++round) {
    std::vector<BodyId> ids;
    for (int i = 0; i < 100; ++i) ids.push_back(addBall(*w, {static_cast<float>(i % 10), 0.5F + static_cast<float>(i / 10), 0}));
    run(*w, 3);
    w->drainEvents();
    for (BodyId id : ids) CHECK(w->destroyBody(id));
    CHECK_EQ(w->bodyCount(), std::size_t{1});
  }
}
