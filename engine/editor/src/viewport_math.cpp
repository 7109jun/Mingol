// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/viewport_math.hpp"

#include <algorithm>
#include <cmath>

namespace mingol::editor {

namespace {
constexpr float kMaxPitch = 1.553343F;  // 89 degrees

Vec3 dirFromAngles(float yaw, float pitch) {  // from target to eye
  return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}
}  // namespace

Vec3 EditorCamera::position() const { return target + dirFromAngles(yaw, pitch) * distance; }
Vec3 EditorCamera::forward() const { return -dirFromAngles(yaw, pitch); }
Vec3 EditorCamera::right() const { return normalize(cross(forward(), {0, 1, 0})); }

renderer::Camera EditorCamera::toCamera(float aspect) const {
  return renderer::Camera::lookAt(position(), target, {0, 1, 0}, fovY, aspect, nearZ, farZ);
}

void EditorCamera::orbit(float dx, float dy) {
  yaw -= dx * 0.01F;
  pitch = clamp(pitch + dy * 0.01F, -kMaxPitch, kMaxPitch);
}

void EditorCamera::look(float dx, float dy) {
  const Vec3 eye = position();
  yaw -= dx * 0.005F;
  pitch = clamp(pitch + dy * 0.005F, -kMaxPitch, kMaxPitch);
  // yaw/pitch describe the eye direction seen from the target; flipping the sign of pitch keeps "mouse up looks up".
  target = eye - dirFromAngles(yaw, pitch) * distance;
}

void EditorCamera::pan(float dx, float dy, float viewportHeight) {
  const float h = std::max(viewportHeight, 1.0F);
  const float worldPerPixel = 2.0F * distance * std::tan(fovY * 0.5F) / h;
  const Vec3 r = right();
  const Vec3 u = normalize(cross(r, forward()));
  target += (r * (-dx) + u * dy) * worldPerPixel;
}

void EditorCamera::zoom(float steps) {
  distance = clamp(distance * std::pow(0.9F, steps), 0.05F, 400.0F);
}

void EditorCamera::fly(Vec3 localDir, float seconds, float speed) {
  const Vec3 f = forward(), r = right();
  const Vec3 u = cross(r, f);
  target += (r * localDir.x + u * localDir.y + f * localDir.z) * (speed * seconds);
}

void EditorCamera::frame(Vec3 center, float radius) {
  target = center;
  const float r = std::max(radius, 0.1F);
  distance = clamp(r / std::sin(fovY * 0.5F) * 1.1F, 0.3F, 400.0F);
}

static bool unproject(const Mat4& inv, float nx, float ny, float nz, Vec3& out) {
  const Vec4 p = inv * Vec4(Vec3{nx, ny, nz}, 1.0F);
  if (std::fabs(p.w) < 1e-12F) return false;
  out = {p.x / p.w, p.y / p.w, p.z / p.w};
  return true;
}

Ray screenRay(const renderer::Camera& cam, Vec2 pixel, Vec2 size) {
  Ray r;
  r.origin = cam.position;
  Mat4 inv;
  if (!inverse(cam.projection * cam.view, inv) || size.x <= 0 || size.y <= 0) return r;
  const float nx = 2.0F * pixel.x / size.x - 1.0F;
  const float ny = 2.0F * pixel.y / size.y - 1.0F;
  Vec3 a, b;
  if (!unproject(inv, nx, ny, 0.0F, a) || !unproject(inv, nx, ny, 1.0F, b)) return r;
  r.origin = a;
  const Vec3 d = b - a;
  if (lengthSq(d) > 1e-20F) r.dir = normalize(d);
  return r;
}

std::optional<Vec2> projectToScreen(const renderer::Camera& cam, Vec3 world, Vec2 size) {
  const Vec4 c = (cam.projection * cam.view) * Vec4(world, 1.0F);
  if (c.w <= 1e-6F) return std::nullopt;
  const float nx = c.x / c.w, ny = c.y / c.w;
  return Vec2{(nx * 0.5F + 0.5F) * size.x, (ny * 0.5F + 0.5F) * size.y};
}

bool rayAabb(const Ray& ray, Vec3 bmin, Vec3 bmax, float& tOut) {
  float t0 = 0.0F, t1 = 1e30F;
  const float o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
  const float d[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
  const float lo[3] = {bmin.x, bmin.y, bmin.z};
  const float hi[3] = {bmax.x, bmax.y, bmax.z};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-12F) {
      if (o[i] < lo[i] || o[i] > hi[i]) return false;
      continue;
    }
    float a = (lo[i] - o[i]) / d[i], b = (hi[i] - o[i]) / d[i];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
    if (t0 > t1) return false;
  }
  tOut = t0;
  return true;
}

bool rayLocalAabb(const Ray& ray, const Mat4& world, const renderer::Aabb& local, float& tOut) {
  Mat4 inv;
  if (!inverse(world, inv)) return false;
  Ray lr;
  lr.origin = transformPoint(inv, ray.origin);
  const Vec3 d = transformDirection(inv, ray.dir);
  const float len = length(d);
  if (len < 1e-12F) return false;
  lr.dir = d / len;
  float t;
  if (!rayAabb(lr, local.min, local.max, t)) return false;
  // t is a distance in local units along lr.dir; convert through the hit point
  const Vec3 hit = transformPoint(world, lr.origin + lr.dir * t);
  tOut = dot(hit - ray.origin, ray.dir);
  return tOut >= 0.0F;
}

bool raySphere(const Ray& ray, Vec3 c, float radius, float& tOut) {
  const Vec3 oc = ray.origin - c;
  const float b = dot(oc, ray.dir);
  const float cc = dot(oc, oc) - radius * radius;
  const float disc = b * b - cc;
  if (disc < 0) return false;
  const float s = std::sqrt(disc);
  float t = -b - s;
  if (t < 0) t = -b + s;
  if (t < 0) return false;
  tOut = t;
  return true;
}

float closestOnAxis(const Ray& ray, Vec3 p, Vec3 a) {
  // closest points of two lines: p + a*s and ray.origin + ray.dir*t
  const Vec3 w = p - ray.origin;
  const float b = dot(a, ray.dir);
  const float d = dot(a, w), e = dot(ray.dir, w);
  const float denom = 1.0F - b * b;
  if (std::fabs(denom) < 1e-6F) return 0.0F;
  return (b * e - d) / denom;
}

float distancePointSegment2D(Vec2 p, Vec2 a, Vec2 b) {
  const Vec2 ab = b - a, ap = p - a;
  const float l2 = ab.x * ab.x + ab.y * ab.y;
  float t = l2 > 0 ? (ap.x * ab.x + ap.y * ab.y) / l2 : 0.0F;
  t = clamp(t, 0.0F, 1.0F);
  const Vec2 c = a + ab * t;
  const Vec2 d = p - c;
  return std::sqrt(d.x * d.x + d.y * d.y);
}

}  // namespace mingol::editor
