// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>

#include "audio_test_util.hpp"
#include "mingol/runtime/runtime.hpp"

using namespace mingol;
using namespace mingol::runtime;
using namespace mingol::scene;
using namespace mingol::test;

namespace {
std::unique_ptr<Runtime> rtOrFail() {
  auto r = Runtime::create();
  if (!r.ok()) {
    fail(__FILE__, __LINE__, "Runtime::create: " + r.error().describe());
    return nullptr;
  }
  return std::move(r).value();
}
std::shared_ptr<const assets::AudioClip> constClip(std::uint64_t frames, float v) {
  return makeClip(1, 48000, frames, [v](std::uint64_t, std::uint32_t) { return v; });
}
float peak(const std::vector<float>& s, int channel) {
  float m = 0;
  for (std::size_t i = static_cast<std::size_t>(channel); i < s.size(); i += 2) m = std::max(m, std::fabs(s[i]));
  return m;
}
}  // namespace

TEST(RuntimeAudio, PlayOnEnableStartsAndDisableStops) {
  auto rt = rtOrFail();
  REQUIRE(rt != nullptr);
  GameObject* go = rt->scene().createObject("s");
  auto* src = go->addComponent<AudioSource>();
  src->clip = constClip(48000, 0.5F);
  CHECK(!src->isPlaying());  // nothing starts before the first frame
  rt->tick(0.0F);  // start -> playOnEnable
  auto out = renderFrames(rt->mixer(), 256);
  CHECK(src->isPlaying());  // visible after the next rendered block
  CHECK_EQ(rt->mixer().stats().activeVoices, 1u);
  CHECK(peak(out, 0) > 0.1F);
  CHECK(peak(out, 1) > 0.1F);
  src->setEnabled(false);
  out = renderFrames(rt->mixer(), 256);
  CHECK(!src->isPlaying());
  CHECK_NEAR(peak(out, 0), 0.0F, 1e-6F);
  src->setEnabled(true);  // re-enabling plays again
  out = renderFrames(rt->mixer(), 256);
  CHECK(peak(out, 0) > 0.1F);
}

TEST(RuntimeAudio, PlayWithoutClipOrMixerFails) {
  auto rt = rtOrFail();
  REQUIRE(rt != nullptr);
  GameObject* go = rt->scene().createObject("s");
  auto* src = go->addComponent<AudioSource>();
  CHECK(!src->play().ok());  // no clip
  Scene bare;
  auto* src2 = bare.createObject("x")->addComponent<AudioSource>();
  src2->clip = constClip(10, 1.0F);
  CHECK(!src2->play().ok());  // no mixer service
}

TEST(RuntimeAudio, SpatialSourceIsPannedFromTheListenerAndFollowsTheObject) {
  auto rt = rtOrFail();
  REQUIRE(rt != nullptr);
  GameObject* ear = rt->scene().createObject("ear");
  ear->addComponent<AudioListener>();  // at origin looking down -Z, +X is to the right
  GameObject* go = rt->scene().createObject("s");
  go->transform().setLocalPosition({5, 0, 0});
  auto* src = go->addComponent<AudioSource>();
  src->clip = constClip(48000 * 4, 0.5F);
  src->spatial = true;
  src->loop = true;
  rt->tick(0.0F);
  rt->tick(1.0F / 60.0F);
  auto out = renderFrames(rt->mixer(), 2048);
  out = renderFrames(rt->mixer(), 2048);
  CHECK(peak(out, 1) > 4.0F * peak(out, 0));  // on the right
  go->transform().setLocalPosition({-5, 0, 0});
  rt->tick(1.0F / 60.0F);
  for (int i = 0; i < 4; ++i) out = renderFrames(rt->mixer(), 2048);  // let the gain ramp settle
  CHECK(peak(out, 0) > 4.0F * peak(out, 1));  // moved to the left
}

TEST(RuntimeAudio, ListenerFollowsItsObject) {
  auto rt = rtOrFail();
  REQUIRE(rt != nullptr);
  GameObject* ear = rt->scene().createObject("ear");
  ear->addComponent<AudioListener>();
  GameObject* go = rt->scene().createObject("s");
  go->transform().setLocalPosition({5, 0, 0});
  auto* src = go->addComponent<AudioSource>();
  src->clip = constClip(48000 * 4, 0.5F);
  src->spatial = true;
  src->loop = true;
  rt->tick(0.0F);
  ear->transform().setLocalPosition({10, 0, 0});  // the source is now on the listener's left
  rt->tick(1.0F / 60.0F);
  std::vector<float> out;
  for (int i = 0; i < 4; ++i) out = renderFrames(rt->mixer(), 2048);
  CHECK(peak(out, 0) > 4.0F * peak(out, 1));
}

TEST(RuntimeAudio, ApplySettingsChangesTheRunningVoice) {
  auto rt = rtOrFail();
  REQUIRE(rt != nullptr);
  GameObject* go = rt->scene().createObject("s");
  auto* src = go->addComponent<AudioSource>();
  src->clip = constClip(48000 * 4, 0.5F);
  src->loop = true;
  rt->tick(0.0F);
  std::vector<float> out;
  for (int i = 0; i < 3; ++i) out = renderFrames(rt->mixer(), 2048);
  const float loud = peak(out, 0);
  src->volume = 0.25F;
  CHECK(src->applySettings().ok());
  for (int i = 0; i < 3; ++i) out = renderFrames(rt->mixer(), 2048);
  CHECK_NEAR(peak(out, 0), loud * 0.25F, loud * 0.02F);
}

TEST(RuntimeAudio, DestroyingTheSceneStopsAllVoices) {
  auto rt = rtOrFail();
  REQUIRE(rt != nullptr);
  for (int i = 0; i < 8; ++i) {
    GameObject* go = rt->scene().createObject("s");
    auto* src = go->addComponent<AudioSource>();
    src->clip = constClip(48000, 0.1F);
    src->loop = true;
  }
  rt->tick(0.0F);
  renderFrames(rt->mixer(), 64);
  CHECK_EQ(rt->mixer().stats().activeVoices, 8u);
  rt->scene().clear();
  renderFrames(rt->mixer(), 128);
  CHECK_EQ(rt->mixer().stats().activeVoices, 0u);
}

TEST(RuntimeAudio, NullDeviceRuntimeStartsAndPullsFrames) {
  RuntimeDesc d;
  d.openAudioDevice = true;
  d.audioBackend = audio::DeviceBackend::Null;
  auto r = Runtime::create(d);
  REQUIRE(r.ok());
  auto rt = std::move(r).value();
  rt->tick(0.016F);
  rt->scene().clear();
}
