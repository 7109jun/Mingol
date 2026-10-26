// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"

namespace mingol::renderer {

// Interleaved static vertex (48 bytes): the layout the forward shaders read.
struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
  Vec4 tangent;  // xyz direction, w = bitangent sign (+1/-1)
};
static_assert(sizeof(Vertex) == 48);

// Second vertex stream for skinned meshes (32 bytes). Joint indices are stored as floats because
// the RHI has no integer vertex formats; exact for indices below 2^24.
struct SkinVertex {
  float joints[4] = {0, 0, 0, 0};
  float weights[4] = {1, 0, 0, 0};
};
static_assert(sizeof(SkinVertex) == 32);

struct Aabb {
  Vec3 min{0, 0, 0};
  Vec3 max{0, 0, 0};
  Vec3 center() const { return (min + max) * 0.5F; }
  float radius() const { return length(max - min) * 0.5F; }
};

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;  // triangle list
  std::vector<SkinVertex> skin;        // empty (static) or one entry per vertex

  // Checks: non-empty, indices multiple of 3 and in range, all floats finite, normals non-zero,
  // skin size matches, joint indices < kMaxJoints, weights non-negative and summing to ~1.
  Result<void> validate() const;
  Aabb bounds() const;
};

// All primitives have normals, UVs and tangents, wound counter-clockwise seen from outside.
MeshData makePlane(float width, float depth);          // in XZ, normal +Y, centred at the origin
MeshData makeBox(Vec3 halfExtents);                    // 24 vertices (flat normals)
MeshData makeUvSphere(float radius, int segments, int rings);

}  // namespace mingol::renderer
