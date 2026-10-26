// SPDX-License-Identifier: GPL-2.0-only
// Skeletons, keyframe clips and pose evaluation. Pure CPU code with no GPU dependency, so the
// results (joint matrices) are exactly testable.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"

namespace mingol::renderer {

inline constexpr std::size_t kMaxJoints = 128;  // limit shared with the skinning shader

struct Transform {
  Vec3 translation{0, 0, 0};
  Quat rotation{};
  Vec3 scale{1, 1, 1};
  Mat4 matrix() const { return Mat4::trs(translation, rotation, scale); }
};

struct Joint {
  std::string name;
  int parent = -1;       // index of the parent joint, -1 for a root; must be smaller than this joint's index
  Transform bindLocal;   // rest pose relative to the parent
  std::optional<Mat4> inverseBind;  // model->joint space at rest; computed from bindLocal when absent
};

class Skeleton {
 public:
  // Fails when: no joints, more than kMaxJoints, a parent index is not smaller than the joint's own
  // (this guarantees parents are evaluated first), duplicate names, non-invertible bind pose.
  static Result<Skeleton> create(std::vector<Joint> joints);

  std::size_t jointCount() const noexcept { return joints_.size(); }
  const Joint& joint(std::size_t i) const { return joints_[i]; }
  const Mat4& inverseBind(std::size_t i) const { return inverseBind_[i]; }
  std::optional<std::size_t> findJoint(const std::string& name) const;

  // Rest pose as local transforms, one per joint.
  std::vector<Transform> bindPose() const;

 private:
  std::vector<Joint> joints_;
  std::vector<Mat4> inverseBind_;
};

enum class Interpolation : std::uint8_t { Linear, Step };

template <class T>
struct Track {
  std::vector<float> times;  // strictly increasing, seconds
  std::vector<T> values;     // same length as times
  Interpolation interpolation = Interpolation::Linear;
  bool empty() const noexcept { return times.empty(); }
};

// Animates one joint. An empty track leaves that component at the bind pose.
struct Channel {
  std::size_t joint = 0;
  Track<Vec3> translation;
  Track<Quat> rotation;
  Track<Vec3> scale;
};

class AnimationClip {
 public:
  // Fails when a track is malformed (length mismatch, non-increasing or non-finite times) or
  // duration is not positive.
  static Result<AnimationClip> create(std::string name, float duration, std::vector<Channel> channels);

  const std::string& name() const noexcept { return name_; }
  float duration() const noexcept { return duration_; }
  const std::vector<Channel>& channels() const noexcept { return channels_; }

 private:
  std::string name_;
  float duration_ = 0;
  std::vector<Channel> channels_;
};

// Local transforms of the skeleton at `time`. Looping wraps time into [0, duration); otherwise it
// clamps. Fails when a channel refers to a joint the skeleton does not have.
Result<std::vector<Transform>> samplePose(const Skeleton& skeleton, const AnimationClip& clip, float time,
                                          bool loop);

// Blends two poses of the same skeleton: translation/scale lerp, rotation slerp. t is clamped to [0,1].
std::vector<Transform> blendPoses(const std::vector<Transform>& a, const std::vector<Transform>& b, float t);

// Model-space matrices that map bind-pose vertices to the posed position (global * inverseBind),
// ready to upload for skinning. `pose.size()` must equal the joint count.
Result<std::vector<Mat4>> computeSkinMatrices(const Skeleton& skeleton, const std::vector<Transform>& pose);

// Model-space joint transforms (without inverse bind), e.g. to attach objects to bones.
Result<std::vector<Mat4>> computeGlobalPose(const Skeleton& skeleton, const std::vector<Transform>& pose);

}  // namespace mingol::renderer
