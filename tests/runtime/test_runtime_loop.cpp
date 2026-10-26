// SPDX-License-Identifier: GPL-2.0-only
#include <string>
#include <vector>

#include "mingol/runtime/runtime.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::runtime;
using namespace mingol::scene;
using mingol::test::fail;

namespace {

struct Trace final : Component {
  const char* typeName() const override { return "Trace"; }
  std::vector<std::string>* log = nullptr;
  int fixed = 0, updates = 0, lates = 0;
  float lastDt = -1;
  void onFixedUpdate(float) override { ++fixed; if (log) log->push_back("F"); }
  void onUpdate(float dt) override { ++updates; lastDt = dt; if (log) log->push_back("U"); }
  void onLateUpdate(float) override { ++lates; if (log) log->push_back("L"); }
};

std::unique_ptr<Runtime> make(RuntimeDesc d = {}) {
  auto r = Runtime::create(d);
  if (!r.ok()) { fail(__FILE__, __LINE__, r.error().describe()); return nullptr; }
  return std::move(r).value();
}

}  // namespace

TEST(RuntimeLoop, FixedStepsAccumulateAndRemainderCarriesOver) {
  auto rt = make();
  REQUIRE(rt != nullptr);
  auto* t = rt->scene().createObject("t")->addComponent<Trace>();
  const float s = 1.0F / 60.0F;
  rt->tick(s * 0.5F);
  CHECK_EQ(t->fixed, 0);
  rt->tick(s * 0.5F);
  CHECK_EQ(t->fixed, 1);  // two halves make one step
  rt->tick(s * 2.5F);
  CHECK_EQ(t->fixed, 3);
  rt->tick(s * 0.5F);
  CHECK_EQ(t->fixed, 4);  // 0.5 left over from before + 0.5
  CHECK_EQ(t->updates, 4);
  CHECK_EQ(t->lates, 4);
  CHECK_EQ(rt->stats().fixedSteps, std::uint64_t{4});
  CHECK_EQ(rt->stats().frames, std::uint64_t{4});
}

TEST(RuntimeLoop, PhaseOrderWithinAFrame) {
  auto rt = make();
  REQUIRE(rt != nullptr);
  std::vector<std::string> log;
  auto* t = rt->scene().createObject("t")->addComponent<Trace>();
  t->log = &log;
  rt->tick(2.0F / 60.0F + 1e-4F);
  const std::vector<std::string> expect{"F", "F", "U", "L"};
  CHECK(log == expect);
}

TEST(RuntimeLoop, SpiralOfDeathIsCappedAndCounted) {
  RuntimeDesc d;
  d.maxFixedStepsPerFrame = 4;
  d.maxFrameTime = 10.0F;
  auto rt = make(d);
  REQUIRE(rt != nullptr);
  auto* t = rt->scene().createObject("t")->addComponent<Trace>();
  rt->tick(1.0F);  // would be 60 steps
  CHECK_EQ(t->fixed, 4);
  CHECK_EQ(rt->stats().droppedFixedSteps, std::uint64_t{1});
  rt->tick(1.0F / 60.0F);  // backlog was dropped, so we are back to normal
  CHECK_EQ(t->fixed, 5);
}

TEST(RuntimeLoop, LongFramesAreClamped) {
  auto rt = make();  // maxFrameTime 0.25 s = 15 steps
  REQUIRE(rt != nullptr);
  auto* t = rt->scene().createObject("t")->addComponent<Trace>();
  rt->tick(30.0F);
  CHECK_EQ(t->fixed, 8);  // clamped to 0.25 s, then limited by maxFixedStepsPerFrame
  CHECK_NEAR(t->lastDt, 0.25F, 1e-6F);
}

TEST(RuntimeLoop, TimeScaleZeroPausesButStillTicksFrames) {
  auto rt = make();
  REQUIRE(rt != nullptr);
  auto* t = rt->scene().createObject("t")->addComponent<Trace>();
  rt->setTimeScale(0.0F);
  rt->tick(1.0F / 30.0F);
  CHECK_EQ(t->fixed, 0);
  CHECK_EQ(t->updates, 1);
  CHECK_NEAR(t->lastDt, 0.0F, 1e-9F);
  rt->setTimeScale(2.0F);
  rt->tick(1.0F / 60.0F);  // 2x: two steps
  CHECK_EQ(t->fixed, 2);
  rt->setTimeScale(-1.0F);  // invalid values are ignored
  CHECK_NEAR(rt->timeScale(), 2.0F, 1e-9F);
  rt->setTimeScale(std::nanf(""));
  CHECK_NEAR(rt->timeScale(), 2.0F, 1e-9F);
}

TEST(RuntimeLoop, InvalidTimesAreSanitised) {
  auto rt = make();
  REQUIRE(rt != nullptr);
  auto* t = rt->scene().createObject("t")->addComponent<Trace>();
  rt->tick(-1.0F);
  rt->tick(std::nanf(""));
  rt->tick(INFINITY);  // clamped like any long frame
  CHECK_EQ(t->updates, 3);
  CHECK(t->lastDt >= 0.0F);
}

TEST(RuntimeLoop, InvalidDescriptorsAreRejected) {
  RuntimeDesc d;
  d.fixedStep = 0.0F;
  CHECK(!Runtime::create(d).ok());
  d = {};
  d.maxFixedStepsPerFrame = 0;
  CHECK(!Runtime::create(d).ok());
  d = {};
  d.maxFrameTime = -1.0F;
  CHECK(!Runtime::create(d).ok());
}

TEST(RuntimeLoop, RunDrivesFramesUntilToldToStop) {
  auto rt = make();
  REQUIRE(rt != nullptr);
  int n = 0, cb = 0;
  const auto frames = rt->run([&] { return n++ < 5; }, [&](float) { ++cb; });
  CHECK_EQ(frames, std::uint64_t{5});
  CHECK_EQ(cb, 5);
  CHECK_EQ(rt->stats().frames, std::uint64_t{5});
}
