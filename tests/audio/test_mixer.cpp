// SPDX-License-Identifier: GPL-2.0-only
#include <atomic>
#include <thread>

#include "audio_test_util.hpp"

using namespace mingol;
using namespace mingol::audio;
using namespace mingol::test;

namespace {
constexpr double kInvSqrt2 = 0.70710678118;
bool near(float a, double b, double eps = 1e-5) { return std::fabs(a - b) <= eps; }
}  // namespace

TEST(Mixer, MonoClipIsCentredWithEqualPower) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  playOk(*m, rampClip(1, 48000, 1000));
  const auto out = renderFrames(*m, 100);
  for (std::uint32_t i = 0; i < 100; ++i) {
    CHECK_MSG(near(out[2 * i], i / 1000.0 * kInvSqrt2) && near(out[2 * i + 1], i / 1000.0 * kInvSqrt2), "frame " + std::to_string(i));
  }
}

TEST(Mixer, PanHardLeftAndRightForMono) {
  for (const float pan : {-1.0F, 1.0F}) {
    auto m = makeMixer();
    REQUIRE(m != nullptr);
    PlayDesc d;
    d.pan = pan;
    playOk(*m, rampClip(1, 48000, 1000), d);
    const auto out = renderFrames(*m, 50);
    const std::size_t loud = pan < 0 ? 0 : 1, quiet = 1 - loud;
    for (std::uint32_t i = 0; i < 50; ++i) {
      CHECK(near(out[2 * i + loud], i / 1000.0, 1e-5));
      CHECK(near(out[2 * i + quiet], 0.0, 1e-5));
    }
  }
}

TEST(Mixer, StereoClipKeepsChannelsAndUsesBalance) {
  auto clip = makeClip(2, 48000, 400, [](std::uint64_t, std::uint32_t ch) { return ch == 0 ? 0.3F : -0.2F; });
  {
    auto m = makeMixer();
    REQUIRE(m != nullptr);
    playOk(*m, clip);
    const auto out = renderFrames(*m, 10);
    CHECK(near(out[0], 0.3));
    CHECK(near(out[1], -0.2));  // not swapped, not panned at centre
  }
  {
    auto m = makeMixer();
    REQUIRE(m != nullptr);
    PlayDesc d;
    d.pan = 0.5F;  // towards the right: the left channel gets quieter
    playOk(*m, clip, d);
    const auto out = renderFrames(*m, 10);
    CHECK(near(out[0], 0.3 * std::cos(0.5 * 1.5707963)));
    CHECK(near(out[1], -0.2));
  }
}

TEST(Mixer, MultichannelIsDownmixed) {
  auto clip = makeClip(4, 48000, 100, [](std::uint64_t, std::uint32_t ch) {
    const float v[4] = {0.4F, 0.2F, 0.6F, 0.0F};
    return v[ch];
  });
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  playOk(*m, clip);
  const auto out = renderFrames(*m, 5);
  CHECK(near(out[0], 0.5));  // (ch0 + ch2) / 2
  CHECK(near(out[1], 0.1));  // (ch1 + ch3) / 2
}

TEST(Mixer, ResamplingAndPitchAreExactOnARamp) {
  {  // 24 kHz clip on a 48 kHz mixer: half a source frame per output frame.
    auto m = makeMixer();
    REQUIRE(m != nullptr);
    playOk(*m, rampClip(2, 24000, 1000));
    const auto out = renderFrames(*m, 200);
    for (std::uint32_t k = 0; k < 200; ++k) CHECK_MSG(near(out[2 * k], 0.5 * k / 1000.0, 1e-6), "k=" + std::to_string(k));
  }
  {  // pitch 2 doubles the step.
    auto m = makeMixer();
    REQUIRE(m != nullptr);
    PlayDesc d;
    d.pitch = 2.0F;
    playOk(*m, rampClip(2, 48000, 1000), d);
    const auto out = renderFrames(*m, 100);
    for (std::uint32_t k = 0; k < 100; ++k) CHECK(near(out[2 * k + 1], 2.0 * k / 1000.0, 1e-6));
  }
  {  // 44.1 kHz source: a non-integer ratio must still land on the ramp.
    auto m = makeMixer();
    REQUIRE(m != nullptr);
    playOk(*m, rampClip(2, 44100, 2000));
    const auto out = renderFrames(*m, 300);
    for (std::uint32_t k = 0; k < 300; ++k) CHECK(near(out[2 * k], (44100.0 / 48000.0) * k / 1000.0, 1e-5));
  }
}

TEST(Mixer, OneShotEndsExactlyAtTheClipLengthAndFreesItsVoice) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  const VoiceId v = playOk(*m, rampClip(2, 48000, 1000));
  auto a = renderFrames(*m, 500);
  CHECK(m->isPlaying(v));
  CHECK_EQ(m->stats().activeVoices, 1u);
  auto b = renderFrames(*m, 700);  // crosses the end at frame 1000 (= frame 500 of this block)
  for (std::uint32_t k = 0; k < 500; ++k) CHECK(near(b[2 * k], (500 + k) / 1000.0, 1e-6));
  for (std::uint32_t k = 500; k < 700; ++k) CHECK(b[2 * k] == 0.0F && b[2 * k + 1] == 0.0F);
  CHECK(!m->isPlaying(v));
  CHECK_EQ(m->stats().activeVoices, 0u);
  CHECK_EQ(m->stats().voicesStarted, std::uint64_t{1});
  (void)a;
}

TEST(Mixer, LoopsSeamlesslyUntilStopped) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  PlayDesc d;
  d.loop = true;
  const VoiceId v = playOk(*m, rampClip(2, 48000, 100), d);
  const auto out = renderFrames(*m, 350);
  for (std::uint32_t k = 0; k < 350; ++k) CHECK_MSG(near(out[2 * k], (k % 100) / 1000.0, 1e-6), "k=" + std::to_string(k));
  CHECK(m->isPlaying(v));
  m->stop(v);
  const auto after = renderFrames(*m, 10);
  for (float f : after) CHECK(f == 0.0F);
  CHECK(!m->isPlaying(v));
}

TEST(Mixer, VoicesSumAndTheOutputIsLimitedAndCounted) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  auto c = makeClip(2, 48000, 500, [](std::uint64_t, std::uint32_t) { return 0.3F; });
  for (int i = 0; i < 3; ++i) playOk(*m, c);
  auto out = renderFrames(*m, 10);
  CHECK(near(out[0], 0.9));
  CHECK_EQ(m->stats().clippedSamples, std::uint64_t{0});
  playOk(*m, c);
  playOk(*m, c);
  out = renderFrames(*m, 10);  // 5 x 0.3 = 1.5
  for (float f : out) CHECK(f == 1.0F);
  CHECK_EQ(m->stats().clippedSamples, std::uint64_t{20});
}

TEST(Mixer, SpatialAttenuationAndPanning) {
  auto clip = makeClip(1, 48000, 2000, [](std::uint64_t, std::uint32_t) { return 0.5F; });
  auto at = [&](Vec3 pos, Vec3 listenerPos = {0, 0, 0}, Vec3 fwd = {0, 0, -1}) {
    auto m = makeMixer();
    PlayDesc d;
    d.spatial = true;
    d.position = pos;
    playOk(*m, clip, d);
    m->setListener(listenerPos, fwd, {0, 1, 0});
    auto out = renderFrames(*m, 4);
    return std::pair{out[6], out[7]};  // L, R of a later frame
  };
  {  // 2 m ahead: gain 1 / (1 + 1) = 0.5, centred => 0.5 * 0.5 * cos(pi/4) per channel
    auto [l, r] = at({0, 0, -2});
    CHECK(near(l, 0.5 * 0.5 * kInvSqrt2));
    CHECK(near(r, 0.5 * 0.5 * kInvSqrt2));
  }
  {  // 10 m to the right: gain 0.1, fully right
    auto [l, r] = at({10, 0, 0});
    CHECK(near(l, 0.0, 1e-6));
    CHECK(near(r, 0.5 * 0.1, 1e-6));
  }
  {  // within the minimum distance: no attenuation; to the left
    auto [l, r] = at({-0.5F, 0, 0});
    CHECK(near(r, 0.0, 1e-6));
    CHECK(near(l, 0.5, 1e-6));
  }
  {  // beyond maxDistance the attenuation stops growing: 1 / (1 + 49)
    auto [l, r] = at({0, 0, -500});
    CHECK(near(l, 0.5 * 0.02 * kInvSqrt2, 1e-6));
    (void)r;
  }
  {  // listener turned to face +X: a source at +Z is now on the right
    auto [l, r] = at({0, 0, 3}, {0, 0, 0}, {1, 0, 0});
    CHECK(l < r);
    CHECK(near(l, 0.0, 1e-6));
  }
  {  // listener moved next to the source
    auto [l, r] = at({10, 0, -2}, {10, 0, 0});
    CHECK(near(l, 0.5 * 0.5 * kInvSqrt2) && near(r, l));
  }
}

TEST(Mixer, MasterVolumeAndVoiceVolume) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  auto c = makeClip(2, 48000, 4800, [](std::uint64_t, std::uint32_t) { return 0.8F; });
  PlayDesc d;
  d.volume = 0.5F;
  playOk(*m, c, d);
  CHECK(m->setMasterVolume(0.5F).ok());
  auto out = renderFrames(*m, 8);
  CHECK(near(out[0], 0.8 * 0.5 * 0.5));
  CHECK(!m->setMasterVolume(-1.0F).ok());
  CHECK(!m->setMasterVolume(std::nanf("")).ok());
}

TEST(Mixer, GainChangesAreRampedNotStepped) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  auto c = makeClip(2, 48000, 48000, [](std::uint64_t, std::uint32_t) { return 1.0F; });
  const VoiceId v = playOk(*m, c);
  const auto a = renderFrames(*m, 256);
  CHECK(near(a[2 * 255], 1.0));
  CHECK(m->setVolume(v, 0.0F).ok());
  const auto b = renderFrames(*m, 256);
  for (std::uint32_t i = 0; i < 256; ++i) CHECK_MSG(near(b[2 * i], 1.0 - (i + 1) / 256.0, 1e-5), "i=" + std::to_string(i));
  CHECK(near(b[2 * 255], 0.0, 1e-5));
  const auto c2 = renderFrames(*m, 16);
  for (float f : c2) CHECK(near(f, 0.0, 1e-6));
}

TEST(Mixer, PauseHoldsPositionAndResumeContinues) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  const VoiceId v = playOk(*m, rampClip(2, 48000, 1000));
  renderFrames(*m, 100);
  CHECK(m->setPaused(v, true).ok());
  const auto silent = renderFrames(*m, 100);
  for (float f : silent) CHECK(f == 0.0F);
  CHECK(m->isPlaying(v));  // paused is not finished
  CHECK(m->setPaused(v, false).ok());
  const auto resumed = renderFrames(*m, 50);
  for (std::uint32_t k = 0; k < 50; ++k) CHECK(near(resumed[2 * k], (100 + k) / 1000.0, 1e-6));
  // Starting paused.
  PlayDesc d;
  d.paused = true;
  const VoiceId p = playOk(*m, rampClip(2, 48000, 1000), d);
  const auto s2 = renderFrames(*m, 10);
  CHECK(m->isPlaying(p));
  (void)s2;
}

TEST(Mixer, SetPitchPanAndPositionTakeEffect) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  const VoiceId v = playOk(*m, rampClip(2, 48000, 4000));
  renderFrames(*m, 100);
  CHECK(m->setPitch(v, 2.0F).ok());
  const auto out = renderFrames(*m, 50);
  CHECK(near(out[2 * 40], (100 + 2.0 * 40) / 1000.0, 2e-6));  // the new speed applies from the next block
  CHECK(m->setPan(v, 1.0F).ok());
  renderFrames(*m, 600);  // let the pan ramp settle
  const auto panned = renderFrames(*m, 8);
  CHECK(near(panned[0], 0.0, 1e-6));
  CHECK(panned[1] > 0.0F);
}

TEST(Mixer, VoiceLimitStealsLowestPriorityThenOldest) {
  auto m = makeMixer(2);
  REQUIRE(m != nullptr);
  auto c = makeClip(2, 48000, 48000, [](std::uint64_t, std::uint32_t) { return 0.1F; });
  PlayDesc lo, hi, mid;
  lo.priority = 0;
  hi.priority = 5;
  mid.priority = 1;
  const VoiceId a = playOk(*m, c, lo);
  const VoiceId b = playOk(*m, c, hi);
  renderFrames(*m, 16);
  CHECK(m->isPlaying(a) && m->isPlaying(b));
  const VoiceId cId = playOk(*m, c, mid);  // steals `a` (lowest priority)
  renderFrames(*m, 16);
  CHECK(!m->isPlaying(a));
  CHECK(m->isPlaying(b) && m->isPlaying(cId));
  CHECK_EQ(m->stats().voicesStolen, std::uint64_t{1});
  const VoiceId dId = playOk(*m, c, lo);  // everything playing outranks it
  renderFrames(*m, 16);
  CHECK(!m->isPlaying(dId));
  CHECK(m->isPlaying(b) && m->isPlaying(cId));
  CHECK_EQ(m->stats().voicesRejected, std::uint64_t{1});
  const VoiceId e = playOk(*m, c, mid);  // equal to the weakest: the older of equals goes
  renderFrames(*m, 16);
  CHECK(m->isPlaying(e) && m->isPlaying(b));
  CHECK(!m->isPlaying(cId));
}

TEST(Mixer, StopAllAndStaleIds) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  auto c = rampClip(2, 48000, 48000);
  const VoiceId a = playOk(*m, c);
  const VoiceId b = playOk(*m, c);
  renderFrames(*m, 8);
  CHECK_EQ(m->stats().activeVoices, 2u);
  m->stopAll();
  renderFrames(*m, 8);
  CHECK(!m->isPlaying(a) && !m->isPlaying(b));
  CHECK_EQ(m->stats().activeVoices, 0u);
  // Commands for voices that no longer exist are harmless.
  CHECK(m->setVolume(a, 0.5F).ok());
  m->stop(a);
  m->stop(VoiceId{});
  renderFrames(*m, 8);
  CHECK(!m->isPlaying(VoiceId{}));
  CHECK(!m->setVolume(VoiceId{}, 1.0F).ok());
}

TEST(Mixer, InvalidArgumentsAreRejected) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  auto c = rampClip(2, 48000, 100);
  CHECK(!m->play(nullptr).ok());
  CHECK(!m->play(std::make_shared<assets::AudioClip>()).ok());
  auto bad = [&](auto mutate) {
    PlayDesc d;
    mutate(d);
    return m->play(c, d).ok();
  };
  CHECK(!bad([](PlayDesc& d) { d.volume = std::nanf(""); }));
  CHECK(!bad([](PlayDesc& d) { d.volume = -1.0F; }));
  CHECK(!bad([](PlayDesc& d) { d.pitch = 0.0F; }));
  CHECK(!bad([](PlayDesc& d) { d.pitch = 100.0F; }));
  CHECK(!bad([](PlayDesc& d) { d.pan = 2.0F; }));
  CHECK(!bad([](PlayDesc& d) { d.spatial = true; d.minDistance = 0.0F; }));
  CHECK(!bad([](PlayDesc& d) { d.spatial = true; d.minDistance = 10.0F; d.maxDistance = 5.0F; }));
  CHECK(!bad([](PlayDesc& d) { d.spatial = true; d.position = {INFINITY, 0, 0}; }));
  CHECK(bad([](PlayDesc& d) { d.volume = 16.0F; d.pitch = 0.01F; }));
  CHECK(!MixerDesc{}.sampleRate == 0);
  CHECK(!Mixer::create(MixerDesc{.sampleRate = 100}).ok());
  CHECK(!Mixer::create(MixerDesc{.maxVoices = 0}).ok());
  CHECK(!Mixer::create(MixerDesc{.maxVoices = 5000}).ok());
  m->setListener({std::nanf(""), 0, 0}, {0, 0, -1}, {0, 1, 0});  // ignored, must not poison the mixer
  m->setListener({0, 0, 0}, {0, 0, 0}, {0, 1, 0});
  const auto out = renderFrames(*m, 16);
  for (float f : out) CHECK(std::isfinite(f));
}

TEST(Mixer, BrokenClipDataNeverReachesTheOutput) {
  auto c = makeClip(2, 48000, 100, [](std::uint64_t f, std::uint32_t) { return f % 2 ? std::nanf("") : 0.25F; });
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  playOk(*m, c);
  const auto out = renderFrames(*m, 50);
  for (float f : out) CHECK(std::isfinite(f) && std::fabs(f) <= 1.0F);
}

TEST(Mixer, ClipMemoryIsReleasedOnTheGameThread) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  auto c = rampClip(2, 48000, 200);
  std::weak_ptr<assets::AudioClip> weak = c;
  playOk(*m, c);
  c.reset();                         // the mixer is now the only owner
  renderFrames(*m, 100);
  CHECK(!weak.expired());            // still playing
  renderFrames(*m, 400);             // finished: handed back to the retire list, NOT freed on the audio thread
  CHECK_MSG(!weak.expired(), "the audio thread must not drop the last reference");
  playOk(*m, rampClip(2, 48000, 10));  // any game-thread call collects the garbage
  CHECK(weak.expired());
}

TEST(Mixer, ThreadsCanDriveTheMixerConcurrently) {
  auto m = makeMixer(16);
  REQUIRE(m != nullptr);
  std::atomic<bool> stop{false};
  std::thread audioThread([&] {
    std::vector<float> buf(512 * 2);
    while (!stop.load()) {
      m->render(buf.data(), 512);
      for (float f : buf) {
        if (!std::isfinite(f) || std::fabs(f) > 1.0F) {
          fail(__FILE__, __LINE__, "bad sample from the audio thread");
          return;
        }
      }
    }
  });
  auto clip = rampClip(2, 44100, 3000);
  std::vector<VoiceId> ids;
  for (int i = 0; i < 4000; ++i) {
    PlayDesc d;
    d.loop = i % 3 == 0;
    d.pitch = 0.5F + static_cast<float>(i % 7) * 0.25F;
    d.spatial = i % 2 == 0;
    d.position = {static_cast<float>(i % 13), 0, -3};
    auto r = m->play(clip, d);
    if (r.ok()) ids.push_back(r.value());
    if (i % 5 == 0 && !ids.empty()) m->stop(ids[static_cast<std::size_t>(i) % ids.size()]);
    if (i % 7 == 0 && !ids.empty()) (void)m->setVolume(ids.back(), 0.5F);
    if (i % 11 == 0) m->setListener({static_cast<float>(i % 5), 0, 0}, {0, 0, -1}, {0, 1, 0});
    (void)m->isPlaying(ids.empty() ? VoiceId{} : ids.back());
    if (i % 500 == 0) std::this_thread::yield();
  }
  m->stopAll();
  stop.store(true);
  audioThread.join();
  CHECK(m->stats().framesRendered > 0);
  CHECK(m->stats().voicesStarted > 0);
}

TEST(Mixer, LoopSeamInterpolatesBetweenTheLastAndFirstFrame) {
  auto m = makeMixer();
  REQUIRE(m != nullptr);
  PlayDesc d;
  d.loop = true;
  d.pitch = 0.5F;  // half a frame per output frame: positions 99.5 / 0.5 straddle the seam
  playOk(*m, rampClip(2, 48000, 100), d);
  const auto out = renderFrames(*m, 205);
  // Output frame 199 is at source position 99.5: halfway between frame 99 (0.099) and frame 0 (0.0).
  CHECK(near(out[2 * 199], 0.0495, 1e-6));
  CHECK(near(out[2 * 200], 0.0, 1e-6));   // position 100 -> wrapped to 0
  CHECK(near(out[2 * 201], 0.0005, 1e-6));
}
