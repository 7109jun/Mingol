// SPDX-License-Identifier: GPL-2.0-only
// Built-in components that describe what to draw, and the bridge to the renderer.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/renderer/animation.hpp"
#include "mingol/renderer/renderer.hpp"
#include "mingol/renderer/scene.hpp"
#include "mingol/scene/scene.hpp"

namespace mingol::scene {

// Draws a mesh with a material at the object's world transform. If the mesh is skinned the object
// must also have an Animator (it supplies the joint matrices).
class MeshRenderer final : public Component {
 public:
  const char* typeName() const override { return "MeshRenderer"; }
  std::shared_ptr<const renderer::Mesh> mesh;
  std::shared_ptr<const renderer::Material> material;
  bool castShadow = true;

 private:
  friend Result<renderer::RenderScene> buildRenderScene(Scene&, float);
  friend Result<renderer::RenderScene> buildRenderScene(Scene&, const renderer::Camera&);
  bool warned_ = false;
};

// Plays skeletal animation clips with cross-fading. Advances in onUpdate (scaled by speed).
class Animator final : public Component {
 public:
  const char* typeName() const override { return "Animator"; }

  // Fails when the skeleton has no joints.
  Result<void> setSkeleton(std::shared_ptr<const renderer::Skeleton> skeleton);
  // Fails when the name is empty/duplicated or a channel refers to a joint the skeleton lacks.
  Result<void> addClip(std::shared_ptr<const renderer::AnimationClip> clip);
  // Starts a clip. fadeSeconds > 0 cross-fades from whatever was playing. Fails for an unknown clip.
  Result<void> play(const std::string& clipName, bool loop = true, float fadeSeconds = 0.0F);
  void stop();

  float speed = 1.0F;
  bool playing() const noexcept { return !current_.clip.empty(); }
  bool looping() const noexcept { return current_.loop; }
  // Clips added so far, by clip name.
  const std::map<std::string, std::shared_ptr<const renderer::AnimationClip>>& clips() const noexcept { return clips_; }
  const std::string& currentClip() const noexcept { return current_.clip; }
  float currentTime() const noexcept { return current_.time; }
  bool fading() const noexcept { return fadeDuration_ > 0.0F; }
  float fadeProgress() const noexcept { return fadeDuration_ > 0.0F ? fadeElapsed_ / fadeDuration_ : 1.0F; }
  // True once a non-looping clip reached its end.
  bool finished() const noexcept { return finished_; }

  // Advances time by dt seconds (also what onUpdate does).
  void advance(float dt);
  // Joint matrices for the present state (bind pose when nothing plays).
  Result<std::vector<Mat4>> skinMatrices() const;
  std::shared_ptr<const renderer::Skeleton> skeleton() const { return skeleton_; }

 protected:
  void onUpdate(float dt) override { advance(dt); }

 private:
  struct State {
    std::string clip;
    float time = 0.0F;
    bool loop = true;
  };
  Result<std::vector<renderer::Transform>> pose(const State& s) const;

  std::shared_ptr<const renderer::Skeleton> skeleton_;
  std::map<std::string, std::shared_ptr<const renderer::AnimationClip>> clips_;
  State current_;
  State previous_;
  float fadeDuration_ = 0.0F;
  float fadeElapsed_ = 0.0F;
  bool finished_ = false;
};

// The view a frame is rendered from. The enabled camera with the highest priority wins; ties go
// to the one created first.
class CameraComponent final : public Component {
 public:
  const char* typeName() const override { return "Camera"; }
  float fovYRadians = 1.0471976F;  // 60 degrees
  float nearZ = 0.1F;
  float farZ = 200.0F;
  int priority = 0;
};

class LightComponent final : public Component {
 public:
  const char* typeName() const override { return "Light"; }
  renderer::LightType type = renderer::LightType::Directional;
  Vec3 color{1, 1, 1};
  float intensity = 1.0F;
  float range = 10.0F;
  float innerConeRadians = 0.4F;
  float outerConeRadians = 0.6F;
  bool castShadow = false;
};

// Collects the active scene into a flat RenderScene (world matrices, skin matrices, lights, the
// main camera, the scene environment). Fails when there is no active enabled camera, or when
// animation evaluation fails. Objects that cannot be drawn (no mesh/material, skinned mesh without
// a usable Animator) are skipped with a one-time log message per component. Lights beyond the
// renderer's limit (one shadowed directional + kMaxPunctualLights point/spot) are dropped, nearest
// to the camera kept.
Result<renderer::RenderScene> buildRenderScene(Scene& scene, float aspect);
// Same, but seen from an explicit camera (editor viewport); needs no Camera component.
Result<renderer::RenderScene> buildRenderScene(Scene& scene, const renderer::Camera& camera);

}  // namespace mingol::scene
