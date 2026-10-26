// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/scene/components.hpp"

#include <algorithm>
#include <cmath>

#include "mingol/core/log.hpp"

namespace mingol::scene {

// ---- Animator -------------------------------------------------------------------------------

Result<void> Animator::setSkeleton(std::shared_ptr<const renderer::Skeleton> skeleton) {
  if (!skeleton || skeleton->jointCount() == 0) return makeError(ErrorCode::InvalidArgument, "skeleton has no joints");
  skeleton_ = std::move(skeleton);
  clips_.clear();
  stop();
  return {};
}

Result<void> Animator::addClip(std::shared_ptr<const renderer::AnimationClip> clip) {
  if (!skeleton_) return makeError(ErrorCode::InvalidArgument, "set the skeleton before adding clips");
  if (!clip || clip->name().empty()) return makeError(ErrorCode::InvalidArgument, "clip needs a name");
  if (clips_.count(clip->name()) != 0) {
    return makeError(ErrorCode::InvalidArgument, "duplicate clip name '{}'", clip->name());
  }
  for (const auto& ch : clip->channels()) {
    if (ch.joint >= skeleton_->jointCount()) {
      return makeError(ErrorCode::InvalidArgument, "clip '{}' animates joint {} but the skeleton has {}",
                       clip->name(), ch.joint, skeleton_->jointCount());
    }
  }
  clips_.emplace(clip->name(), std::move(clip));
  return {};
}

Result<void> Animator::play(const std::string& clipName, bool loop, float fadeSeconds) {
  if (clips_.count(clipName) == 0) return makeError(ErrorCode::NotFound, "unknown clip '{}'", clipName);
  if (fadeSeconds > 0.0F && playing()) {
    previous_ = current_;
    fadeDuration_ = fadeSeconds;
    fadeElapsed_ = 0.0F;
  } else {
    fadeDuration_ = 0.0F;
    fadeElapsed_ = 0.0F;
    previous_ = State{};
  }
  current_ = State{clipName, 0.0F, loop};
  finished_ = false;
  return {};
}

void Animator::stop() {
  current_ = State{};
  previous_ = State{};
  fadeDuration_ = fadeElapsed_ = 0.0F;
  finished_ = false;
}

void Animator::advance(float dt) {
  if (!playing() || !(dt > 0.0F)) return;
  const float step = dt * speed;
  auto step1 = [&](State& s) {
    const auto it = clips_.find(s.clip);
    if (it == clips_.end()) return;
    const float d = it->second->duration();
    s.time += step;
    if (s.loop) {
      s.time = std::fmod(s.time, d);
      if (s.time < 0.0F) s.time += d;
    } else if (s.time >= d) {
      s.time = d;
      finished_ = (&s == &current_);
    } else if (s.time < 0.0F) {
      s.time = 0.0F;
    }
  };
  step1(current_);
  if (fadeDuration_ > 0.0F) {
    step1(previous_);
    fadeElapsed_ += dt;  // fades run in real (unscaled) time
    if (fadeElapsed_ >= fadeDuration_) {
      fadeDuration_ = fadeElapsed_ = 0.0F;
      previous_ = State{};
    }
  }
}

Result<std::vector<renderer::Transform>> Animator::pose(const State& s) const {
  return renderer::samplePose(*skeleton_, *clips_.at(s.clip), s.time, s.loop);
}

Result<std::vector<Mat4>> Animator::skinMatrices() const {
  if (!skeleton_) return makeError(ErrorCode::InvalidArgument, "Animator has no skeleton");
  std::vector<renderer::Transform> p;
  if (!playing()) {
    p = skeleton_->bindPose();
  } else {
    MINGOL_TRY_ASSIGN(auto cur, pose(current_));
    p = std::move(cur);
    if (fadeDuration_ > 0.0F && !previous_.clip.empty()) {
      MINGOL_TRY_ASSIGN(auto prev, pose(previous_));
      p = renderer::blendPoses(prev, p, fadeElapsed_ / fadeDuration_);
    }
  }
  return renderer::computeSkinMatrices(*skeleton_, p);
}

// ---- buildRenderScene -----------------------------------------------------------------------

namespace {
Mat4 viewFrom(const Transform& t) {
  // Rigid part only: scale on a camera object must not distort the image.
  const Mat4 rigid = Mat4::trs(t.worldPosition(), t.worldRotation(), {1, 1, 1});
  Mat4 inv;
  if (!inverse(rigid, inv)) return Mat4::identity();
  return inv;
}
}  // namespace

Result<renderer::RenderScene> buildRenderScene(Scene& scene, float aspect) {
  if (!(aspect > 0.0F) || !std::isfinite(aspect)) return makeError(ErrorCode::InvalidArgument, "bad aspect {}", aspect);
  const CameraComponent* best = nullptr;
  for (const CameraComponent* c : scene.findComponents<CameraComponent>()) {
    if (!c->activeAndEnabled()) continue;
    if (best == nullptr || c->priority > best->priority) best = c;
  }
  if (best == nullptr) return makeError(ErrorCode::NotFound, "no active enabled Camera in the scene");
  const Transform& ct = best->gameObject().transform();
  renderer::Camera cam;
  cam.view = viewFrom(ct);
  cam.projection = Mat4::perspective(best->fovYRadians, aspect, best->nearZ, best->farZ);
  cam.position = ct.worldPosition();
  return buildRenderScene(scene, cam);
}

Result<renderer::RenderScene> buildRenderScene(Scene& scene, const renderer::Camera& camera) {
  renderer::RenderScene out;
  out.camera = camera;
  out.ambient = scene.environment.ambient;
  for (int i = 0; i < 4; ++i) out.clearColor[i] = scene.environment.clearColor[i];

  // Lights: keep one directional (the first), and the nearest punctual ones.
  struct Punctual {
    renderer::Light light;
    float dist2;
  };
  std::vector<Punctual> punctual;
  bool haveDirectional = false;
  for (const LightComponent* l : scene.findComponents<LightComponent>()) {
    if (!l->activeAndEnabled()) continue;
    const Transform& t = l->gameObject().transform();
    renderer::Light r;
    r.type = l->type;
    r.color = l->color;
    r.intensity = l->intensity;
    r.position = t.worldPosition();
    r.direction = t.forward();
    r.range = l->range;
    r.innerConeRadians = l->innerConeRadians;
    r.outerConeRadians = l->outerConeRadians;
    r.castShadow = l->castShadow && l->type == renderer::LightType::Directional;
    if (l->type == renderer::LightType::Directional) {
      if (haveDirectional) continue;
      haveDirectional = true;
      out.lights.push_back(r);
    } else {
      punctual.push_back({r, lengthSq(r.position - out.camera.position)});
    }
  }
  std::stable_sort(punctual.begin(), punctual.end(),
                   [](const Punctual& a, const Punctual& b) { return a.dist2 < b.dist2; });
  if (punctual.size() > static_cast<std::size_t>(renderer::kMaxPunctualLights)) {
    punctual.resize(static_cast<std::size_t>(renderer::kMaxPunctualLights));
  }
  for (const Punctual& p : punctual) out.lights.push_back(p.light);

  for (MeshRenderer* mr : scene.findComponents<MeshRenderer>()) {
    if (!mr->activeAndEnabled()) continue;
    GameObject& go = mr->gameObject();
    auto skip = [&](const char* why) {
      if (!mr->warned_) {
        mr->warned_ = true;
        MINGOL_LOG_WARN("scene", "MeshRenderer on '{}' is not drawn: {}", go.name(), why);
      }
    };
    if (!mr->mesh || !mr->material) {
      skip("mesh or material is not set");
      continue;
    }
    renderer::DrawItem item;
    item.mesh = mr->mesh;
    item.material = mr->material;
    item.transform = go.transform().worldMatrix();
    item.castShadow = mr->castShadow;
    if (mr->mesh->skinned()) {
      const Animator* anim = go.getComponent<Animator>();
      if (anim == nullptr || anim->skeleton() == nullptr) {
        skip("the mesh is skinned but the object has no Animator with a skeleton");
        continue;
      }
      MINGOL_TRY_ASSIGN(auto mats, anim->skinMatrices());
      if (mr->mesh->maxJointIndex() >= mats.size()) {
        skip("the mesh uses joints the Animator's skeleton does not have");
        continue;
      }
      item.skinMatrices = std::move(mats);
    }
    mr->warned_ = false;
    out.items.push_back(std::move(item));
  }
  return out;
}

}  // namespace mingol::scene
