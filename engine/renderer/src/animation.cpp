// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/renderer/animation.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace mingol::renderer {

Result<Skeleton> Skeleton::create(std::vector<Joint> joints) {
  if (joints.empty()) return makeError(ErrorCode::InvalidArgument, "skeleton has no joints");
  if (joints.size() > kMaxJoints) {
    return makeError(ErrorCode::OutOfRange, "skeleton has {} joints, the maximum is {}", joints.size(), kMaxJoints);
  }
  std::unordered_set<std::string> names;
  for (std::size_t i = 0; i < joints.size(); ++i) {
    const Joint& j = joints[i];
    if (j.parent < -1 || j.parent >= static_cast<int>(i)) {
      return makeError(ErrorCode::InvalidArgument,
                       "joint {} ('{}') has parent {}: a parent must come before its children", i, j.name, j.parent);
    }
    if (!j.name.empty() && !names.insert(j.name).second) {
      return makeError(ErrorCode::AlreadyExists, "duplicate joint name '{}'", j.name);
    }
  }
  Skeleton s;
  s.joints_ = std::move(joints);
  s.inverseBind_.resize(s.joints_.size());
  std::vector<Mat4> bindGlobal(s.joints_.size());
  for (std::size_t i = 0; i < s.joints_.size(); ++i) {
    const Joint& j = s.joints_[i];
    const Mat4 local = j.bindLocal.matrix();
    bindGlobal[i] = j.parent < 0 ? local : bindGlobal[static_cast<std::size_t>(j.parent)] * local;
    if (j.inverseBind) {
      s.inverseBind_[i] = *j.inverseBind;
    } else if (!inverse(bindGlobal[i], s.inverseBind_[i])) {
      return makeError(ErrorCode::InvalidArgument, "joint {} ('{}') has a non-invertible bind pose", i, j.name);
    }
  }
  return s;
}

std::optional<std::size_t> Skeleton::findJoint(const std::string& name) const {
  for (std::size_t i = 0; i < joints_.size(); ++i) {
    if (joints_[i].name == name) return i;
  }
  return std::nullopt;
}

std::vector<Transform> Skeleton::bindPose() const {
  std::vector<Transform> pose;
  pose.reserve(joints_.size());
  for (const Joint& j : joints_) pose.push_back(j.bindLocal);
  return pose;
}

namespace {

template <class T>
Result<void> validateTrack(const Track<T>& t, const char* what, std::size_t joint) {
  if (t.times.size() != t.values.size()) {
    return makeError(ErrorCode::InvalidArgument, "joint {} {} track: {} times but {} values", joint, what,
                     t.times.size(), t.values.size());
  }
  for (std::size_t i = 0; i < t.times.size(); ++i) {
    if (!std::isfinite(t.times[i]) || t.times[i] < 0.0F) {
      return makeError(ErrorCode::InvalidArgument, "joint {} {} track: invalid time {}", joint, what, t.times[i]);
    }
    if (i > 0 && !(t.times[i] > t.times[i - 1])) {
      return makeError(ErrorCode::InvalidArgument, "joint {} {} track: times must be strictly increasing", joint, what);
    }
  }
  return {};
}

// Returns the segment [i, i+1] containing time and the blend factor within it.
template <class T>
void locate(const Track<T>& t, float time, std::size_t& i0, std::size_t& i1, float& f) {
  if (time <= t.times.front()) {
    i0 = i1 = 0;
    f = 0;
    return;
  }
  if (time >= t.times.back()) {
    i0 = i1 = t.times.size() - 1;
    f = 0;
    return;
  }
  const auto it = std::upper_bound(t.times.begin(), t.times.end(), time);
  i1 = static_cast<std::size_t>(it - t.times.begin());
  i0 = i1 - 1;
  f = (time - t.times[i0]) / (t.times[i1] - t.times[i0]);
}

Vec3 sampleVec3(const Track<Vec3>& t, float time) {
  std::size_t a = 0, b = 0;
  float f = 0;
  locate(t, time, a, b, f);
  return t.interpolation == Interpolation::Step ? t.values[a] : lerp(t.values[a], t.values[b], f);
}

Quat sampleQuat(const Track<Quat>& t, float time) {
  std::size_t a = 0, b = 0;
  float f = 0;
  locate(t, time, a, b, f);
  return t.interpolation == Interpolation::Step ? t.values[a] : slerp(t.values[a], t.values[b], f);
}

}  // namespace

Result<AnimationClip> AnimationClip::create(std::string name, float duration, std::vector<Channel> channels) {
  if (!std::isfinite(duration) || duration <= 0.0F) {
    return makeError(ErrorCode::InvalidArgument, "animation '{}' needs a positive duration", name);
  }
  for (const Channel& c : channels) {
    MINGOL_TRY(validateTrack(c.translation, "translation", c.joint));
    MINGOL_TRY(validateTrack(c.rotation, "rotation", c.joint));
    MINGOL_TRY(validateTrack(c.scale, "scale", c.joint));
  }
  AnimationClip clip;
  clip.name_ = std::move(name);
  clip.duration_ = duration;
  clip.channels_ = std::move(channels);
  return clip;
}

Result<std::vector<Transform>> samplePose(const Skeleton& skeleton, const AnimationClip& clip, float time, bool loop) {
  if (!std::isfinite(time)) return makeError(ErrorCode::InvalidArgument, "non-finite animation time");
  float t = time;
  if (loop) {
    t = std::fmod(t, clip.duration());
    if (t < 0.0F) t += clip.duration();
  } else {
    t = clamp(t, 0.0F, clip.duration());
  }
  std::vector<Transform> pose = skeleton.bindPose();
  for (const Channel& c : clip.channels()) {
    if (c.joint >= pose.size()) {
      return makeError(ErrorCode::OutOfRange, "animation '{}' animates joint {} but the skeleton has {}", clip.name(),
                       c.joint, pose.size());
    }
    Transform& tr = pose[c.joint];
    if (!c.translation.empty()) tr.translation = sampleVec3(c.translation, t);
    if (!c.rotation.empty()) tr.rotation = normalize(sampleQuat(c.rotation, t));
    if (!c.scale.empty()) tr.scale = sampleVec3(c.scale, t);
  }
  return pose;
}

std::vector<Transform> blendPoses(const std::vector<Transform>& a, const std::vector<Transform>& b, float t) {
  const float w = clamp(t, 0.0F, 1.0F);
  const std::size_t n = std::min(a.size(), b.size());
  std::vector<Transform> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i].translation = lerp(a[i].translation, b[i].translation, w);
    out[i].scale = lerp(a[i].scale, b[i].scale, w);
    out[i].rotation = normalize(slerp(a[i].rotation, b[i].rotation, w));
  }
  return out;
}

Result<std::vector<Mat4>> computeGlobalPose(const Skeleton& skeleton, const std::vector<Transform>& pose) {
  if (pose.size() != skeleton.jointCount()) {
    return makeError(ErrorCode::InvalidArgument, "pose has {} transforms for {} joints", pose.size(),
                     skeleton.jointCount());
  }
  std::vector<Mat4> global(pose.size());
  for (std::size_t i = 0; i < pose.size(); ++i) {
    const Mat4 local = pose[i].matrix();
    const int parent = skeleton.joint(i).parent;
    global[i] = parent < 0 ? local : global[static_cast<std::size_t>(parent)] * local;
  }
  return global;
}

Result<std::vector<Mat4>> computeSkinMatrices(const Skeleton& skeleton, const std::vector<Transform>& pose) {
  MINGOL_TRY_ASSIGN(std::vector<Mat4> global, computeGlobalPose(skeleton, pose));
  for (std::size_t i = 0; i < global.size(); ++i) global[i] = global[i] * skeleton.inverseBind(i);
  return global;
}

}  // namespace mingol::renderer
