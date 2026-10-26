// SPDX-License-Identifier: GPL-2.0-only
// The game loop: owns the Scene and the physics/audio services and advances them frame by frame.
//
// One tick(dt): whole fixed steps first (Scene::fixedUpdate -> physics step -> pose write-back ->
// contact callbacks), then Scene::update and Scene::lateUpdate once with the frame time. Time is
// supplied by the caller (so tests and replays are exact); `Runtime::run` is a convenience that
// drives tick() from a monotonic clock.
#pragma once

#include <functional>
#include <memory>

#include "mingol/audio/device.hpp"
#include "mingol/audio/mixer.hpp"
#include "mingol/runtime/audio_components.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/scene.hpp"

namespace mingol::runtime {

struct RuntimeDesc {
  float fixedStep = 1.0F / 60.0F;
  int maxFixedStepsPerFrame = 8;   // beyond this, simulation time is dropped (no spiral of death)
  float maxFrameTime = 0.25F;      // longer frames (debugger pauses, window drags) are clamped
  physics::WorldDesc physics;      // physics.fixedStep is overridden by `fixedStep`
  audio::MixerDesc mixer;
  bool openAudioDevice = false;    // false: the mixer exists but nothing pulls from it
  audio::DeviceBackend audioBackend = audio::DeviceBackend::Default;
};

struct RuntimeStats {
  std::uint64_t frames = 0;
  std::uint64_t fixedSteps = 0;
  std::uint64_t droppedFixedSteps = 0;  // simulation time discarded by the step limit
  double time = 0.0;                    // scaled seconds since start
};

class Runtime {
 public:
  static Result<std::unique_ptr<Runtime>> create(const RuntimeDesc& desc = {});
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  scene::Scene& scene() { return scene_; }
  PhysicsSystem& physics() { return *physics_; }
  audio::Mixer& mixer() { return *mixer_; }
  const RuntimeStats& stats() const noexcept { return stats_; }

  // Time scale: 0 pauses the simulation (frames still tick, with dt = 0), 1 is real time.
  void setTimeScale(float scale);
  float timeScale() const noexcept { return timeScale_; }

  // Advances one frame by `realDt` real seconds.
  void tick(float realDt);

  // Runs frames until `keepGoing` returns false, measuring time with a monotonic clock. Returns
  // the number of frames. `onFrame` (optional) runs after each tick, e.g. to render.
  std::uint64_t run(const std::function<bool()>& keepGoing, const std::function<void(float dt)>& onFrame = {});

 private:
  Runtime() = default;

  RuntimeDesc desc_;
  // Declaration order = destruction order reversed: the scene must go first (its components call
  // into physics/audio while being destroyed), then the device, then the services.
  std::unique_ptr<audio::Mixer> mixer_;
  std::unique_ptr<audio::Device> device_;
  std::unique_ptr<PhysicsSystem> physics_;
  scene::Scene scene_;
  float timeScale_ = 1.0F;
  float accumulator_ = 0.0F;
  RuntimeStats stats_;
};

}  // namespace mingol::runtime
