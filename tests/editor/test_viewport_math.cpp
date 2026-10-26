// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>

#include "mingol/editor/viewport_math.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::editor;

TEST(ViewportMath, ProjectAndRayRoundTrip) {
  EditorCamera c;
  const Vec2 size{800, 600};
  const auto cam = c.toCamera(size.x / size.y);
  const Vec3 pts[] = {c.target, c.target + Vec3{1, 0, 0}, c.target + Vec3{0, 1, 1}, {2, 0.5F, -1}};
  for (const Vec3& p : pts) {
    auto s = projectToScreen(cam, p, size);
    REQUIRE(s.has_value());
    const Ray r = screenRay(cam, *s, size);
    // the point must lie on the ray
    const Vec3 d = p - r.origin;
    const float along = dot(d, r.dir);
    const Vec3 off = d - r.dir * along;
    CHECK_MSG(length(off) < 1e-3F, "ray misses its own projection");
  }
  // target projects to the screen centre
  auto centre = projectToScreen(cam, c.target, size);
  REQUIRE(centre.has_value());
  CHECK_NEAR(centre->x, 400.0F, 0.5F);
  CHECK_NEAR(centre->y, 300.0F, 0.5F);
  // behind the camera
  CHECK(!projectToScreen(cam, c.position() - c.forward() * 5.0F, size).has_value());
}

TEST(ViewportMath, UpIsUpOnScreen) {
  EditorCamera c;
  const Vec2 size{400, 400};
  const auto cam = c.toCamera(1.0F);
  auto lo = projectToScreen(cam, c.target, size);
  auto hi = projectToScreen(cam, c.target + Vec3{0, 1, 0}, size);
  REQUIRE(lo.has_value());
  REQUIRE(hi.has_value());
  CHECK(hi->y < lo->y);  // smaller y = higher on screen
}

TEST(ViewportMath, AabbPicking) {
  Ray r{{0, 0, 10}, {0, 0, -1}};
  float t = 0;
  CHECK(rayAabb(r, {-1, -1, -1}, {1, 1, 1}, t));
  CHECK_NEAR(t, 9.0F, 1e-4F);
  CHECK(!rayAabb({{5, 0, 10}, {0, 0, -1}}, {-1, -1, -1}, {1, 1, 1}, t));
  CHECK(!rayAabb({{0, 0, 10}, {0, 0, 1}}, {-1, -1, -1}, {1, 1, 1}, t));  // pointing away
  CHECK(rayAabb({{0, 0, 0}, {0, 0, 1}}, {-1, -1, -1}, {1, 1, 1}, t));     // origin inside
  CHECK_NEAR(t, 0.0F, 1e-6F);

  renderer::Aabb box{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
  const Mat4 world = Mat4::trs({0, 0, 0}, Quat::fromAxisAngle({0, 1, 0}, 0.7F), {4, 1, 1});  // stretched along local x
  // a ray along -z through the origin hits at distance ~ 10 - half extent along z of the rotated box
  CHECK(rayLocalAabb({{0, 0, 10}, {0, 0, -1}}, world, box, t));
  CHECK(t > 7.0F && t < 10.0F);
  CHECK(!rayLocalAabb({{0, 5, 10}, {0, 0, -1}}, world, box, t));
  float s = 0;
  CHECK(raySphere({{0, 0, 10}, {0, 0, -1}}, {0, 0, 0}, 2.0F, s));
  CHECK_NEAR(s, 8.0F, 1e-4F);
  CHECK(!raySphere({{0, 3, 10}, {0, 0, -1}}, {0, 0, 0}, 2.0F, s));
}

TEST(ViewportMath, AxisDragParameter) {
  // axis along X through the origin; a ray from above hitting x = 3 should yield s = 3
  const Ray r{{3, 5, 0}, {0, -1, 0}};
  CHECK_NEAR(closestOnAxis(r, {0, 0, 0}, {1, 0, 0}), 3.0F, 1e-4F);
  // a ray parallel to the axis gives 0
  CHECK_NEAR(closestOnAxis({{0, 1, 0}, {1, 0, 0}}, {0, 0, 0}, {1, 0, 0}), 0.0F, 1e-6F);
  CHECK_NEAR(distancePointSegment2D({5, 5}, {0, 0}, {10, 0}), 5.0F, 1e-5F);
  CHECK_NEAR(distancePointSegment2D({-3, 4}, {0, 0}, {10, 0}), 5.0F, 1e-5F);
}

TEST(ViewportMath, CameraControls) {
  EditorCamera c;
  const Vec3 t0 = c.target;
  const float d0 = c.distance;
  c.orbit(100, 0);
  CHECK(length(c.target - t0) < 1e-6F);
  CHECK_NEAR(length(c.position() - c.target), d0, 1e-4F);
  c.orbit(0, 100000);
  CHECK(c.pitch <= 1.5534F && c.pitch >= -1.5534F);

  EditorCamera p;
  const Vec3 before = p.position();
  p.pan(50, 0, 600);
  const Vec3 moved = p.position() - before;
  CHECK(length(moved) > 0.01F);
  CHECK_NEAR(dot(normalize(moved), p.forward()), 0.0F, 1e-4F);  // pan stays in the view plane

  EditorCamera z;
  z.zoom(3);
  CHECK(z.distance < 8.0F);
  z.zoom(-100);
  CHECK(z.distance <= 400.0F);

  EditorCamera l;
  const Vec3 eye = l.position();
  l.look(0, -50);  // mouse up -> look up
  CHECK(l.forward().y > 0.0F || l.pitch < 0.4F);
  CHECK(length(l.position() - eye) < 1e-3F);  // eye stays put while looking

  EditorCamera f;
  const Vec3 e0 = f.position();
  f.fly({0, 0, 1}, 1.0F, 2.0F);
  CHECK_NEAR(length(f.position() - e0), 2.0F, 1e-4F);
  CHECK(dot(normalize(f.position() - e0), f.forward()) > 0.999F);

  EditorCamera fr;
  fr.frame({10, 0, 0}, 2.0F);
  const auto cam = fr.toCamera(1.0F);
  auto s = projectToScreen(cam, {10, 2, 0}, {100, 100});
  REQUIRE(s.has_value());
  CHECK(s->y >= 0.0F && s->y <= 100.0F);  // the sphere top fits in view
}
