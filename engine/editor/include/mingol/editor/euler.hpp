// SPDX-License-Identifier: GPL-2.0-only
// Euler angle helpers for the inspector. Convention: degrees, rotation = Ry(y) * Rx(x) * Rz(z)
// (Z applied first, then X, then Y), right-handed, +Y up.
#pragma once

#include "mingol/core/math.hpp"

namespace mingol::editor {

Quat fromEulerDegrees(Vec3 eulerDegrees);
// Returns angles in x in [-90, 90], y,z in (-180, 180]. At the +-90 degree gimbal pole z is 0.
Vec3 toEulerDegrees(Quat q);

}  // namespace mingol::editor
