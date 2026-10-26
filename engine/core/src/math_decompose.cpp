// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>

#include "mingol/core/math.hpp"
#include "mingol/core/math_decompose.hpp"

namespace mingol {

Quat quatFromMatrix(const Mat4& m) {
  const float r00 = m.m[0], r10 = m.m[1], r20 = m.m[2];
  const float r01 = m.m[4], r11 = m.m[5], r21 = m.m[6];
  const float r02 = m.m[8], r12 = m.m[9], r22 = m.m[10];
  const float trace = r00 + r11 + r22;
  Quat q;
  if (trace > 0.0F) {
    const float s = std::sqrt(trace + 1.0F) * 2.0F;
    q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25F * s};
  } else if (r00 > r11 && r00 > r22) {
    const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
    q = {0.25F * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
  } else if (r11 > r22) {
    const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
    q = {(r01 + r10) / s, 0.25F * s, (r12 + r21) / s, (r02 - r20) / s};
  } else {
    const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
    q = {(r02 + r20) / s, (r12 + r21) / s, 0.25F * s, (r10 - r01) / s};
  }
  return normalize(q);
}

void decompose(const Mat4& m, Vec3& translation, Quat& rotation, Vec3& scale) {
  translation = {m.m[12], m.m[13], m.m[14]};
  Vec3 c0{m.m[0], m.m[1], m.m[2]};
  Vec3 c1{m.m[4], m.m[5], m.m[6]};
  Vec3 c2{m.m[8], m.m[9], m.m[10]};
  float sx = length(c0), sy = length(c1), sz = length(c2);
  if (dot(cross(c0, c1), c2) < 0.0F) sx = -sx;  // mirrored: fold the reflection into X
  scale = {sx, sy, sz};
  if (std::fabs(sx) < kEpsilon || sy < kEpsilon || sz < kEpsilon) {
    rotation = Quat{};  // degenerate: no meaningful orientation
    return;
  }
  Mat4 r;
  const Vec3 n0 = c0 / sx, n1 = c1 / sy, n2 = c2 / sz;
  r.m[0] = n0.x; r.m[1] = n0.y; r.m[2] = n0.z;
  r.m[4] = n1.x; r.m[5] = n1.y; r.m[6] = n1.z;
  r.m[8] = n2.x; r.m[9] = n2.y; r.m[10] = n2.z;
  rotation = quatFromMatrix(r);
}

}  // namespace mingol
