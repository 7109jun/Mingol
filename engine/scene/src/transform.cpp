// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/scene/transform.hpp"

#include <cmath>
#include <vector>

#include "mingol/scene/scene.hpp"

namespace mingol::scene {

void Transform::markStale() {
  if (stale_) return;  // invariant: descendants are already stale
  std::vector<Transform*> stack{this};
  while (!stack.empty()) {
    Transform* t = stack.back();
    stack.pop_back();
    if (t->stale_ && t != this) continue;
    t->stale_ = true;
    Scene& scene = t->owner_->scene();
    for (const GameObjectId id : t->owner_->children()) {
      if (GameObject* child = scene.get(id)) stack.push_back(&child->transform_);
    }
  }
}

void Transform::setLocalPosition(Vec3 p) {
  position_ = p;
  markStale();
}
void Transform::setLocalRotation(Quat q) {
  rotation_ = normalize(q);
  markStale();
}
void Transform::setLocalScale(Vec3 s) {
  scale_ = s;
  markStale();
}
void Transform::setLocal(Vec3 p, Quat q, Vec3 s) {
  position_ = p;
  rotation_ = normalize(q);
  scale_ = s;
  markStale();
}

const Mat4& Transform::worldMatrix() const {
  if (!stale_) return world_;
  // Collect the stale chain from this node up to (excluding) the first clean ancestor.
  std::vector<const Transform*> chain;
  const Transform* t = this;
  while (t != nullptr && t->stale_) {
    chain.push_back(t);
    const GameObject* p = t->owner_->parent();
    t = p != nullptr ? &p->transform() : nullptr;
  }
  Mat4 parentWorld = t != nullptr ? t->world_ : Mat4::identity();
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const Transform* node = *it;
    node->world_ = parentWorld * node->localMatrix();
    node->stale_ = false;
    parentWorld = node->world_;
  }
  return world_;
}

Mat4 Transform::inverseWorldMatrix() const {
  Mat4 inv;
  if (!inverse(worldMatrix(), inv)) return Mat4::identity();
  return inv;
}

Vec3 Transform::worldPosition() const {
  const Mat4& w = worldMatrix();
  return {w.m[12], w.m[13], w.m[14]};
}

Quat Transform::worldRotation() const {
  Vec3 t, s;
  Quat r;
  decompose(worldMatrix(), t, r, s);
  return r;
}

Vec3 Transform::worldScale() const {
  Vec3 t, s;
  Quat r;
  decompose(worldMatrix(), t, r, s);
  return s;
}

void Transform::setLocalFromWorld(const Mat4& world) {
  const GameObject* p = owner_->parent();
  Mat4 local = world;
  if (p != nullptr) local = p->transform().inverseWorldMatrix() * world;
  Vec3 t, s;
  Quat r;
  decompose(local, t, r, s);
  setLocal(t, r, s);
}

void Transform::setWorldPosition(Vec3 p) {
  const GameObject* parent = owner_->parent();
  setLocalPosition(parent != nullptr ? transformPoint(parent->transform().inverseWorldMatrix(), p) : p);
}

void Transform::setWorldRotation(Quat q) {
  const GameObject* parent = owner_->parent();
  setLocalRotation(parent != nullptr ? conjugate(parent->transform().worldRotation()) * normalize(q) : q);
}

Vec3 Transform::forward() const { return normalize(transformDirection(worldMatrix(), {0, 0, -1})); }
Vec3 Transform::right() const { return normalize(transformDirection(worldMatrix(), {1, 0, 0})); }
Vec3 Transform::up() const { return normalize(transformDirection(worldMatrix(), {0, 1, 0})); }

void Transform::lookAt(Vec3 target, Vec3 upHint) {
  const Vec3 toTarget = target - worldPosition();
  if (lengthSq(toTarget) < 1e-12F) return;
  const Vec3 f = normalize(toTarget);
  Vec3 x = cross(upHint, -f);
  if (lengthSq(x) < 1e-10F) {
    // The hint is parallel to the view direction (e.g. a light pointing straight down): pick the
    // world axis that is least aligned with it, so the result is still well defined.
    const Vec3 alt = std::fabs(f.z) < 0.9F ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    x = cross(alt, -f);
  }
  const Vec3 xn = normalize(x);
  const Vec3 zn = -f;
  const Vec3 yn = cross(zn, xn);
  Mat4 r;
  r.m[0] = xn.x; r.m[1] = xn.y; r.m[2] = xn.z;
  r.m[4] = yn.x; r.m[5] = yn.y; r.m[6] = yn.z;
  r.m[8] = zn.x; r.m[9] = zn.y; r.m[10] = zn.z;
  setWorldRotation(quatFromMatrix(r));
}

}  // namespace mingol::scene
