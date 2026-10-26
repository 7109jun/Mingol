// SPDX-License-Identifier: GPL-2.0-only
// What the renderer draws: a flat, immutable description of one frame. The GameObject/Component
// scene (a later module) builds one of these each frame; the renderer knows nothing about it.
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "mingol/core/math.hpp"
#include "mingol/renderer/mesh.hpp"

namespace mingol::renderer {

class Mesh;
class Material;

struct Camera {
  Mat4 view;
  Mat4 projection;
  Vec3 position{0, 0, 0};  // world-space eye position (specular needs it)

  static Camera lookAt(Vec3 eye, Vec3 target, Vec3 up, float fovYRadians, float aspect, float nearZ, float farZ) {
    return {Mat4::lookAt(eye, target, up), Mat4::perspective(fovYRadians, aspect, nearZ, farZ), eye};
  }
};

enum class LightType : std::uint8_t { Directional, Point, Spot };

struct Light {
  LightType type = LightType::Directional;
  Vec3 color{1, 1, 1};
  // Radiance multiplier: a surface facing the light with a white diffuse albedo of 1 shows
  // intensity / pi * cos(theta) before tone mapping. (intensity = pi gives albedo * cos(theta).)
  float intensity = 1.0F;
  Vec3 position{0, 0, 0};        // point/spot
  Vec3 direction{0, -1, 0};      // direction the light travels (directional/spot)
  float range = 10.0F;           // point/spot: contribution fades to zero here
  float innerConeRadians = 0.4F; // spot
  float outerConeRadians = 0.6F; // spot
  bool castShadow = false;       // directional lights only
};

struct DrawItem {
  std::shared_ptr<const Mesh> mesh;
  std::shared_ptr<const Material> material;
  Mat4 transform;  // model -> world
  // Joint matrices from computeSkinMatrices(), required iff the mesh is skinned.
  std::vector<Mat4> skinMatrices;
  bool castShadow = true;
};

struct RenderScene {
  Camera camera;
  Vec3 ambient{0.03F, 0.03F, 0.03F};     // multiplies diffuse colour
  float clearColor[4] = {0.0F, 0.0F, 0.0F, 1.0F};
  std::vector<Light> lights;             // at most one directional + kMaxPunctualLights point/spot
  std::vector<DrawItem> items;
  // The shadow map covers this sphere. Without it, the bounding sphere of all shadow-casting items
  // is used (skinned meshes contribute their bind-pose bounds, so set this when they move far).
  struct ShadowFocus {
    Vec3 center;
    float radius;
  };
  std::optional<ShadowFocus> shadowFocus;
};

inline constexpr int kMaxPunctualLights = 8;

}  // namespace mingol::renderer
