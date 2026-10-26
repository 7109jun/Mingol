// SPDX-License-Identifier: GPL-2.0-only
// Audio for the GameObject scene. Both components find the mixer through the Scene service
// registry (Scene::setService<audio::Mixer>), which Runtime fills in.
#pragma once

#include <memory>

#include "mingol/audio/mixer.hpp"
#include "mingol/scene/scene.hpp"

namespace mingol::runtime {

class AudioSource final : public scene::Component {
 public:
  const char* typeName() const override { return "AudioSource"; }

  std::shared_ptr<const assets::AudioClip> clip;
  float volume = 1.0F;
  float pitch = 1.0F;
  float pan = 0.0F;          // 2D sources only
  bool loop = false;
  bool playOnEnable = true;  // start when the component becomes active
  int priority = 0;
  bool spatial = false;      // 3D: attenuated and panned from the AudioListener; follows this object
  float minDistance = 1.0F;
  float maxDistance = 50.0F;
  float rolloff = 1.0F;

  // Restarts the clip from the beginning. Fails without a clip or mixer, or for invalid settings.
  Result<void> play();
  void stop();
  Result<void> setPaused(bool paused);
  // Reflects the mixer's state as of the last rendered block (see audio::Mixer::isPlaying).
  bool isPlaying() const;
  // Applies volume/pitch/pan changes made after play() to the running voice.
  Result<void> applySettings();

 protected:
  void onEnable() override;
  void onStart() override;
  void onDisable() override;
  void onLateUpdate(float dt) override;

 private:
  void startIfConfigured();
  audio::VoiceId voice_;
  bool started_ = false;
};

// Where the player "hears" from: position and orientation of its GameObject. Use one per scene.
class AudioListener final : public scene::Component {
 public:
  const char* typeName() const override { return "AudioListener"; }

 protected:
  void onLateUpdate(float dt) override;
  void onEnable() override;
};

}  // namespace mingol::runtime
