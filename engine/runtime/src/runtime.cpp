// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/runtime/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "mingol/core/log.hpp"

namespace mingol::runtime {

Result<std::unique_ptr<Runtime>> Runtime::create(const RuntimeDesc& desc) {
  if (!(desc.fixedStep > 0.0F) || desc.fixedStep > 1.0F) return makeError(ErrorCode::InvalidArgument, "fixedStep must be in (0, 1]");
  if (desc.maxFixedStepsPerFrame < 1) return makeError(ErrorCode::InvalidArgument, "maxFixedStepsPerFrame must be >= 1");
  if (!(desc.maxFrameTime > 0.0F)) return makeError(ErrorCode::InvalidArgument, "maxFrameTime must be positive");
  std::unique_ptr<Runtime> rt(new Runtime());
  rt->desc_ = desc;

  physics::WorldDesc wd = desc.physics;
  wd.fixedStep = desc.fixedStep;
  MINGOL_TRY_ASSIGN(auto physics, PhysicsSystem::create(wd));
  rt->physics_ = std::move(physics);

  MINGOL_TRY_ASSIGN(auto mixer, audio::Mixer::create(desc.mixer));
  rt->mixer_ = std::move(mixer);
  if (desc.openAudioDevice) {
    audio::DeviceDesc dd;
    dd.backend = desc.audioBackend;
    auto dev = audio::Device::create(*rt->mixer_, dd);
    if (dev.ok()) {
      rt->device_ = std::move(dev).value();
    } else {
      // A machine without audio hardware must still be able to run the game.
      MINGOL_LOG_WARN("runtime", "audio output unavailable ({}); running silent", dev.error().describe());
    }
  }
  rt->scene_.setService<PhysicsSystem>(rt->physics_.get());
  rt->scene_.setService<audio::Mixer>(rt->mixer_.get());
  return rt;
}

Runtime::~Runtime() {
  scene_.clear();  // components still need physics/audio while they are torn down
}

void Runtime::setTimeScale(float scale) {
  if (std::isfinite(scale) && scale >= 0.0F && scale <= 100.0F) timeScale_ = scale;
}

void Runtime::tick(float realDt) {
  if (!std::isfinite(realDt) || realDt < 0.0F) realDt = 0.0F;
  const float dt = std::min(realDt, desc_.maxFrameTime) * timeScale_;
  accumulator_ += dt;
  const float fixed = desc_.fixedStep;
  int steps = 0;
  while (accumulator_ + fixed * 1e-4F >= fixed && steps < desc_.maxFixedStepsPerFrame) {
    scene_.fixedUpdate(fixed);
    physics_->fixedStep(scene_);
    accumulator_ -= fixed;
    ++steps;
    ++stats_.fixedSteps;
  }
  if (accumulator_ < 0.0F) accumulator_ = 0.0F;
  if (steps == desc_.maxFixedStepsPerFrame && accumulator_ >= fixed) {
    ++stats_.droppedFixedSteps;
    accumulator_ = 0.0F;
  }
  scene_.update(dt);
  scene_.lateUpdate(dt);
  stats_.time += dt;
  ++stats_.frames;
}

std::uint64_t Runtime::run(const std::function<bool()>& keepGoing, const std::function<void(float)>& onFrame) {
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  std::uint64_t frames = 0;
  while (keepGoing()) {
    const auto now = Clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    tick(dt);
    if (onFrame) onFrame(dt);
    ++frames;
  }
  return frames;
}

}  // namespace mingol::runtime
