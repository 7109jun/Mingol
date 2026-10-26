// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/euler.hpp"

#include <algorithm>
#include <cmath>

namespace mingol::editor {

Quat fromEulerDegrees(Vec3 e) {
  const Quat qy = Quat::fromAxisAngle({0, 1, 0}, radians(e.y));
  const Quat qx = Quat::fromAxisAngle({1, 0, 0}, radians(e.x));
  const Quat qz = Quat::fromAxisAngle({0, 0, 1}, radians(e.z));
  return normalize(qy * qx * qz);
}

Vec3 toEulerDegrees(Quat q) {
  q = normalize(q);
  const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  // rotation matrix elements R[row][col]
  const float r00 = 1 - 2 * (yy + zz), r02 = 2 * (q.x * q.z + q.w * q.y);
  const float r10 = 2 * (q.x * q.y + q.w * q.z), r11 = 1 - 2 * (xx + zz), r12 = 2 * (q.y * q.z - q.w * q.x);
  const float r20 = 2 * (q.x * q.z - q.w * q.y), r22 = 1 - 2 * (xx + yy);
  const float sx = std::clamp(-r12, -1.0F, 1.0F);
  float x = std::asin(sx), y, z;
  if (std::fabs(r12) < 0.99999F) {
    y = std::atan2(r02, r22);
    z = std::atan2(r10, r11);
  } else {
    y = std::atan2(-r20, r00);
    z = 0;
  }
  return {degrees(x), degrees(y), degrees(z)};
}

}  // namespace mingol::editor
