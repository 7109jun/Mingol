// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "mingol/audio/mixer.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

using Gen = std::function<float(std::uint64_t frame, std::uint32_t channel)>;

inline std::shared_ptr<assets::AudioClip> makeClip(std::uint32_t channels, std::uint32_t rate, std::uint64_t frames, const Gen& g) {
  auto c = std::make_shared<assets::AudioClip>();
  c->channels = channels;
  c->sampleRate = rate;
  c->samples.resize(frames * channels);
  for (std::uint64_t f = 0; f < frames; ++f) {
    for (std::uint32_t ch = 0; ch < channels; ++ch) c->samples[f * channels + ch] = g(f, ch);
  }
  return c;
}

// frame / 1000 on every channel: a ramp, so linear resampling is exact.
inline std::shared_ptr<assets::AudioClip> rampClip(std::uint32_t channels, std::uint32_t rate, std::uint64_t frames) {
  return makeClip(channels, rate, frames, [](std::uint64_t f, std::uint32_t) { return static_cast<float>(f) / 1000.0F; });
}

inline std::unique_ptr<audio::Mixer> makeMixer(std::uint32_t maxVoices = 64, std::uint32_t rate = 48000) {
  audio::MixerDesc d;
  d.maxVoices = maxVoices;
  d.sampleRate = rate;
  auto m = audio::Mixer::create(d);
  if (!m.ok()) {
    fail(__FILE__, __LINE__, "Mixer::create: " + m.error().describe());
    return nullptr;
  }
  return std::move(m).value();
}

inline std::vector<float> renderFrames(audio::Mixer& m, std::uint32_t frames) {
  std::vector<float> out(static_cast<std::size_t>(frames) * 2, 123.0F);  // poison: render must overwrite everything
  m.render(out.data(), frames);
  return out;
}

inline audio::VoiceId playOk(audio::Mixer& m, std::shared_ptr<const assets::AudioClip> clip, const audio::PlayDesc& d = {}) {
  auto r = m.play(std::move(clip), d);
  if (!r.ok()) {
    fail(__FILE__, __LINE__, "play: " + r.error().describe());
    return {};
  }
  return r.value();
}

}  // namespace mingol::test
