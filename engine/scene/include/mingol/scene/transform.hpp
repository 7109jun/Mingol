// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "mingol/core/math.hpp"
#include "mingol/core/math_decompose.hpp"

namespace mingol::scene {

class GameObject;

// Position / rotation / scale relative to the parent, with a cached world matrix.
//
// Invariant: if a transform's cached world matrix is stale, so are those of all its descendants.
// Setting a local value therefore only walks down until it meets an already-stale node, and reading
// a world matrix recomputes only the stale chain above it.
class Transform {
 public:
  explicit Transform(GameObject& owner) : owner_(&owner) {}
  Transform(const Transform&) = delete;
  Transform& operator=(const Transform&) = delete;

  Vec3 localPosition() const noexcept { return position_; }
  Quat localRotation() const noexcept { return rotation_; }
  Vec3 localScale() const noexcept { return scale_; }
  void setLocalPosition(Vec3 p);
  void setLocalRotation(Quat q);  // normalised on set
  void setLocalScale(Vec3 s);
  void setLocal(Vec3 p, Quat q, Vec3 s);

  Mat4 localMatrix() const { return Mat4::trs(position_, rotation_, scale_); }
  const Mat4& worldMatrix() const;
  Mat4 inverseWorldMatrix() const;

  Vec3 worldPosition() const;
  Quat worldRotation() const;
  // Lossy under shear (children of rotated, non-uniformly scaled parents can have shear).
  Vec3 worldScale() const;
  void setWorldPosition(Vec3 p);
  void setWorldRotation(Quat q);

  // Local axes in world space: forward is -Z, right +X, up +Y (matching the camera convention).
  Vec3 forward() const;
  Vec3 right() const;
  Vec3 up() const;

  // Rotates so that forward() points at `target` (world space). No-op if target is at the position
  // or `up` is parallel to the view direction.
  void lookAt(Vec3 target, Vec3 up = {0, 1, 0});

  // Stale-flag access for tests and the scene (not needed by users).
  bool worldStale() const noexcept { return stale_; }

 private:
  friend class GameObject;
  friend class Scene;
  void markStale();
  void setLocalFromWorld(const Mat4& world);  // keeps the parent fixed

  GameObject* owner_;
  Vec3 position_{0, 0, 0};
  Quat rotation_{};
  Vec3 scale_{1, 1, 1};
  mutable Mat4 world_;
  mutable bool stale_ = true;
};

// Matrix decomposition lives in core; re-exported here for convenience.
using mingol::decompose;
using mingol::quatFromMatrix;

}  // namespace mingol::scene
