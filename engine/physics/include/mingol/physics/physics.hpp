// SPDX-License-Identifier: GPL-2.0-only
// Rigid-body physics behind an engine-owned interface. The implementation uses Bullet Physics
// (zlib licence), but no Bullet type appears here: the backend can be swapped without touching
// callers. 2D physics is the same world with every body constrained to the XY plane.
//
// Threading: a World is single-threaded; use it from the thread that steps the simulation.
// Units: metres, kilograms, seconds, radians. Y is up. Rotations are unit quaternions.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"

namespace mingol::physics {

enum class BodyType : std::uint8_t {
  Static,     // never moves; infinite mass
  Dynamic,    // simulated; needs mass > 0
  Kinematic,  // moved by the game (setTransform); pushes dynamic bodies, is not pushed
};

enum class ShapeType : std::uint8_t { Box, Sphere, Capsule, TriangleMesh };

// Triangle soup for static level geometry. Copied into the body on creation.
struct MeshShapeData {
  std::vector<Vec3> vertices;
  std::vector<std::uint32_t> indices;  // triangle list
};

struct ShapeDesc {
  ShapeType type = ShapeType::Box;
  Vec3 halfExtents{0.5F, 0.5F, 0.5F};  // Box
  float radius = 0.5F;                 // Sphere, Capsule
  float height = 1.0F;                 // Capsule: length of the cylindrical part (Y axis)
  Vec3 offset{0, 0, 0};                // shape position relative to the body
  Quat rotation{};                     // shape orientation relative to the body
  std::shared_ptr<const MeshShapeData> mesh;  // TriangleMesh (static bodies only)
};

struct BodyDesc {
  BodyType type = BodyType::Dynamic;
  std::vector<ShapeDesc> shapes;  // one or more; several form a compound
  float mass = 1.0F;              // Dynamic only
  Vec3 position{0, 0, 0};
  Quat rotation{};
  float friction = 0.5F;          // >= 0
  float restitution = 0.0F;       // 0..1+
  float linearDamping = 0.0F;     // 0..1
  float angularDamping = 0.05F;   // 0..1
  bool isTrigger = false;         // detects overlaps, produces no collision response
  bool allowSleep = true;
  std::uint32_t layer = 1;        // this body's collision group bit(s)
  std::uint32_t mask = 0xFFFFFFFFu;  // groups it collides with; a pair collides when each side's mask accepts the other's layer
  bool lockRotationX = false, lockRotationY = false, lockRotationZ = false;
  bool lockPositionX = false, lockPositionY = false, lockPositionZ = false;
  std::uint64_t userData = 0;     // opaque, returned in events and queries
};

struct BodyId {
  std::uint32_t index = 0;  // slot + 1; 0 = none
  std::uint32_t generation = 0;
  bool valid() const noexcept { return index != 0; }
  friend bool operator==(BodyId a, BodyId b) { return a.index == b.index && a.generation == b.generation; }
  friend bool operator<(BodyId a, BodyId b) { return a.index != b.index ? a.index < b.index : a.generation < b.generation; }
};

struct WorldDesc {
  Vec3 gravity{0, -9.81F, 0};
  float fixedStep = 1.0F / 60.0F;  // simulation step; step(dt) runs whole steps of this size
  int maxSubSteps = 8;             // upper bound per step(dt) call; leftover time is dropped (no spiral of death)
  bool planar2D = false;           // constrain every body to the XY plane (translation in X/Y, rotation about Z)
};

enum class ContactEventType : std::uint8_t {
  ContactBegin,  // two solid bodies started touching
  ContactEnd,
  TriggerEnter,  // a body started overlapping a trigger
  TriggerExit,
};

struct ContactEvent {
  ContactEventType type = ContactEventType::ContactBegin;
  BodyId a, b;                 // a < b; for trigger events `a` is the trigger
  std::uint64_t userDataA = 0, userDataB = 0;
  Vec3 point{0, 0, 0};         // world-space contact point (Begin / Enter; zero for End / Exit)
  Vec3 normal{0, 0, 0};        // from a towards b
};

struct RayHit {
  BodyId body;
  std::uint64_t userData = 0;
  Vec3 point{0, 0, 0};
  Vec3 normal{0, 1, 0};
  float distance = 0.0F;
};

struct BodyState {
  Vec3 position{0, 0, 0};
  Quat rotation{};
  Vec3 linearVelocity{0, 0, 0};
  Vec3 angularVelocity{0, 0, 0};
  bool sleeping = false;
};

class World {
 public:
  static Result<std::unique_ptr<World>> create(const WorldDesc& desc = {});
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // Fails for an empty shape list, non-finite or non-positive sizes, a dynamic body without mass,
  // a non-static triangle mesh, or an empty/out-of-range mesh.
  Result<BodyId> createBody(const BodyDesc& desc);
  // Removing an unknown/stale id is a harmless no-op that returns false.
  bool destroyBody(BodyId id);
  bool exists(BodyId id) const noexcept;
  std::size_t bodyCount() const noexcept;

  // Advances the simulation by `dt` seconds in whole `fixedStep`s; returns the steps taken.
  int step(float dt);
  // Exactly one fixed step.
  void stepOnce();

  std::optional<BodyState> state(BodyId id) const;
  // Teleports the body (zeroes nothing: velocities are kept). For kinematic bodies this is how they move.
  Result<void> setTransform(BodyId id, Vec3 position, Quat rotation);
  Result<void> setLinearVelocity(BodyId id, Vec3 v);
  Result<void> setAngularVelocity(BodyId id, Vec3 v);
  Result<void> applyForce(BodyId id, Vec3 force);              // until the next step ends
  Result<void> applyImpulse(BodyId id, Vec3 impulse);          // instantaneous, at the centre of mass
  Result<void> applyTorqueImpulse(BodyId id, Vec3 torqueImpulse);
  Result<void> setGravity(Vec3 g);
  Vec3 gravity() const noexcept;
  float fixedStep() const noexcept;

  // Closest solid (non-trigger) body hit by the segment origin -> origin + direction * maxDistance.
  // `mask` selects which layers are hit. direction need not be normalised but must be non-zero.
  std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t mask = 0xFFFFFFFFu) const;
  // All bodies (solid and trigger) whose shape overlaps a sphere.
  std::vector<BodyId> overlapSphere(Vec3 center, float radius, std::uint32_t mask = 0xFFFFFFFFu) const;

  // Events produced since the last call, in the order they were detected.
  std::vector<ContactEvent> drainEvents();

 private:
  World();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mingol::physics
