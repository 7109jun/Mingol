// SPDX-License-Identifier: GPL-2.0-only
// Physics for the GameObject scene: Rigidbody + Collider components and the PhysicsSystem that
// steps the world and keeps Transforms in sync.
//
// Model: a Rigidbody owns one physics body; the Collider components on the SAME GameObject provide
// its shapes (a Collider without a Rigidbody does nothing, and says so in the log). A static
// collider is a Rigidbody of type Static. Colliders are read when the Rigidbody is enabled; to apply
// later changes, disable and re-enable the Rigidbody. World scale is baked into the shapes at that
// moment (non-uniform scale on spheres/capsules uses the largest relevant axis).
//
// Transform sync (every fixed step): moving the Transform yourself teleports the body (movement
// below 0.1 mm / 0.003 degrees is ignored as float noise); dynamic bodies write their simulated pose
// back to the Transform. Kinematic bodies follow their Transform.
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "mingol/physics/physics.hpp"
#include "mingol/scene/scene.hpp"

namespace mingol::runtime {

// Implement alongside Component to receive contact callbacks. `other` is the other GameObject.
class ContactListener {
 public:
  virtual ~ContactListener() = default;
  virtual void onCollisionEnter(scene::GameObject& /*other*/, const physics::ContactEvent& /*e*/) {}
  virtual void onCollisionExit(scene::GameObject& /*other*/) {}
  virtual void onTriggerEnter(scene::GameObject& /*other*/) {}
  virtual void onTriggerExit(scene::GameObject& /*other*/) {}
};

class Collider final : public scene::Component {
 public:
  const char* typeName() const override { return "Collider"; }

  static Collider* addBox(scene::GameObject& go, Vec3 halfExtents, Vec3 center = {0, 0, 0});
  static Collider* addSphere(scene::GameObject& go, float radius, Vec3 center = {0, 0, 0});
  static Collider* addCapsule(scene::GameObject& go, float radius, float height, Vec3 center = {0, 0, 0});
  static Collider* addMesh(scene::GameObject& go, std::shared_ptr<const physics::MeshShapeData> mesh);

  physics::ShapeDesc shape;  // offset/rotation are relative to the GameObject
};

class Rigidbody final : public scene::Component {
 public:
  const char* typeName() const override { return "Rigidbody"; }

  // The body is created when the component first starts (before the first fixed step), so these
  // can be configured right after addComponent. Later changes need a disable/enable to rebuild it.
  physics::BodyType type = physics::BodyType::Dynamic;
  float mass = 1.0F;
  float friction = 0.5F;
  float restitution = 0.0F;
  float linearDamping = 0.0F;
  float angularDamping = 0.05F;
  bool isTrigger = false;
  bool allowSleep = true;
  std::uint32_t layer = 1;
  std::uint32_t mask = 0xFFFFFFFFu;
  bool lockRotationX = false, lockRotationY = false, lockRotationZ = false;
  bool lockPositionX = false, lockPositionY = false, lockPositionZ = false;

  bool hasBody() const noexcept { return body_.valid(); }
  physics::BodyId bodyId() const noexcept { return body_; }

  // Runtime control. All fail with NotFound while there is no body (disabled or no collider).
  Result<void> setVelocity(Vec3 v);
  Result<void> setAngularVelocity(Vec3 v);
  Result<void> addForce(Vec3 f);
  Result<void> addImpulse(Vec3 impulse);
  Result<void> addTorqueImpulse(Vec3 impulse);
  std::optional<physics::BodyState> state() const;

 protected:
  void onEnable() override;
  void onStart() override;
  void onDisable() override;

 private:
  void createBody();
  bool started_ = false;
  friend class PhysicsSystem;
  Result<void> need() const;
  physics::BodyId body_;
  Vec3 syncedPos_{0, 0, 0};
  Quat syncedRot_{};
};

class PhysicsSystem {
 public:
  explicit PhysicsSystem(std::unique_ptr<physics::World> world) : world_(std::move(world)) {}
  static Result<std::unique_ptr<PhysicsSystem>> create(const physics::WorldDesc& desc = {});

  physics::World& world() { return *world_; }

  // One fixed step: push Transform edits into the world, simulate, write poses back, deliver
  // contact callbacks. Callback exceptions are logged and disable the offending component.
  void fixedStep(scene::Scene& scene);

  struct Hit {
    scene::GameObject* object = nullptr;
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};
    float distance = 0.0F;
  };
  std::optional<Hit> raycast(scene::Scene& scene, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t mask = 0xFFFFFFFFu);
  std::vector<scene::GameObject*> overlapSphere(scene::Scene& scene, Vec3 center, float radius, std::uint32_t mask = 0xFFFFFFFFu);

  std::size_t bodyCount() const noexcept { return world_->bodyCount(); }
  std::uint64_t callbackFailures() const noexcept { return failures_; }

 private:
  friend class Rigidbody;
  void add(Rigidbody* rb) { bodies_.push_back(rb); }
  void remove(Rigidbody* rb);
  void pushTransforms();
  void pullTransforms();
  void dispatch(scene::Scene& scene);

  std::unique_ptr<physics::World> world_;
  std::vector<Rigidbody*> bodies_;
  std::uint64_t failures_ = 0;
};

// GameObjectId <-> the opaque userData carried through the physics world.
std::uint64_t packObjectId(scene::GameObjectId id) noexcept;
scene::GameObjectId unpackObjectId(std::uint64_t v) noexcept;

}  // namespace mingol::runtime
