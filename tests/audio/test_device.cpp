// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>
#include <thread>

#include "audio_test_util.hpp"
#include "mingol/audio/device.hpp"

using namespace mingol;
using namespace mingol::audio;
using namespace mingol::test;

namespace {
template <class Pred>
bool waitFor(Pred p, int timeoutMs) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < end) {
    if (p()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return p();
}
}  // namespace

TEST(Device, NullBackendPullsAudioFromTheMixerInRealTime) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  PlayDesc d;
  d.loop = true;
  const VoiceId v = playOk(*m, rampClip(2, 48000, 1000), d);
  DeviceDesc dd;
  dd.backend = DeviceBackend::Null;
  auto dev = Device::create(*m, dd);
  REQUIRE(dev.ok());
  CHECK_EQ(dev.value()->backendName(), std::string("Null"));
  CHECK_MSG(waitFor([&] { return dev.value()->framesPulled() >= 9600; }, 5000), "the device never pulled 0.2 s of audio");
  CHECK(m->stats().framesRendered >= dev.value()->framesPulled() - 4096);
  CHECK(m->isPlaying(v));
  CHECK_EQ(m->stats().activeVoices, 1u);
  m->stop(v);
  CHECK_MSG(waitFor([&] { return !m->isPlaying(v); }, 2000), "stop() was not applied by the audio thread");
}

TEST(Device, CreateAndDestroyRepeatedlyJoinsTheAudioThread) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  for (int i = 0; i < 5; ++i) {
    DeviceDesc dd;
    dd.backend = DeviceBackend::Null;
    auto dev = Device::create(*m, dd);
    REQUIRE(dev.ok());
    waitFor([&] { return dev.value()->framesPulled() > 0; }, 2000);
  }  // ~Device must stop the callback before the mixer or the device memory goes away
  const auto before = m->stats().framesRendered;
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  CHECK_EQ(m->stats().framesRendered, before);  // nothing keeps rendering after the devices are gone
}
