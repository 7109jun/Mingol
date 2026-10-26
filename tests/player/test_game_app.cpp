// SPDX-License-Identifier: GPL-2.0-only
// GameApp without a GPU: directory and archive sources, encryption, scripts running, scene loading.
#include "player_util.hpp"

using namespace mingol;
using namespace mingol::test;

namespace {

void checkRuns(std::unique_ptr<player::ContentSource> src) {
  auto app = player::GameApp::create(std::move(src), {});
  REQUIRE(app.ok());
  auto* h = app.value()->scene().find("H");
  REQUIRE(h != nullptr);
  auto* sc = h->getComponent<scripting::ScriptComponent>();
  REQUIRE(sc != nullptr);
  CHECK_EQ(sc->getInt("hp").value(), 42);
  CHECK_EQ(app.value()->project().name, std::string("Demo"));
  CHECK_EQ(app.value()->currentScene(), std::string("scenes/main.minseen"));
  for (int i = 0; i < 10; ++i) app.value()->tick(1.0F / 60.0F);
  CHECK_EQ(sc->getInt("ticks").value(), 10);
}

}  // namespace

TEST(Player, RunsFromDirectory) {
  TempDir dir;
  const auto mindev = buildProject(dir, nullptr, nullptr);
  REQUIRE(!mindev.empty());
  auto src = player::openDirectorySource(mindev);
  REQUIRE(src.ok());
  CHECK(src.value()->contains("project.mindev"));
  CHECK(src.value()->contains("scripts/hero.mins"));
  CHECK(!src.value()->contains("../x"));
  CHECK(!src.value()->read("../x").ok());
  checkRuns(std::move(src).value());
}

TEST(Player, RunsFromArchiveAndEncryptedArchive) {
  TempDir dir;
  const auto mindev = buildProject(dir, nullptr, nullptr);
  REQUIRE(!mindev.empty());

  auto plain = package::packProject(mindev, {});
  REQUIRE(plain.ok());
  const auto plainPath = dir.path / "plain.mingol";
  REQUIRE(fs::writeFileAtomic(plainPath, plain.value()).ok());
  auto src = player::openArchiveSource(plainPath);
  REQUIRE(src.ok());
  CHECK(!src.value()->list("scripts").empty());
  checkRuns(std::move(src).value());

  auto key = package::ArchiveKey::generate();
  REQUIRE(key.ok());
  package::ProjectPackOptions po;
  po.write.key = key.value();
  auto enc = package::packProject(mindev, po);
  REQUIRE(enc.ok());
  const auto encPath = dir.path / "enc.mingol";
  REQUIRE(fs::writeFileAtomic(encPath, enc.value()).ok());
  CHECK(!player::openArchiveSource(encPath).ok());  // no key
  auto wrong = package::ArchiveKey::generate();
  REQUIRE(wrong.ok());
  CHECK(!player::openArchiveSource(encPath, wrong.value()).ok());
  auto good = player::openArchiveSource(encPath, key.value());
  REQUIRE(good.ok());
  checkRuns(std::move(good).value());
}

TEST(Player, SceneSwitchAndErrors) {
  TempDir dir;
  const auto mindev = buildProject(dir, nullptr, nullptr);
  REQUIRE(!mindev.empty());
  writeText(dir.path / "scenes" / "other.minseen", "not json");
  auto src = player::openDirectorySource(mindev);
  REQUIRE(src.ok());
  auto app = player::GameApp::create(std::move(src).value(), {});
  REQUIRE(app.ok());
  // a broken scene file fails and leaves the running scene intact
  CHECK(!app.value()->loadScene("scenes/other.minseen").ok());
  CHECK(!app.value()->loadScene("scenes/missing.minseen").ok());
  CHECK(app.value()->scene().find("H") != nullptr);
  CHECK_EQ(app.value()->currentScene(), std::string("scenes/main.minseen"));
  CHECK(app.value()->loadScene("scenes/main.minseen").ok());
  CHECK(app.value()->scene().find("H") != nullptr);

  CHECK(!player::openDirectorySource(dir.path / "nope.mindev").ok());
  CHECK(!player::GameApp::create(nullptr, {}).ok());
}
