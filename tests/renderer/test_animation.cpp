// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>

#include "mingol/renderer/animation.hpp"
#include "mingol/renderer/mesh.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::renderer;

namespace {

bool matNear(const Mat4& a, const Mat4& b, float eps = 1e-4F) {
  for (int i = 0; i < 16; ++i) {
    if (std::fabs(a.m[i] - b.m[i]) > eps) return false;
  }
  return true;
}

// root at origin, mid 1 unit up, tip 1 unit further up (an arm along +Y).
Skeleton makeArm() {
  std::vector<Joint> j(3);
  j[0].name = "root";
  j[1].name = "mid";
  j[1].parent = 0;
  j[1].bindLocal.translation = {0, 1, 0};
  j[2].name = "tip";
  j[2].parent = 1;
  j[2].bindLocal.translation = {0, 1, 0};
  auto s = Skeleton::create(std::move(j));
  return std::move(s).value();
}

}  // namespace

TEST(Skeleton, ValidatesStructure) {
  CHECK(!Skeleton::create({}).ok());

  std::vector<Joint> badParent(2);
  badParent[0].parent = 1;  // parent after child
  CHECK(!Skeleton::create(badParent).ok());

  std::vector<Joint> selfParent(1);
  selfParent[0].parent = 0;
  CHECK(!Skeleton::create(selfParent).ok());

  std::vector<Joint> dup(2);
  dup[0].name = "a";
  dup[1].name = "a";
  dup[1].parent = 0;
  const auto r = Skeleton::create(dup);
  CHECK(!r.ok());
  if (!r.ok()) CHECK(r.error().code == ErrorCode::AlreadyExists);

  std::vector<Joint> tooMany(kMaxJoints + 1);
  for (std::size_t i = 1; i < tooMany.size(); ++i) tooMany[i].parent = static_cast<int>(i) - 1;
  CHECK(!Skeleton::create(tooMany).ok());
  tooMany.pop_back();
  CHECK(Skeleton::create(tooMany).ok());

  std::vector<Joint> singular(1);
  singular[0].bindLocal.scale = {0, 1, 1};
  CHECK(!Skeleton::create(singular).ok());
}

TEST(Skeleton, FindJointAndBindPose) {
  const Skeleton s = makeArm();
  CHECK_EQ(s.jointCount(), 3u);
  CHECK_EQ(s.findJoint("tip").value(), 2u);
  CHECK(!s.findJoint("nope").has_value());
  CHECK_EQ(s.bindPose().size(), 3u);
}

TEST(Pose, BindPoseGivesIdentitySkinMatrices) {
  const Skeleton s = makeArm();
  const auto skin = computeSkinMatrices(s, s.bindPose());
  REQUIRE(skin.ok());
  for (const Mat4& m : skin.value()) CHECK(matNear(m, Mat4::identity()));
}

TEST(Pose, ParentRotationMovesChildren) {
  const Skeleton s = makeArm();
  auto pose = s.bindPose();
  // Rotate the middle joint 90 degrees about Z: the tip (1 unit above mid) swings to -X.
  pose[1].rotation = Quat::fromAxisAngle({0, 0, 1}, kPi * 0.5F);
  const auto skin = computeSkinMatrices(s, pose);
  REQUIRE(skin.ok());
  // A bind-pose point at the tip joint maps to: mid at (0,1,0) + rotate((0,1,0)) = (-1,1,0).
  const Vec3 tip = transformPoint(skin.value()[2], {0, 2, 0});
  CHECK(nearlyEqual(tip, Vec3{-1, 1, 0}, 1e-4F));
  // The root is untouched.
  CHECK(matNear(skin.value()[0], Mat4::identity()));
  // A point on the root-side of the mid joint stays put under the mid joint's own matrix.
  CHECK(nearlyEqual(transformPoint(skin.value()[1], {0, 1, 0}), Vec3{0, 1, 0}, 1e-4F));
}

TEST(Pose, PoseSizeMismatchIsAnError) {
  const Skeleton s = makeArm();
  std::vector<Transform> wrong(2);
  CHECK(!computeSkinMatrices(s, wrong).ok());
  CHECK(!computeGlobalPose(s, wrong).ok());
}

TEST(Pose, ExplicitInverseBindIsRespected) {
  std::vector<Joint> j(1);
  j[0].inverseBind = Mat4::translation({5, 0, 0});
  auto s = Skeleton::create(j);
  REQUIRE(s.ok());
  CHECK(matNear(s.value().inverseBind(0), Mat4::translation({5, 0, 0})));
}

TEST(Clip, ValidatesTracks) {
  Channel c;
  c.joint = 0;
  c.translation.times = {0.0F, 1.0F};
  c.translation.values = {{0, 0, 0}};  // length mismatch
  CHECK(!AnimationClip::create("bad", 1.0F, {c}).ok());

  c.translation.values = {{0, 0, 0}, {1, 0, 0}};
  c.translation.times = {0.5F, 0.5F};  // not strictly increasing
  CHECK(!AnimationClip::create("bad", 1.0F, {c}).ok());

  c.translation.times = {0.0F, std::nanf("")};
  CHECK(!AnimationClip::create("bad", 1.0F, {c}).ok());
  c.translation.times = {-1.0F, 0.0F};
  CHECK(!AnimationClip::create("bad", 1.0F, {c}).ok());

  c.translation.times = {0.0F, 1.0F};
  CHECK(!AnimationClip::create("zero", 0.0F, {c}).ok());
  CHECK(!AnimationClip::create("neg", -1.0F, {c}).ok());
  CHECK(AnimationClip::create("good", 1.0F, {c}).ok());
}

TEST(Clip, LinearInterpolationAndClamping) {
  const Skeleton s = makeArm();
  Channel c;
  c.joint = 1;
  c.translation.times = {0.0F, 2.0F};
  c.translation.values = {{0, 1, 0}, {4, 1, 0}};
  auto clip = AnimationClip::create("slide", 2.0F, {c});
  REQUIRE(clip.ok());

  auto at = [&](float t, bool loop) { return samplePose(s, clip.value(), t, loop).value()[1].translation; };
  CHECK(nearlyEqual(at(0.0F, false), Vec3{0, 1, 0}));
  CHECK(nearlyEqual(at(1.0F, false), Vec3{2, 1, 0}));
  CHECK(nearlyEqual(at(0.5F, false), Vec3{1, 1, 0}));
  CHECK(nearlyEqual(at(2.0F, false), Vec3{4, 1, 0}));
  CHECK(nearlyEqual(at(99.0F, false), Vec3{4, 1, 0}));  // clamped
  CHECK(nearlyEqual(at(-5.0F, false), Vec3{0, 1, 0}));
  CHECK(nearlyEqual(at(3.0F, true), Vec3{2, 1, 0}));    // 3 mod 2 = 1
  CHECK(nearlyEqual(at(-0.5F, true), Vec3{3, 1, 0}));   // wraps backwards: 1.5
  // Untouched joints stay at the bind pose.
  CHECK(nearlyEqual(samplePose(s, clip.value(), 1.0F, false).value()[2].translation, Vec3{0, 1, 0}));
}

TEST(Clip, RotationSlerpAndStep) {
  const Skeleton s = makeArm();
  Channel c;
  c.joint = 0;
  c.rotation.times = {0.0F, 1.0F};
  c.rotation.values = {Quat{}, Quat::fromAxisAngle({0, 0, 1}, kPi * 0.5F)};  // 90 degrees (180 is ambiguous)
  auto clip = AnimationClip::create("spin", 1.0F, {c});
  REQUIRE(clip.ok());
  const Quat mid = samplePose(s, clip.value(), 0.5F, false).value()[0].rotation;
  // Halfway through a 90 degree rotation is 45 degrees.
  CHECK(nearlyEqual(rotate(mid, {1, 0, 0}), Vec3{std::cos(kPi * 0.25F), std::sin(kPi * 0.25F), 0}, 1e-4F));
  CHECK_NEAR(dot(mid, mid), 1.0F, 1e-5);  // stays normalised

  c.rotation.interpolation = Interpolation::Step;
  auto step = AnimationClip::create("step", 1.0F, {c});
  const Quat held = samplePose(s, step.value(), 0.99F, false).value()[0].rotation;
  CHECK(nearlyEqual(rotate(held, {1, 0, 0}), Vec3{1, 0, 0}, 1e-5F));  // still the first key (identity)
}

TEST(Clip, SlerpTakesTheShortPath) {
  const Skeleton s = makeArm();
  Channel c;
  c.joint = 0;
  const Quat a = Quat::fromAxisAngle({0, 0, 1}, 0.1F);
  Quat b = Quat::fromAxisAngle({0, 0, 1}, 0.3F);
  b = {-b.x, -b.y, -b.z, -b.w};  // same rotation, opposite sign
  c.rotation.times = {0.0F, 1.0F};
  c.rotation.values = {a, b};
  auto clip = AnimationClip::create("neg", 1.0F, {c});
  const Quat m = samplePose(s, clip.value(), 0.5F, false).value()[0].rotation;
  const Vec3 r = rotate(m, {1, 0, 0});
  CHECK_NEAR(std::atan2(r.y, r.x), 0.2F, 1e-3);  // halfway between 0.1 and 0.3 rad, not the long way round
}

TEST(Clip, ChannelForUnknownJointIsAnError) {
  const Skeleton s = makeArm();
  Channel c;
  c.joint = 7;
  c.scale.times = {0.0F};
  c.scale.values = {{2, 2, 2}};
  auto clip = AnimationClip::create("oob", 1.0F, {c});
  REQUIRE(clip.ok());
  const auto r = samplePose(s, clip.value(), 0.0F, true);
  CHECK(!r.ok());
  if (!r.ok()) CHECK(r.error().code == ErrorCode::OutOfRange);
  CHECK(!samplePose(s, clip.value(), std::nanf(""), true).ok());
}

TEST(Clip, SingleKeyHoldsItsValue) {
  const Skeleton s = makeArm();
  Channel c;
  c.joint = 2;
  c.scale.times = {0.5F};
  c.scale.values = {{2, 3, 4}};
  auto clip = AnimationClip::create("one", 1.0F, {c});
  REQUIRE(clip.ok());
  for (float t : {0.0F, 0.5F, 0.9F}) {
    CHECK(nearlyEqual(samplePose(s, clip.value(), t, false).value()[2].scale, Vec3{2, 3, 4}));
  }
}

TEST(Blend, InterpolatesPoses) {
  const Skeleton s = makeArm();
  auto a = s.bindPose();
  auto b = s.bindPose();
  b[1].translation = {2, 1, 0};
  b[1].rotation = Quat::fromAxisAngle({0, 0, 1}, kPi * 0.5F);
  const auto mid = blendPoses(a, b, 0.5F);
  CHECK(nearlyEqual(mid[1].translation, Vec3{1, 1, 0}));
  CHECK(nearlyEqual(rotate(mid[1].rotation, {1, 0, 0}), Vec3{std::cos(kPi * 0.25F), std::sin(kPi * 0.25F), 0}, 1e-4F));
  CHECK(nearlyEqual(blendPoses(a, b, -3.0F)[1].translation, Vec3{0, 1, 0}));  // clamped
  CHECK(nearlyEqual(blendPoses(a, b, 3.0F)[1].translation, Vec3{2, 1, 0}));
}

// ---- Mesh data ------------------------------------------------------------------------------------

namespace {
// Every triangle's geometric normal (from winding) must agree with its vertex normals.
void checkWinding(const MeshData& m, const char* what) {
  for (std::size_t i = 0; i < m.indices.size(); i += 3) {
    const Vertex& a = m.vertices[m.indices[i]];
    const Vertex& b = m.vertices[m.indices[i + 1]];
    const Vertex& c = m.vertices[m.indices[i + 2]];
    const Vec3 n = cross(b.position - a.position, c.position - a.position);
    if (lengthSq(n) < 1e-12F) continue;  // degenerate triangle at a sphere pole
    const Vec3 avg = a.normal + b.normal + c.normal;
    CHECK_MSG(dot(n, avg) > 0.0F, std::string(what) + ": triangle " + std::to_string(i / 3) + " is wound clockwise");
  }
}
}  // namespace

TEST(Mesh, PrimitivesAreValidAndWoundCorrectly) {
  const MeshData plane = makePlane(4, 2);
  const MeshData box = makeBox({1, 2, 3});
  const MeshData sphere = makeUvSphere(1.5F, 24, 12);
  for (const MeshData* m : {&plane, &box, &sphere}) CHECK(m->validate().ok());
  checkWinding(plane, "plane");
  checkWinding(box, "box");
  checkWinding(sphere, "sphere");
  CHECK_EQ(plane.vertices.size(), 4u);
  CHECK_EQ(box.vertices.size(), 24u);
  CHECK_EQ(box.indices.size(), 36u);

  const Aabb bb = box.bounds();
  CHECK(nearlyEqual(bb.min, Vec3{-1, -2, -3}));
  CHECK(nearlyEqual(bb.max, Vec3{1, 2, 3}));
  const Aabb sb = sphere.bounds();
  CHECK_NEAR(sb.max.y, 1.5F, 1e-5);
  CHECK_NEAR(sb.min.x, -1.5F, 0.02);
  for (const Vertex& v : sphere.vertices) {
    CHECK_NEAR(length(v.position), 1.5F, 1e-4);
    CHECK_NEAR(length(v.normal), 1.0F, 1e-4);
  }
  for (const Vertex& v : box.vertices) {
    CHECK_NEAR(length(v.normal), 1.0F, 1e-6);
    CHECK_NEAR(dot(Vec3{v.tangent.x, v.tangent.y, v.tangent.z}, v.normal), 0.0F, 1e-6);  // tangent ⟂ normal
  }
}

TEST(Mesh, ValidationRejectsBrokenData) {
  MeshData good = makeBox({1, 1, 1});
  CHECK(good.validate().ok());

  MeshData m = good;
  m.indices.pop_back();
  CHECK(!m.validate().ok());  // not a multiple of 3
  m = good;
  m.indices[5] = 9999;
  CHECK(!m.validate().ok());  // out of range
  m = good;
  m.vertices[3].position.x = std::nanf("");
  CHECK(!m.validate().ok());
  m = good;
  m.vertices[2].normal = {0, 0, 0};
  CHECK(!m.validate().ok());
  CHECK(!MeshData{}.validate().ok());

  m = good;
  m.skin.resize(3);  // wrong count
  CHECK(!m.validate().ok());
  m.skin.assign(good.vertices.size(), SkinVertex{});
  CHECK(m.validate().ok());
  m.skin[4].weights[0] = 0.5F;  // sums to 0.5
  CHECK(!m.validate().ok());
  m.skin[4].weights[0] = 1.0F;
  m.skin[4].joints[0] = static_cast<float>(kMaxJoints);  // joint out of range
  CHECK(!m.validate().ok());
  m.skin[4].joints[0] = 1.5F;  // not an integer
  CHECK(!m.validate().ok());
  m.skin[4].joints[0] = 3.0F;
  m.skin[4].weights[1] = -0.5F;
  m.skin[4].weights[0] = 1.5F;  // negative weight even though the sum is 1
  CHECK(!m.validate().ok());
}
