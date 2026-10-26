// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/physics/physics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <btBulletDynamicsCommon.h>

namespace mingol::physics {
namespace {

constexpr float kTouchDistance = 0.005F;  // a manifold point this close counts as touching
constexpr std::size_t kMaxTriangles = 4'000'000;

bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(Quat q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

btVector3 toBt(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 fromBt(const btVector3& v) { return {v.x(), v.y(), v.z()}; }
btQuaternion toBt(Quat q) { return {q.x, q.y, q.z, q.w}; }
Quat fromBt(const btQuaternion& q) { return {q.x(), q.y(), q.z(), q.w()}; }

struct Record {
  std::vector<std::unique_ptr<btCollisionShape>> childShapes;
  std::unique_ptr<btCollisionShape> shape;
  // Triangle mesh storage must outlive the shape.
  std::vector<float> meshVertices;
  std::vector<int> meshIndices;
  std::unique_ptr<btTriangleIndexVertexArray> meshArray;
  std::unique_ptr<btDefaultMotionState> motion;
  std::unique_ptr<btRigidBody> body;
  BodyType type = BodyType::Dynamic;
  bool trigger = false;
  std::uint32_t layer = 1;
  std::uint64_t userData = 0;
};

struct PairInfo {
  BodyId a, b;
  Vec3 point{0, 0, 0};
  Vec3 normal{0, 0, 0};
  bool trigger = false;
};

// Raycast filter: select layers by `mask`, skip triggers.
struct SolidRayCallback : btCollisionWorld::ClosestRayResultCallback {
  SolidRayCallback(const btVector3& from, const btVector3& to, std::uint32_t m)
      : ClosestRayResultCallback(from, to), mask(m) {}
  bool needsCollision(btBroadphaseProxy* proxy) const override {
    const auto* obj = static_cast<const btCollisionObject*>(proxy->m_clientObject);
    if ((obj->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) != 0) return false;
    return (static_cast<std::uint32_t>(proxy->m_collisionFilterGroup) & mask) != 0;
  }
  std::uint32_t mask;
};

struct OverlapCallback : btCollisionWorld::ContactResultCallback {
  explicit OverlapCallback(std::uint32_t m) : mask(m) {}
  bool needsCollision(btBroadphaseProxy* proxy) const override {
    return (static_cast<std::uint32_t>(proxy->m_collisionFilterGroup) & mask) != 0;
  }
  btScalar addSingleResult(btManifoldPoint&, const btCollisionObjectWrapper*, int, int,
                           const btCollisionObjectWrapper* other, int, int) override {
    hits.insert(other->getCollisionObject()->getUserIndex());
    return 0;
  }
  std::uint32_t mask;
  std::set<int> hits;
};

}  // namespace

struct World::Impl {
  WorldDesc desc;
  btDefaultCollisionConfiguration config;
  std::unique_ptr<btCollisionDispatcher> dispatcher;
  std::unique_ptr<btDbvtBroadphase> broadphase;
  std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
  std::unique_ptr<btDiscreteDynamicsWorld> world;

  std::vector<std::unique_ptr<Record>> slots;
  std::vector<std::uint32_t> generations;
  std::vector<std::uint32_t> freeSlots;
  std::size_t alive = 0;

  float accumulator = 0.0F;
  std::map<std::pair<BodyId, BodyId>, PairInfo> touching;  // ordered pair (a < b)
  std::vector<ContactEvent> events;

  Impl() {
    dispatcher = std::make_unique<btCollisionDispatcher>(&config);
    broadphase = std::make_unique<btDbvtBroadphase>();
    solver = std::make_unique<btSequentialImpulseConstraintSolver>();
    world = std::make_unique<btDiscreteDynamicsWorld>(dispatcher.get(), broadphase.get(), solver.get(), &config);
    world->setWorldUserInfo(this);
    world->setInternalTickCallback(&Impl::tick, this, false);
  }

  ~Impl() {
    for (auto& r : slots) {
      if (r) world->removeRigidBody(r->body.get());
    }
  }

  static void tick(btDynamicsWorld* w, btScalar) { static_cast<Impl*>(w->getWorldUserInfo())->collectContacts(); }

  BodyId idOfSlot(std::uint32_t slot) const { return {slot + 1, generations[slot]}; }
  Record* find(BodyId id) const {
    if (id.index == 0 || id.index > slots.size()) return nullptr;
    const std::uint32_t slot = id.index - 1;
    if (generations[slot] != id.generation) return nullptr;
    return slots[slot].get();
  }
  Record* recordOf(const btCollisionObject* o) const {
    const int idx = o->getUserIndex();
    if (idx <= 0 || static_cast<std::size_t>(idx) > slots.size()) return nullptr;
    return slots[static_cast<std::size_t>(idx) - 1].get();
  }

  void collectContacts() {
    std::map<std::pair<BodyId, BodyId>, PairInfo> now;
    const int n = dispatcher->getNumManifolds();
    for (int i = 0; i < n; ++i) {
      const btPersistentManifold* m = dispatcher->getManifoldByIndexInternal(i);
      const int pts = m->getNumContacts();
      if (pts == 0) continue;
      const btManifoldPoint* best = nullptr;
      for (int j = 0; j < pts; ++j) {
        const btManifoldPoint& p = m->getContactPoint(j);
        if (p.getDistance() <= kTouchDistance && (best == nullptr || p.getDistance() < best->getDistance())) best = &p;
      }
      if (best == nullptr) continue;
      const btCollisionObject* o0 = m->getBody0();
      const btCollisionObject* o1 = m->getBody1();
      const Record* r0 = recordOf(o0);
      const Record* r1 = recordOf(o1);
      if (r0 == nullptr || r1 == nullptr) continue;
      BodyId a = idOfSlot(static_cast<std::uint32_t>(o0->getUserIndex() - 1));
      BodyId b = idOfSlot(static_cast<std::uint32_t>(o1->getUserIndex() - 1));
      // Bullet's normalWorldOnB points from B towards A; ours points from a towards b.
      Vec3 normal = -fromBt(best->m_normalWorldOnB);
      const bool trigger = r0->trigger || r1->trigger;
      // Order: the trigger first when exactly one side is a trigger, otherwise the smaller id.
      const bool swap = (r0->trigger != r1->trigger) ? r1->trigger : !(a < b);
      if (swap) {
        std::swap(a, b);
        normal = -normal;
      }
      // Key by sorted ids so a pair is one entry whichever side is the trigger.
      const auto key = a < b ? std::pair{a, b} : std::pair{b, a};
      PairInfo info;
      info.point = fromBt(best->getPositionWorldOnB());
      info.normal = normal;
      info.trigger = trigger;
      info.a = a;
      info.b = b;
      now.emplace(key, info);
    }
    // Begin / enter.
    for (const auto& [key, info] : now) {
      if (touching.count(key) != 0) continue;
      emit(info.trigger ? ContactEventType::TriggerEnter : ContactEventType::ContactBegin, info.a, info.b, info.point, info.normal);
    }
    // End / exit.
    for (const auto& [key, info] : touching) {
      if (now.count(key) != 0) continue;
      emit(info.trigger ? ContactEventType::TriggerExit : ContactEventType::ContactEnd, info.a, info.b, {}, {});
    }
    touching = std::move(now);
  }

  void emit(ContactEventType type, BodyId a, BodyId b, Vec3 point, Vec3 normal) {
    ContactEvent e;
    e.type = type;
    e.a = a;
    e.b = b;
    if (const Record* r = find(a)) e.userDataA = r->userData;
    if (const Record* r = find(b)) e.userDataB = r->userData;
    e.point = point;
    e.normal = normal;
    events.push_back(e);
  }
};

// ---- World ------------------------------------------------------------------------------------

World::World() = default;
World::~World() = default;

Result<std::unique_ptr<World>> World::create(const WorldDesc& desc) {
  if (!finite(desc.gravity)) return makeError(ErrorCode::InvalidArgument, "gravity must be finite");
  if (!(desc.fixedStep > 0.0F) || !std::isfinite(desc.fixedStep) || desc.fixedStep > 1.0F) {
    return makeError(ErrorCode::InvalidArgument, "fixedStep must be in (0, 1] seconds");
  }
  if (desc.maxSubSteps < 1 || desc.maxSubSteps > 1000) return makeError(ErrorCode::InvalidArgument, "maxSubSteps must be 1..1000");
  std::unique_ptr<World> w(new World());
  w->impl_ = std::make_unique<Impl>();
  w->impl_->desc = desc;
  w->impl_->world->setGravity(toBt(desc.gravity));
  return w;
}

namespace {

Result<std::unique_ptr<btCollisionShape>> makeShape(const ShapeDesc& s, BodyType bodyType, Record& rec) {
  switch (s.type) {
    case ShapeType::Box:
      if (!finite(s.halfExtents) || s.halfExtents.x <= 0 || s.halfExtents.y <= 0 || s.halfExtents.z <= 0) {
        return makeError(ErrorCode::InvalidArgument, "box half extents must be positive and finite");
      }
      return std::unique_ptr<btCollisionShape>(new btBoxShape(toBt(s.halfExtents)));
    case ShapeType::Sphere:
      if (!(s.radius > 0.0F) || !std::isfinite(s.radius)) return makeError(ErrorCode::InvalidArgument, "sphere radius must be positive");
      return std::unique_ptr<btCollisionShape>(new btSphereShape(s.radius));
    case ShapeType::Capsule:
      if (!(s.radius > 0.0F) || !std::isfinite(s.radius) || !(s.height >= 0.0F) || !std::isfinite(s.height)) {
        return makeError(ErrorCode::InvalidArgument, "capsule needs radius > 0 and height >= 0");
      }
      return std::unique_ptr<btCollisionShape>(new btCapsuleShape(s.radius, s.height));
    case ShapeType::TriangleMesh: {
      if (bodyType != BodyType::Static) return makeError(ErrorCode::InvalidArgument, "triangle meshes are only allowed on static bodies");
      if (!s.mesh || s.mesh->vertices.empty() || s.mesh->indices.empty() || s.mesh->indices.size() % 3 != 0) {
        return makeError(ErrorCode::InvalidArgument, "triangle mesh needs vertices and a multiple of 3 indices");
      }
      if (s.mesh->indices.size() / 3 > kMaxTriangles) return makeError(ErrorCode::OutOfRange, "triangle mesh is too large");
      for (const Vec3& v : s.mesh->vertices) {
        if (!finite(v)) return makeError(ErrorCode::InvalidArgument, "triangle mesh has non-finite vertices");
      }
      for (std::uint32_t i : s.mesh->indices) {
        if (i >= s.mesh->vertices.size()) return makeError(ErrorCode::OutOfRange, "triangle mesh index {} is out of range", i);
      }
      rec.meshVertices.clear();
      for (const Vec3& v : s.mesh->vertices) rec.meshVertices.insert(rec.meshVertices.end(), {v.x, v.y, v.z});
      rec.meshIndices.assign(s.mesh->indices.begin(), s.mesh->indices.end());
      rec.meshArray = std::make_unique<btTriangleIndexVertexArray>(
          static_cast<int>(rec.meshIndices.size() / 3), rec.meshIndices.data(), static_cast<int>(3 * sizeof(int)),
          static_cast<int>(s.mesh->vertices.size()), rec.meshVertices.data(), static_cast<int>(3 * sizeof(float)));
      return std::unique_ptr<btCollisionShape>(new btBvhTriangleMeshShape(rec.meshArray.get(), true));
    }
  }
  return makeError(ErrorCode::InvalidArgument, "unknown shape type");
}

}  // namespace

Result<BodyId> World::createBody(const BodyDesc& d) {
  Impl& im = *impl_;
  if (d.shapes.empty()) return makeError(ErrorCode::InvalidArgument, "a body needs at least one shape");
  if (d.type == BodyType::Dynamic && !(d.mass > 0.0F && std::isfinite(d.mass))) {
    return makeError(ErrorCode::InvalidArgument, "a dynamic body needs a positive, finite mass");
  }
  if (!finite(d.position) || !finite(d.rotation)) return makeError(ErrorCode::InvalidArgument, "position/rotation must be finite");
  if (!(d.friction >= 0.0F) || !std::isfinite(d.friction) || !std::isfinite(d.restitution) || d.restitution < 0.0F) {
    return makeError(ErrorCode::InvalidArgument, "friction/restitution out of range");
  }
  if (!(d.linearDamping >= 0.0F && d.linearDamping <= 1.0F) || !(d.angularDamping >= 0.0F && d.angularDamping <= 1.0F)) {
    return makeError(ErrorCode::InvalidArgument, "damping must be within 0..1");
  }
  if (d.layer == 0) return makeError(ErrorCode::InvalidArgument, "layer must have at least one bit set");

  auto rec = std::make_unique<Record>();
  rec->type = d.type;
  rec->trigger = d.isTrigger;
  rec->layer = d.layer;
  rec->userData = d.userData;

  const bool single = d.shapes.size() == 1 && lengthSq(d.shapes[0].offset) == 0.0F &&
                      d.shapes[0].rotation.x == 0 && d.shapes[0].rotation.y == 0 && d.shapes[0].rotation.z == 0;
  if (single) {
    MINGOL_TRY_ASSIGN(auto s, makeShape(d.shapes[0], d.type, *rec));
    rec->shape = std::move(s);
  } else {
    auto compound = std::make_unique<btCompoundShape>();
    for (const ShapeDesc& sd : d.shapes) {
      if (!finite(sd.offset) || !finite(sd.rotation)) return makeError(ErrorCode::InvalidArgument, "shape offset/rotation must be finite");
      MINGOL_TRY_ASSIGN(auto s, makeShape(sd, d.type, *rec));
      btTransform local(toBt(normalize(sd.rotation)), toBt(sd.offset));
      compound->addChildShape(local, s.get());
      rec->childShapes.push_back(std::move(s));
    }
    rec->shape = std::move(compound);
  }

  btVector3 inertia(0, 0, 0);
  const float mass = d.type == BodyType::Dynamic ? d.mass : 0.0F;
  if (mass > 0.0F) rec->shape->calculateLocalInertia(mass, inertia);
  btTransform start(toBt(normalize(d.rotation)), toBt(d.position));
  rec->motion = std::make_unique<btDefaultMotionState>(start);
  btRigidBody::btRigidBodyConstructionInfo info(mass, rec->motion.get(), rec->shape.get(), inertia);
  info.m_friction = d.friction;
  info.m_restitution = d.restitution;
  info.m_linearDamping = d.linearDamping;
  info.m_angularDamping = d.angularDamping;
  rec->body = std::make_unique<btRigidBody>(info);
  btRigidBody& body = *rec->body;

  int flags = body.getCollisionFlags();
  if (d.type == BodyType::Kinematic) flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
  if (d.type == BodyType::Static) flags |= btCollisionObject::CF_STATIC_OBJECT;
  if (d.isTrigger) flags |= btCollisionObject::CF_NO_CONTACT_RESPONSE;
  body.setCollisionFlags(flags);
  if (d.type == BodyType::Kinematic || !d.allowSleep) body.setActivationState(DISABLE_DEACTIVATION);

  const bool planar = im.desc.planar2D;
  body.setLinearFactor(btVector3(d.lockPositionX ? 0.0F : 1.0F, d.lockPositionY ? 0.0F : 1.0F, (planar || d.lockPositionZ) ? 0.0F : 1.0F));
  body.setAngularFactor(btVector3((planar || d.lockRotationX) ? 0.0F : 1.0F, (planar || d.lockRotationY) ? 0.0F : 1.0F, d.lockRotationZ ? 0.0F : 1.0F));

  std::uint32_t slot;
  if (!im.freeSlots.empty()) {
    slot = im.freeSlots.back();
    im.freeSlots.pop_back();
  } else {
    slot = static_cast<std::uint32_t>(im.slots.size());
    im.slots.emplace_back();
    im.generations.push_back(1);
  }
  body.setUserIndex(static_cast<int>(slot) + 1);
  im.world->addRigidBody(&body, static_cast<int>(d.layer), static_cast<int>(d.mask));
  im.slots[slot] = std::move(rec);
  ++im.alive;
  return im.idOfSlot(slot);
}

bool World::destroyBody(BodyId id) {
  Impl& im = *impl_;
  Record* r = im.find(id);
  if (r == nullptr) return false;
  // Listeners must see every pair end before the body disappears.
  for (auto it = im.touching.begin(); it != im.touching.end();) {
    if (it->first.first == id || it->first.second == id) {
      im.emit(it->second.trigger ? ContactEventType::TriggerExit : ContactEventType::ContactEnd, it->second.a, it->second.b, {}, {});
      it = im.touching.erase(it);
    } else {
      ++it;
    }
  }
  im.world->removeRigidBody(r->body.get());
  const std::uint32_t slot = id.index - 1;
  im.slots[slot].reset();
  ++im.generations[slot];
  im.freeSlots.push_back(slot);
  --im.alive;
  return true;
}

bool World::exists(BodyId id) const noexcept { return impl_->find(id) != nullptr; }
std::size_t World::bodyCount() const noexcept { return impl_->alive; }

void World::stepOnce() {
  impl_->world->stepSimulation(impl_->desc.fixedStep, 0, 0.0F);  // one exact step, no Bullet-side accumulation
}

int World::step(float dt) {
  Impl& im = *impl_;
  if (!(dt > 0.0F) || !std::isfinite(dt)) return 0;
  im.accumulator += dt;
  const float fixed = im.desc.fixedStep;
  int steps = 0;
  while (im.accumulator + fixed * 1e-4F >= fixed && steps < im.desc.maxSubSteps) {
    stepOnce();
    im.accumulator -= fixed;
    ++steps;
  }
  if (im.accumulator < 0.0F) im.accumulator = 0.0F;
  if (steps == im.desc.maxSubSteps) im.accumulator = 0.0F;  // fell behind: drop the backlog instead of spiralling
  return steps;
}

std::optional<BodyState> World::state(BodyId id) const {
  const Record* r = impl_->find(id);
  if (r == nullptr) return std::nullopt;
  const btRigidBody& b = *r->body;
  BodyState s;
  s.position = fromBt(b.getWorldTransform().getOrigin());
  s.rotation = fromBt(b.getWorldTransform().getRotation());
  s.linearVelocity = fromBt(b.getLinearVelocity());
  s.angularVelocity = fromBt(b.getAngularVelocity());
  s.sleeping = b.getActivationState() == ISLAND_SLEEPING;
  return s;
}

Result<void> World::setTransform(BodyId id, Vec3 position, Quat rotation) {
  Record* r = impl_->find(id);
  if (r == nullptr) return makeError(ErrorCode::NotFound, "unknown or destroyed body");
  if (!finite(position) || !finite(rotation)) return makeError(ErrorCode::InvalidArgument, "transform must be finite");
  btTransform t(toBt(normalize(rotation)), toBt(position));
  r->body->setWorldTransform(t);
  r->motion->setWorldTransform(t);
  r->body->setInterpolationWorldTransform(t);
  impl_->world->updateSingleAabb(r->body.get());
  if (r->type != BodyType::Static) r->body->activate(true);
  return {};
}

Result<void> World::setLinearVelocity(BodyId id, Vec3 v) {
  Record* r = impl_->find(id);
  if (r == nullptr) return makeError(ErrorCode::NotFound, "unknown or destroyed body");
  if (!finite(v)) return makeError(ErrorCode::InvalidArgument, "velocity must be finite");
  // Bullet applies the axis factors to forces and contacts but not to a velocity set directly.
  r->body->setLinearVelocity(toBt(v) * r->body->getLinearFactor());
  if (r->type == BodyType::Dynamic) r->body->activate(true);
  return {};
}

Result<void> World::setAngularVelocity(BodyId id, Vec3 v) {
  Record* r = impl_->find(id);
  if (r == nullptr) return makeError(ErrorCode::NotFound, "unknown or destroyed body");
  if (!finite(v)) return makeError(ErrorCode::InvalidArgument, "velocity must be finite");
  r->body->setAngularVelocity(toBt(v) * r->body->getAngularFactor());
  if (r->type == BodyType::Dynamic) r->body->activate(true);
  return {};
}

Result<void> World::applyForce(BodyId id, Vec3 f) {
  Record* r = impl_->find(id);
  if (r == nullptr) return makeError(ErrorCode::NotFound, "unknown or destroyed body");
  if (!finite(f)) return makeError(ErrorCode::InvalidArgument, "force must be finite");
  if (r->type != BodyType::Dynamic) return makeError(ErrorCode::InvalidArgument, "forces only act on dynamic bodies");
  r->body->applyCentralForce(toBt(f));
  r->body->activate(true);
  return {};
}

Result<void> World::applyImpulse(BodyId id, Vec3 j) {
  Record* r = impl_->find(id);
  if (r == nullptr) return makeError(ErrorCode::NotFound, "unknown or destroyed body");
  if (!finite(j)) return makeError(ErrorCode::InvalidArgument, "impulse must be finite");
  if (r->type != BodyType::Dynamic) return makeError(ErrorCode::InvalidArgument, "impulses only act on dynamic bodies");
  r->body->applyCentralImpulse(toBt(j));
  r->body->activate(true);
  return {};
}

Result<void> World::applyTorqueImpulse(BodyId id, Vec3 j) {
  Record* r = impl_->find(id);
  if (r == nullptr) return makeError(ErrorCode::NotFound, "unknown or destroyed body");
  if (!finite(j)) return makeError(ErrorCode::InvalidArgument, "torque must be finite");
  if (r->type != BodyType::Dynamic) return makeError(ErrorCode::InvalidArgument, "torque only acts on dynamic bodies");
  r->body->applyTorqueImpulse(toBt(j));
  r->body->activate(true);
  return {};
}

Result<void> World::setGravity(Vec3 g) {
  if (!finite(g)) return makeError(ErrorCode::InvalidArgument, "gravity must be finite");
  impl_->desc.gravity = g;
  impl_->world->setGravity(toBt(g));
  for (auto& r : impl_->slots) {
    if (r && r->type == BodyType::Dynamic) r->body->activate(true);
  }
  return {};
}

Vec3 World::gravity() const noexcept { return impl_->desc.gravity; }
float World::fixedStep() const noexcept { return impl_->desc.fixedStep; }

std::optional<RayHit> World::raycast(Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t mask) const {
  if (!finite(origin) || !finite(direction) || !(maxDistance > 0.0F) || !std::isfinite(maxDistance)) return std::nullopt;
  if (lengthSq(direction) < 1e-20F) return std::nullopt;
  const Vec3 dir = normalize(direction);
  const btVector3 from = toBt(origin);
  const btVector3 to = toBt(origin + dir * maxDistance);
  SolidRayCallback cb(from, to, mask);
  impl_->world->rayTest(from, to, cb);
  if (!cb.hasHit()) return std::nullopt;
  const Record* r = impl_->recordOf(cb.m_collisionObject);
  if (r == nullptr) return std::nullopt;
  RayHit hit;
  hit.body = impl_->idOfSlot(static_cast<std::uint32_t>(cb.m_collisionObject->getUserIndex() - 1));
  hit.userData = r->userData;
  hit.point = fromBt(cb.m_hitPointWorld);
  hit.normal = fromBt(cb.m_hitNormalWorld);
  hit.distance = cb.m_closestHitFraction * maxDistance;
  return hit;
}

std::vector<BodyId> World::overlapSphere(Vec3 center, float radius, std::uint32_t mask) const {
  std::vector<BodyId> out;
  if (!finite(center) || !(radius > 0.0F) || !std::isfinite(radius)) return out;
  btSphereShape sphere(radius);
  btCollisionObject probe;
  probe.setCollisionShape(&sphere);
  probe.setWorldTransform(btTransform(btQuaternion::getIdentity(), toBt(center)));
  OverlapCallback cb(mask);
  cb.m_collisionFilterGroup = -1;
  cb.m_collisionFilterMask = -1;
  impl_->world->contactTest(&probe, cb);
  for (int idx : cb.hits) {
    if (idx > 0 && static_cast<std::size_t>(idx) <= impl_->slots.size() && impl_->slots[static_cast<std::size_t>(idx) - 1]) {
      out.push_back(impl_->idOfSlot(static_cast<std::uint32_t>(idx) - 1));
    }
  }
  return out;
}

std::vector<ContactEvent> World::drainEvents() {
  std::vector<ContactEvent> out;
  out.swap(impl_->events);
  return out;
}

}  // namespace mingol::physics
