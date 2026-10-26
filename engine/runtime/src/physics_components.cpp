// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/runtime/physics_components.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include "mingol/core/log.hpp"

namespace mingol::runtime {
namespace {

constexpr float kMovedPosSq = 1e-8F;       // (0.1 mm)^2
constexpr float kMovedRotDot = 1.0F - 1e-9F;  // ~0.003 degrees

bool poseDiffers(Vec3 p0, Quat q0, Vec3 p1, Quat q1) {
  if (lengthSq(p0 - p1) > kMovedPosSq) return true;
  const float d = std::fabs(q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w);
  return d < kMovedRotDot;
}

Collider* addCollider(scene::GameObject& go, physics::ShapeDesc s) {
  Collider* c = go.addComponent<Collider>();
  c->shape = std::move(s);
  return c;
}

}  // namespace

std::uint64_t packObjectId(scene::GameObjectId id) noexcept {
  return (static_cast<std::uint64_t>(id.index) << 32) | id.generation;
}
scene::GameObjectId unpackObjectId(std::uint64_t v) noexcept {
  return {static_cast<std::uint32_t>(v >> 32), static_cast<std::uint32_t>(v & 0xFFFFFFFFu)};
}

// ---- Collider -------------------------------------------------------------------------------------

Collider* Collider::addBox(scene::GameObject& go, Vec3 halfExtents, Vec3 center) {
  physics::ShapeDesc s;
  s.type = physics::ShapeType::Box;
  s.halfExtents = halfExtents;
  s.offset = center;
  return addCollider(go, std::move(s));
}
Collider* Collider::addSphere(scene::GameObject& go, float radius, Vec3 center) {
  physics::ShapeDesc s;
  s.type = physics::ShapeType::Sphere;
  s.radius = radius;
  s.offset = center;
  return addCollider(go, std::move(s));
}
Collider* Collider::addCapsule(scene::GameObject& go, float radius, float height, Vec3 center) {
  physics::ShapeDesc s;
  s.type = physics::ShapeType::Capsule;
  s.radius = radius;
  s.height = height;
  s.offset = center;
  return addCollider(go, std::move(s));
}
Collider* Collider::addMesh(scene::GameObject& go, std::shared_ptr<const physics::MeshShapeData> mesh) {
  physics::ShapeDesc s;
  s.type = physics::ShapeType::TriangleMesh;
  s.mesh = std::move(mesh);
  return addCollider(go, std::move(s));
}

// ---- Rigidbody ------------------------------------------------------------------------------------

Result<void> Rigidbody::need() const {
  if (!body_.valid()) return makeError(ErrorCode::NotFound, "Rigidbody on '{}' has no physics body (disabled, or no Collider)", gameObject().name());
  return {};
}

void Rigidbody::onEnable() {
  if (started_) createBody();
}

void Rigidbody::onStart() {
  started_ = true;
  createBody();
}

void Rigidbody::createBody() {
  if (body_.valid()) return;
  auto* sys = scene().service<PhysicsSystem>();
  if (sys == nullptr) {
    MINGOL_LOG_ERROR("physics", "Rigidbody on '{}': the scene has no PhysicsSystem service", gameObject().name());
    return;
  }
  const scene::Transform& t = gameObject().transform();
  const Vec3 scale = t.worldScale();
  const Vec3 as{std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z)};

  physics::BodyDesc d;
  d.type = type;
  d.mass = mass;
  d.friction = friction;
  d.restitution = restitution;
  d.linearDamping = linearDamping;
  d.angularDamping = angularDamping;
  d.isTrigger = isTrigger;
  d.allowSleep = allowSleep;
  d.layer = layer;
  d.mask = mask;
  d.lockRotationX = lockRotationX; d.lockRotationY = lockRotationY; d.lockRotationZ = lockRotationZ;
  d.lockPositionX = lockPositionX; d.lockPositionY = lockPositionY; d.lockPositionZ = lockPositionZ;
  d.position = t.worldPosition();
  d.rotation = t.worldRotation();
  d.userData = packObjectId(gameObject().id());

  for (Collider* c : gameObject().getComponents<Collider>()) {
    if (!c->enabled()) continue;
    physics::ShapeDesc s = c->shape;
    s.offset = s.offset * scale;  // component-wise, signed
    switch (s.type) {
      case physics::ShapeType::Box: s.halfExtents = s.halfExtents * as; break;
      case physics::ShapeType::Sphere: s.radius *= std::max({as.x, as.y, as.z}); break;
      case physics::ShapeType::Capsule:
        s.radius *= std::max(as.x, as.z);
        s.height *= as.y;
        break;
      case physics::ShapeType::TriangleMesh:
        if (s.mesh && (as.x != 1.0F || as.y != 1.0F || as.z != 1.0F)) {
          auto scaled = std::make_shared<physics::MeshShapeData>(*s.mesh);
          for (Vec3& v : scaled->vertices) v = v * scale;
          s.mesh = std::move(scaled);
        }
        break;
    }
    d.shapes.push_back(std::move(s));
  }
  if (d.shapes.empty()) {
    MINGOL_LOG_WARN("physics", "Rigidbody on '{}' has no enabled Collider; no body was created", gameObject().name());
    return;
  }
  auto r = sys->world().createBody(d);
  if (!r.ok()) {
    MINGOL_LOG_ERROR("physics", "Rigidbody on '{}': {}", gameObject().name(), r.error().describe());
    return;
  }
  body_ = r.value();
  syncedPos_ = d.position;
  syncedRot_ = d.rotation;
  sys->add(this);
}

void Rigidbody::onDisable() {
  if (!body_.valid()) return;
  if (auto* sys = scene().service<PhysicsSystem>()) {
    sys->remove(this);
    sys->world().destroyBody(body_);
  }
  body_ = {};
}

namespace {
physics::World* worldOf(scene::Component& c) {
  auto* sys = c.scene().service<PhysicsSystem>();
  return sys != nullptr ? &sys->world() : nullptr;
}
}  // namespace

#define MINGOL_RB_CALL(EXPR)                                  \
  MINGOL_TRY(need());                                         \
  physics::World* w = worldOf(*this);                         \
  if (w == nullptr) return makeError(ErrorCode::NotFound, "no PhysicsSystem"); \
  return w->EXPR

Result<void> Rigidbody::setVelocity(Vec3 v) { MINGOL_RB_CALL(setLinearVelocity(body_, v)); }
Result<void> Rigidbody::setAngularVelocity(Vec3 v) { MINGOL_RB_CALL(setAngularVelocity(body_, v)); }
Result<void> Rigidbody::addForce(Vec3 f) { MINGOL_RB_CALL(applyForce(body_, f)); }
Result<void> Rigidbody::addImpulse(Vec3 i) { MINGOL_RB_CALL(applyImpulse(body_, i)); }
Result<void> Rigidbody::addTorqueImpulse(Vec3 i) { MINGOL_RB_CALL(applyTorqueImpulse(body_, i)); }
#undef MINGOL_RB_CALL

std::optional<physics::BodyState> Rigidbody::state() const {
  if (!body_.valid()) return std::nullopt;
  physics::World* w = worldOf(const_cast<Rigidbody&>(*this));
  return w != nullptr ? w->state(body_) : std::nullopt;
}

// ---- PhysicsSystem --------------------------------------------------------------------------------

Result<std::unique_ptr<PhysicsSystem>> PhysicsSystem::create(const physics::WorldDesc& desc) {
  MINGOL_TRY_ASSIGN(auto w, physics::World::create(desc));
  return std::make_unique<PhysicsSystem>(std::move(w));
}

void PhysicsSystem::remove(Rigidbody* rb) { bodies_.erase(std::remove(bodies_.begin(), bodies_.end(), rb), bodies_.end()); }

void PhysicsSystem::pushTransforms() {
  for (Rigidbody* rb : bodies_) {
    const scene::Transform& t = rb->gameObject().transform();
    const Vec3 p = t.worldPosition();
    const Quat q = t.worldRotation();
    if (!poseDiffers(p, q, rb->syncedPos_, rb->syncedRot_)) continue;
    if (world_->setTransform(rb->body_, p, q).ok()) {
      rb->syncedPos_ = p;
      rb->syncedRot_ = q;
    }
  }
}

void PhysicsSystem::pullTransforms() {
  for (Rigidbody* rb : bodies_) {
    if (rb->type != physics::BodyType::Dynamic) continue;
    auto s = world_->state(rb->body_);
    if (!s || s->sleeping) continue;
    if (!poseDiffers(s->position, s->rotation, rb->syncedPos_, rb->syncedRot_)) continue;
    scene::Transform& t = rb->gameObject().transform();
    t.setWorldPosition(s->position);
    t.setWorldRotation(s->rotation);
    rb->syncedPos_ = s->position;
    rb->syncedRot_ = s->rotation;
  }
}

void PhysicsSystem::dispatch(scene::Scene& scene) {
  for (const physics::ContactEvent& e : world_->drainEvents()) {
    scene::GameObject* a = scene.get(unpackObjectId(e.userDataA));
    scene::GameObject* b = scene.get(unpackObjectId(e.userDataB));
    if (a == nullptr || b == nullptr || a->markedForDestroy() || b->markedForDestroy()) continue;
    auto notify = [&](scene::GameObject& self, scene::GameObject& other, bool isA) {
      (void)isA;
      std::vector<scene::Component*> comps;
      self.forEachComponent([&](scene::Component& c) { comps.push_back(&c); });
      for (scene::Component* c : comps) {
        auto* listener = dynamic_cast<ContactListener*>(c);
        if (listener == nullptr || !c->activeAndEnabled()) continue;
        try {
          switch (e.type) {
            case physics::ContactEventType::ContactBegin: listener->onCollisionEnter(other, e); break;
            case physics::ContactEventType::ContactEnd: listener->onCollisionExit(other); break;
            case physics::ContactEventType::TriggerEnter: listener->onTriggerEnter(other); break;
            case physics::ContactEventType::TriggerExit: listener->onTriggerExit(other); break;
          }
        } catch (const std::exception& ex) {
          ++failures_;
          MINGOL_LOG_ERROR("physics", "contact callback of '{}' on '{}' threw: {}; component disabled", c->typeName(), self.name(), ex.what());
          c->setEnabled(false);
        } catch (...) {
          ++failures_;
          MINGOL_LOG_ERROR("physics", "contact callback of '{}' on '{}' threw; component disabled", c->typeName(), self.name());
          c->setEnabled(false);
        }
      }
    };
    notify(*a, *b, true);
    notify(*b, *a, false);
  }
}

void PhysicsSystem::fixedStep(scene::Scene& scene) {
  pushTransforms();
  world_->stepOnce();
  pullTransforms();
  dispatch(scene);
}

std::optional<PhysicsSystem::Hit> PhysicsSystem::raycast(scene::Scene& scene, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t mask) {
  auto h = world_->raycast(origin, direction, maxDistance, mask);
  if (!h) return std::nullopt;
  scene::GameObject* go = scene.get(unpackObjectId(h->userData));
  if (go == nullptr) return std::nullopt;
  return Hit{go, h->point, h->normal, h->distance};
}

std::vector<scene::GameObject*> PhysicsSystem::overlapSphere(scene::Scene& scene, Vec3 center, float radius, std::uint32_t mask) {
  std::vector<scene::GameObject*> out;
  for (physics::BodyId id : world_->overlapSphere(center, radius, mask)) {
    for (Rigidbody* rb : bodies_) {
      if (rb->body_ == id) {
        if (scene::GameObject* go = scene.get(rb->gameObject().id())) out.push_back(go);
        break;
      }
    }
  }
  return out;
}

}  // namespace mingol::runtime
