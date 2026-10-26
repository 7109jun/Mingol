// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/math.hpp"
#include "mingol_test.hpp"

using namespace mingol;

namespace {

bool matNear(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
  for (int i = 0; i < 16; ++i)
    if (!nearlyEqual(a.m[i], b.m[i], eps)) return false;
  return true;
}

}  // namespace

TEST(Math, VectorBasics) {
  CHECK_NEAR(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}), 32.0, 1e-6);
  CHECK(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == (Vec3{0, 0, 1}));
  CHECK_NEAR(length(Vec3{3, 4, 0}), 5.0, 1e-6);
  CHECK_NEAR(length(normalize(Vec3{10, -2, 7})), 1.0, 1e-5);
  CHECK(normalize(Vec3{}) == Vec3{});  // no NaNs for the zero vector
}

TEST(Math, QuaternionRotation) {
  const Quat q = Quat::fromAxisAngle({0, 1, 0}, radians(90.0f));
  const Vec3 r = rotate(q, {1, 0, 0});
  CHECK(nearlyEqual(r, Vec3{0, 0, -1}));  // +X rotated 90deg about +Y (right-handed) -> -Z
  const Vec3 back = rotate(conjugate(q), r);
  CHECK(nearlyEqual(back, Vec3{1, 0, 0}));
}

TEST(Math, QuaternionMatchesMatrix) {
  const Quat q = normalize(Quat::fromAxisAngle({1, 2, 3}, 0.7f));
  const Vec3 v{0.3f, -1.2f, 2.5f};
  CHECK(nearlyEqual(rotate(q, v), transformDirection(Mat4::rotation(q), v), 1e-4f));
}

TEST(Math, QuaternionComposition) {
  const Quat a = Quat::fromAxisAngle({0, 0, 1}, radians(30.0f));
  const Quat b = Quat::fromAxisAngle({0, 0, 1}, radians(60.0f));
  const Vec3 r = rotate(a * b, {1, 0, 0});
  CHECK(nearlyEqual(r, Vec3{0, 1, 0}, 1e-5f));  // 90 degrees total about Z
}

TEST(Math, Slerp) {
  const Quat a;
  const Quat b = Quat::fromAxisAngle({0, 1, 0}, radians(90.0f));
  const Quat mid = slerp(a, b, 0.5f);
  const Quat expect = Quat::fromAxisAngle({0, 1, 0}, radians(45.0f));
  CHECK_NEAR(std::fabs(dot(mid, expect)), 1.0, 1e-5);
  CHECK_NEAR(dot(slerp(a, b, 0.0f), a), 1.0, 1e-5);
}

TEST(Math, MatrixInverse) {
  const Mat4 m = Mat4::trs({1, 2, 3}, normalize(Quat::fromAxisAngle({0.3f, 1, 0.2f}, 1.1f)), {2, 3, 0.5f});
  Mat4 inv;
  REQUIRE(inverse(m, inv));
  CHECK(matNear(m * inv, Mat4::identity()));
  CHECK(matNear(inv * m, Mat4::identity()));
}

TEST(Math, SingularMatrixInverseFails) {
  Mat4 m = Mat4::scale({1, 0, 1});
  Mat4 out = Mat4::translation({9, 9, 9});
  CHECK(!inverse(m, out));
  CHECK_NEAR(out.m[12], 9.0, 0.0);  // untouched on failure
}

TEST(Math, TrsOrder) {
  // Scale first, then rotate, then translate.
  const Mat4 m = Mat4::trs({10, 0, 0}, Quat::fromAxisAngle({0, 0, 1}, radians(90.0f)), {2, 2, 2});
  CHECK(nearlyEqual(transformPoint(m, {1, 0, 0}), Vec3{10, 2, 0}, 1e-5f));
}

TEST(Math, LookAtMapsTargetToNegativeZ) {
  const Mat4 view = Mat4::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  CHECK(nearlyEqual(transformPoint(view, {0, 0, 0}), Vec3{0, 0, -5}));
  CHECK(nearlyEqual(transformPoint(view, {0, 0, 5}), Vec3{0, 0, 0}));
  CHECK(nearlyEqual(transformPoint(view, {1, 0, 0}), Vec3{1, 0, -5}));
}

TEST(Math, PerspectiveVulkanClipSpace) {
  const float n = 0.1f, f = 100.0f;
  const Mat4 p = Mat4::perspective(radians(60.0f), 16.0f / 9.0f, n, f);
  auto ndc = [&](Vec3 v) {
    const Vec4 c = p * Vec4(v, 1.0f);
    return Vec3{c.x / c.w, c.y / c.w, c.z / c.w};
  };
  CHECK_NEAR(ndc({0, 0, -n}).z, 0.0, 1e-4);    // near plane -> depth 0
  CHECK_NEAR(ndc({0, 0, -f}).z, 1.0, 1e-4);    // far plane  -> depth 1
  CHECK(ndc({0, 1, -5}).y < 0.0f);              // +Y world is -Y in Vulkan NDC
}

TEST(Math, OrthographicVulkanClipSpace) {
  const Mat4 o = Mat4::orthographic(-2, 2, -1, 1, 0.5f, 10.0f);
  const Vec3 nearPt = transformPoint(o, {2, 1, -0.5f});
  CHECK_NEAR(nearPt.x, 1.0, 1e-5);
  CHECK_NEAR(nearPt.y, -1.0, 1e-5);  // top maps to -1 (Y flipped)
  CHECK_NEAR(nearPt.z, 0.0, 1e-5);
  CHECK_NEAR(transformPoint(o, {0, 0, -10.0f}).z, 1.0, 1e-5);
}

TEST(Math, Transpose) {
  Mat4 m;
  for (int i = 0; i < 16; ++i) m.m[i] = static_cast<float>(i);
  CHECK(matNear(transpose(transpose(m)), m));
  CHECK_NEAR(transpose(m).at(0, 1), m.at(1, 0), 0.0);
}
