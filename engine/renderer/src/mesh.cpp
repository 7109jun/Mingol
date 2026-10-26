// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/renderer/mesh.hpp"

#include <cmath>

#include "mingol/renderer/animation.hpp"

namespace mingol::renderer {

namespace {
bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

Result<void> MeshData::validate() const {
  if (vertices.empty()) return makeError(ErrorCode::InvalidArgument, "mesh has no vertices");
  if (indices.empty()) return makeError(ErrorCode::InvalidArgument, "mesh has no indices");
  if (indices.size() % 3 != 0) {
    return makeError(ErrorCode::InvalidArgument, "index count {} is not a multiple of 3", indices.size());
  }
  for (std::size_t i = 0; i < indices.size(); ++i) {
    if (indices[i] >= vertices.size()) {
      return makeError(ErrorCode::OutOfRange, "index {} (value {}) is outside {} vertices", i, indices[i], vertices.size());
    }
  }
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const Vertex& v = vertices[i];
    if (!finite3(v.position) || !finite3(v.normal) || !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) ||
        !std::isfinite(v.tangent.x) || !std::isfinite(v.tangent.y) || !std::isfinite(v.tangent.z) ||
        !std::isfinite(v.tangent.w)) {
      return makeError(ErrorCode::InvalidArgument, "vertex {} has a non-finite component", i);
    }
    if (lengthSq(v.normal) < 1e-8F) return makeError(ErrorCode::InvalidArgument, "vertex {} has a zero normal", i);
  }
  if (!skin.empty()) {
    if (skin.size() != vertices.size()) {
      return makeError(ErrorCode::InvalidArgument, "skin data has {} entries for {} vertices", skin.size(), vertices.size());
    }
    for (std::size_t i = 0; i < skin.size(); ++i) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) {
        const float j = skin[i].joints[k];
        const float w = skin[i].weights[k];
        if (!std::isfinite(j) || !std::isfinite(w) || w < 0.0F || j < 0.0F || j >= static_cast<float>(kMaxJoints) ||
            j != std::floor(j)) {
          return makeError(ErrorCode::InvalidArgument, "vertex {} has an invalid joint index or weight", i);
        }
        sum += w;
      }
      if (std::fabs(sum - 1.0F) > 1e-3F) {
        return makeError(ErrorCode::InvalidArgument, "vertex {} skin weights sum to {}, expected 1", i, sum);
      }
    }
  }
  return {};
}

Aabb MeshData::bounds() const {
  Aabb b;
  if (vertices.empty()) return b;
  b.min = b.max = vertices[0].position;
  for (const Vertex& v : vertices) {
    b.min = {std::fmin(b.min.x, v.position.x), std::fmin(b.min.y, v.position.y), std::fmin(b.min.z, v.position.z)};
    b.max = {std::fmax(b.max.x, v.position.x), std::fmax(b.max.y, v.position.y), std::fmax(b.max.z, v.position.z)};
  }
  return b;
}

MeshData makePlane(float width, float depth) {
  MeshData m;
  const float hx = width * 0.5F;
  const float hz = depth * 0.5F;
  // Seen from +Y looking down: +X right, +Z toward the viewer's bottom edge. uv (0,0) at -X,-Z.
  const Vec3 n{0, 1, 0};
  const Vec4 t{1, 0, 0, 1};
  m.vertices = {{{-hx, 0, -hz}, n, {0, 0}, t},
                {{-hx, 0, hz}, n, {0, 1}, t},
                {{hx, 0, hz}, n, {1, 1}, t},
                {{hx, 0, -hz}, n, {1, 0}, t}};
  m.indices = {0, 1, 2, 0, 2, 3};
  return m;
}

MeshData makeBox(Vec3 h) {
  MeshData m;
  struct Face {
    Vec3 normal, tangent, bitangent;  // bitangent = cross(normal, tangent)
  };
  const Face faces[6] = {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                         {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                         {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
  for (const Face& f : faces) {
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    const Vec3 c{f.normal.x * h.x, f.normal.y * h.y, f.normal.z * h.z};
    const Vec3 tx{f.tangent.x * h.x, f.tangent.y * h.y, f.tangent.z * h.z};
    const Vec3 by{f.bitangent.x * h.x, f.bitangent.y * h.y, f.bitangent.z * h.z};
    const Vec4 tan{f.tangent, 1.0F};
    m.vertices.push_back({c - tx - by, f.normal, {0, 1}, tan});
    m.vertices.push_back({c + tx - by, f.normal, {1, 1}, tan});
    m.vertices.push_back({c + tx + by, f.normal, {1, 0}, tan});
    m.vertices.push_back({c - tx + by, f.normal, {0, 0}, tan});
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
  }
  return m;
}

MeshData makeUvSphere(float radius, int segments, int rings) {
  MeshData m;
  if (segments < 3) segments = 3;
  if (rings < 2) rings = 2;
  for (int r = 0; r <= rings; ++r) {
    const float v = static_cast<float>(r) / static_cast<float>(rings);
    const float phi = v * kPi;  // 0 at the north pole
    for (int s = 0; s <= segments; ++s) {
      const float u = static_cast<float>(s) / static_cast<float>(segments);
      const float theta = u * 2.0F * kPi;
      const Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
      // d(position)/d(theta) direction, well defined away from the poles; fall back at the poles.
      Vec3 tan{-std::sin(theta), 0, std::cos(theta)};
      m.vertices.push_back({n * radius, n, {u, v}, {tan, 1.0F}});
    }
  }
  const std::uint32_t stride = static_cast<std::uint32_t>(segments) + 1;
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < segments; ++s) {
      const std::uint32_t a = static_cast<std::uint32_t>(r) * stride + static_cast<std::uint32_t>(s);
      const std::uint32_t b = a + stride;
      // CCW seen from outside.
      if (r != 0) {
        for (std::uint32_t i : {a, a + 1, b}) m.indices.push_back(i);
      }
      if (r != rings - 1) {
        for (std::uint32_t i : {a + 1, b + 1, b}) m.indices.push_back(i);
      }
    }
  }
  return m;
}

}  // namespace mingol::renderer
