// SPDX-License-Identifier: GPL-2.0-only
// Pure math of the editor viewport: orbit/fly camera, screen <-> world rays, picking and gizmo helpers.
// Screen space: pixels, origin top-left (matches Mat4::perspective, which flips Y for Vulkan).
#pragma once

#include <optional>

#include "mingol/core/math.hpp"
#include "mingol/renderer/mesh.hpp"
#include "mingol/renderer/scene.hpp"

namespace mingol::editor {

struct Ray {
  Vec3 origin{0, 0, 0};
  Vec3 dir{0, 0, -1};  // normalised
};

struct EditorCamera {
  Vec3 target{0, 1, 0};
  float yaw = 0.6F;    // radians around +Y
  float pitch = 0.4F;  // radians above the horizon, limited to +-89 degrees
  float distance = 8.0F;
  float fovY = 1.0471976F;
  float nearZ = 0.05F;
  float farZ = 500.0F;

  Vec3 position() const;
  Vec3 forward() const;  // from the eye towards the target
  Vec3 right() const;
  renderer::Camera toCamera(float aspect) const;

  void orbit(float dxPixels, float dyPixels);                      // around the target
  void look(float dxPixels, float dyPixels);                       // fly look: the eye stays, the target moves
  void pan(float dxPixels, float dyPixels, float viewportHeight);  // moves target and eye in the view plane
  void zoom(float wheelSteps);                                     // dolly towards/away from the target
  void fly(Vec3 localDir, float seconds, float speed);             // x right, y up, z forward
  void frame(Vec3 center, float radius);                           // makes a sphere fit the view
};

// Ray through a pixel of a viewport of `size` pixels.
Ray screenRay(const renderer::Camera& cam, Vec2 pixel, Vec2 size);
// Pixel position of a world point; nullopt when it is behind the camera.
std::optional<Vec2> projectToScreen(const renderer::Camera& cam, Vec3 world, Vec2 size);

// Slab test; `tOut` is the distance along the (normalised) ray to the entry point (0 when the origin is inside).
bool rayAabb(const Ray& ray, Vec3 bmin, Vec3 bmax, float& tOut);
// Ray vs an AABB given in the local space of `world` (non-uniform scale supported). tOut is in world units.
bool rayLocalAabb(const Ray& ray, const Mat4& world, const renderer::Aabb& local, float& tOut);
bool raySphere(const Ray& ray, Vec3 center, float radius, float& tOut);

// Parameter s so that axisOrigin + axisDir * s is the point of the axis line closest to the ray (axisDir normalised).
// Returns 0 when the ray is parallel to the axis.
float closestOnAxis(const Ray& ray, Vec3 axisOrigin, Vec3 axisDir);
float distancePointSegment2D(Vec2 p, Vec2 a, Vec2 b);

}  // namespace mingol::editor
