// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/runtime/audio_components.hpp"

#include "mingol/core/log.hpp"

namespace mingol::runtime {
namespace {
audio::Mixer* mixerOf(scene::Component& c) { return c.scene().service<audio::Mixer>(); }
}  // namespace

Result<void> AudioSource::play() {
  audio::Mixer* mixer = mixerOf(*this);
  if (mixer == nullptr) return makeError(ErrorCode::NotFound, "the scene has no audio::Mixer service");
  if (!clip) return makeError(ErrorCode::InvalidArgument, "AudioSource on '{}' has no clip", gameObject().name());
  stop();
  audio::PlayDesc d;
  d.volume = volume;
  d.pitch = pitch;
  d.pan = pan;
  d.loop = loop;
  d.priority = priority;
  d.spatial = spatial;
  d.minDistance = minDistance;
  d.maxDistance = maxDistance;
  d.rolloff = rolloff;
  if (spatial) d.position = gameObject().transform().worldPosition();
  MINGOL_TRY_ASSIGN(auto id, mixer->play(clip, d));
  voice_ = id;
  return {};
}

void AudioSource::stop() {
  if (!voice_.valid()) return;
  if (audio::Mixer* m = mixerOf(*this)) m->stop(voice_);
  voice_ = {};
}

Result<void> AudioSource::setPaused(bool paused) {
  audio::Mixer* m = mixerOf(*this);
  if (m == nullptr || !voice_.valid()) return makeError(ErrorCode::NotFound, "nothing is playing");
  return m->setPaused(voice_, paused);
}

bool AudioSource::isPlaying() const {
  audio::Mixer* m = const_cast<AudioSource*>(this)->scene().service<audio::Mixer>();
  return m != nullptr && voice_.valid() && m->isPlaying(voice_);
}

Result<void> AudioSource::applySettings() {
  audio::Mixer* m = mixerOf(*this);
  if (m == nullptr || !voice_.valid()) return makeError(ErrorCode::NotFound, "nothing is playing");
  MINGOL_TRY(m->setVolume(voice_, volume));
  MINGOL_TRY(m->setPitch(voice_, pitch));
  if (!spatial) MINGOL_TRY(m->setPan(voice_, pan));
  return {};
}

void AudioSource::onEnable() {
  if (started_) startIfConfigured();
}

void AudioSource::onStart() {
  started_ = true;
  startIfConfigured();
}

void AudioSource::startIfConfigured() {
  if (!playOnEnable || !clip) return;
  if (auto r = play(); !r.ok()) MINGOL_LOG_WARN("audio", "AudioSource on '{}' could not start: {}", gameObject().name(), r.error().describe());
}

void AudioSource::onDisable() { stop(); }

void AudioSource::onLateUpdate(float) {
  if (!spatial || !voice_.valid()) return;
  if (audio::Mixer* m = mixerOf(*this)) (void)m->setPosition(voice_, gameObject().transform().worldPosition());
}

void AudioListener::onEnable() { onLateUpdate(0.0F); }

void AudioListener::onLateUpdate(float) {
  audio::Mixer* m = mixerOf(*this);
  if (m == nullptr) return;
  const scene::Transform& t = gameObject().transform();
  m->setListener(t.worldPosition(), t.forward(), t.up());
}

}  // namespace mingol::runtime
