// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "mingol/core/math.hpp"

namespace mingol {

// Splits an affine matrix into translation, rotation and scale (columns are the scaled axes).
// Handles mirrored matrices by folding the reflection into the X scale. Shear is not
// representable and is discarded. A degenerate (zero-scale) matrix yields the identity rotation.
void decompose(const Mat4& m, Vec3& translation, Quat& rotation, Vec3& scale);

// Rotation matrix (upper 3x3 of m, assumed orthonormal) to a normalised quaternion.
Quat quatFromMatrix(const Mat4& m);

}  // namespace mingol
