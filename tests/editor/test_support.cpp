// SPDX-License-Identifier: GPL-2.0-only
// Console, profiler, euler conversion and property reflection (no GPU, no document).
#include <cmath>
#include <thread>

#include "mingol/editor/console.hpp"
#include "mingol/editor/euler.hpp"
#include "mingol/editor/profiler.hpp"
#include "mingol/editor/properties.hpp"
#include "mingol/scene/components.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::editor;

TEST(EditorConsole, CapturesLoggerOutputAndFilters) {
  auto c = std::make_shared<ConsoleLog>(100);
  c->install(c);
  Logger::get().log(LogLevel::Warn, "cat", "hello world");
  Logger::get().log(LogLevel::Error, "other", "boom");
  ConsoleLog::uninstall(c);
  Logger::get().log(LogLevel::Error, "other", "after uninstall");
  CHECK_EQ(c->size(), 2u);
  ConsoleFilter f;
  f.minLevel = LogLevel::Error;
  CHECK_EQ(c->filtered(f).size(), 1u);
  f = {};
  f.text = "HELLO";
  const auto r = c->filtered(f);
  REQUIRE(r.size() == 1);
  CHECK_EQ(r[0].category, std::string("cat"));
  CHECK_EQ(c->count(LogLevel::Warn), 1u);
}

TEST(EditorConsole, CapacityDropsOldestAndCountsTotal) {
  ConsoleLog c(3);
  for (int i = 0; i < 10; ++i) c.add(LogLevel::Info, "t", "m" + std::to_string(i));
  CHECK_EQ(c.size(), 3u);
  CHECK_EQ(c.totalAdded(), 10u);
  const auto all = c.filtered({});
  REQUIRE(all.size() == 3);
  CHECK_EQ(all[0].message, std::string("m7"));
  CHECK_EQ(all[2].message, std::string("m9"));
  c.clear();
  CHECK_EQ(c.size(), 0u);
}

TEST(EditorConsole, ThreadSafe) {
  ConsoleLog c(100000);
  std::vector<std::thread> th;
  for (int t = 0; t < 4; ++t)
    th.emplace_back([&c, t] { for (int i = 0; i < 500; ++i) c.add(LogLevel::Info, "t", std::to_string(t) + ":" + std::to_string(i)); });
  for (auto& t : th) t.join();
  CHECK_EQ(c.size(), 2000u);
}

TEST(EditorProfiler, RecordsScopesCountersAndHistory) {
  Profiler p;
  for (int i = 0; i < 3; ++i) {
    p.beginFrame();
    p.add("update", 2.0);
    p.add("update", 1.0);
    p.add("render", 4.0);
    p.setCounter("objects", 7 + i);
    p.endFrame();
  }
  CHECK_EQ(p.frames(), 3u);
  CHECK_EQ(p.frameHistory().size(), 3u);
  CHECK_EQ(p.counter("objects"), 9.0);
  const auto s = p.scopes();
  REQUIRE(s.size() == 2);
  for (const auto& sc : s) {
    if (sc.name == "update") { CHECK_NEAR(sc.lastMs, 3.0, 1e-9); CHECK_NEAR(sc.avgMs, 3.0, 1e-9); }
    if (sc.name == "render") CHECK_NEAR(sc.maxMs, 4.0, 1e-9);
  }
  REQUIRE(p.scopeHistory("update") != nullptr);
  CHECK(p.scopeHistory("nope") == nullptr);
  CHECK(p.fps() > 0.0);
}

TEST(EditorProfiler, HistoryIsBounded) {
  Profiler p;
  for (std::size_t i = 0; i < Profiler::kHistory + 50; ++i) { p.beginFrame(); p.endFrame(); }
  CHECK_EQ(p.frameHistory().size(), Profiler::kHistory);
}

TEST(EditorEuler, RoundTripsAndMatchesAxes) {
  const Vec3 cases[] = {{0, 0, 0}, {30, 45, 60}, {-80, 170, 10}, {10, -120, -45}, {89, 0, 0}, {0, 90, 0}};
  for (const Vec3& e : cases) {
    const Quat q = fromEulerDegrees(e);
    const Quat q2 = fromEulerDegrees(toEulerDegrees(q));
    const float d = std::fabs(q.x * q2.x + q.y * q2.y + q.z * q2.z + q.w * q2.w);  // |dot| = 1 -> same rotation
    CHECK_MSG(d > 0.99999F, "euler roundtrip for " + std::to_string(e.x) + "," + std::to_string(e.y) + "," + std::to_string(e.z));
  }
  const Quat y90 = fromEulerDegrees({0, 90, 0});
  const Quat ref = Quat::fromAxisAngle({0, 1, 0}, radians(90.0F));
  CHECK_NEAR(std::fabs(y90.x * ref.x + y90.y * ref.y + y90.z * ref.z + y90.w * ref.w), 1.0F, 1e-6F);
}

TEST(EditorProperties, TransformEditsApplyAndValidate) {
  scene::Scene s;
  auto* go = s.createObject("a");
  auto props = describeTransform(*go);
  REQUIRE(props.size() == 3);
  CHECK_EQ(props[0].name, std::string("Position"));
  REQUIRE(props[0].set(PropValue(Vec3{1, 2, 3})).ok());
  CHECK_NEAR(go->transform().localPosition().y, 2.0F, 1e-6F);
  REQUIRE(props[1].set(PropValue(Vec3{0, 90, 0})).ok());
  const Vec3 e = std::get<Vec3>(props[1].get());
  CHECK_NEAR(e.y, 90.0F, 1e-3F);
  CHECK(!props[0].set(PropValue(Vec3{NAN, 0, 0})).ok());
  CHECK(!props[0].set(PropValue(std::string("x"))).ok());
  CHECK_NEAR(go->transform().localPosition().x, 1.0F, 1e-6F);  // rejected edits change nothing
}

TEST(EditorProperties, ComponentsExposeTypedRangeCheckedValues) {
  scene::Scene s;
  auto* go = s.createObject("c");
  auto* cam = go->addComponent<scene::CameraComponent>();
  const PropertyEnv env;
  auto props = describeComponent(*cam, env);
  Property* fov = nullptr;
  for (auto& p : props) if (p.name == "Field of View") fov = &p;
  REQUIRE(fov != nullptr);
  REQUIRE(fov->set(PropValue(90.0)).ok());
  CHECK_NEAR(cam->fovYRadians, radians(90.0F), 1e-5F);
  CHECK(!fov->set(PropValue(500.0)).ok());
  CHECK(!fov->set(PropValue(NAN)).ok());
  CHECK_NEAR(std::get<double>(fov->get()), 90.0, 1e-3);
}

TEST(EditorProperties, AddableTypesAreDescribable) {
  CHECK(!addableComponentTypes().empty());
  CHECK_EQ(toDisplayString(PropValue(true)), std::string("true"));
  CHECK_EQ(toDisplayString(PropValue(std::int64_t{5})), std::string("5"));
}
