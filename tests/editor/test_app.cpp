// SPDX-License-Identifier: GPL-2.0-only
// The editor application driven by synthetic input (no GPU, no window).
#include "app_util.hpp"
#include "mingol/package/archive.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/scripting/script_component.hpp"

using namespace mingol;
using namespace mingol::editor;
using namespace mingol::test;
using platform::Key;
using platform::Modifiers;

TEST(EditorApp, FirstFramesAreCleanAndAllPanelsExist) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  CHECK(d.app->gui().errors().empty());
  for (const char* p : {"Viewport", "Hierarchy", "Inspector", "Console", "Assets", "Scripts", "Profiler"}) CHECK_MSG(d.app->dock().contains(p), p);
  CHECK(d.app->dock().valid());
  CHECK(d.app->viewportRect().width() > 100);
  CHECK(d.app->gui().drawList().vertices().size() > 500);
  CHECK(d.rect("btn:Play").has_value());
  CHECK(d.rect("node:Main Camera").has_value());
  d.screenshot("editor_default");
}

TEST(EditorApp, MenuCreatesObjectAndSelectsIt) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  const std::size_t before = d.app->document().allObjects().size();
  REQUIRE(d.click("menu:GameObject"));
  REQUIRE(d.click("item:Create Empty"));
  CHECK_EQ(d.app->document().allObjects().size(), before + 1);
  REQUIRE(d.app->document().selected() != nullptr);
  CHECK_EQ(d.app->document().selected()->name(), std::string("GameObject"));
  CHECK(d.rect("node:GameObject").has_value());
  CHECK(d.app->gui().errors().empty());
}

TEST(EditorApp, PlayButtonTogglesPlayMode) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  REQUIRE(d.click("btn:Play"));
  CHECK(d.app->document().playing());
  d.frame(5);
  CHECK(d.app->document().playTime() > 0.05);
  REQUIRE(d.click("btn:Stop"));
  CHECK(!d.app->document().playing());
}

TEST(EditorApp, HierarchyClickSelectsAndDragReparents) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  doc.createEmpty("A");
  doc.createEmpty("B");
  d.frame(2);
  REQUIRE(d.click("node:Main Camera"));
  CHECK_EQ(doc.selected(), doc.scene().find("Main Camera"));

  const auto a = d.rect("node:A"), b = d.rect("node:B");
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  d.drag(AppDriver::centre(*a), AppDriver::centre(*b));
  CHECK_EQ(doc.scene().find("A")->parent(), doc.scene().find("B"));
  CHECK(doc.canUndo());
  REQUIRE(doc.undo().ok());
  CHECK(doc.scene().find("A")->parent() == nullptr);
  CHECK(d.app->gui().errors().empty());
}

TEST(EditorApp, ShortcutsUndoRedoDuplicateDelete) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  doc.createEmpty("X");
  d.frame(2);
  d.key(Key::Z, Modifiers::Control);
  d.frame(2);
  CHECK(doc.scene().find("X") == nullptr);
  d.key(Key::Y, Modifiers::Control);
  d.frame(2);
  CHECK(doc.scene().find("X") != nullptr);
  d.key(Key::D, Modifiers::Control);
  d.frame(2);
  CHECK_EQ(doc.scene().rootObjects().size(), 4u);
  d.key(Key::Delete);
  d.frame(2);
  CHECK_EQ(doc.scene().rootObjects().size(), 3u);
}

TEST(EditorApp, ViewportPickingSelectsCameraAndLightMarkers) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  const Vec2 vs{std::floor(d.app->viewportRect().width()), std::floor(d.app->viewportRect().height())};
  const auto cam = d.app->camera().toCamera(vs.x / vs.y);
  const Vec3 camPos = doc.scene().find("Main Camera")->transform().worldPosition();
  const auto p = projectToScreen(cam, camPos, vs);
  REQUIRE(p.has_value());
  CHECK_EQ(d.app->pickObject(*p, vs), doc.scene().find("Main Camera"));
  // clicking in the viewport selects it
  doc.select(nullptr);
  d.clickAt(d.app->viewportRect().min() + *p);
  CHECK_EQ(doc.selected(), doc.scene().find("Main Camera"));
  // empty corner picks nothing
  CHECK(d.app->pickObject({2, 2}, vs) == nullptr);
}

TEST(EditorApp, GizmoTranslateDragMovesAlongAxisAndUndoes) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  auto* light = doc.scene().find("Directional Light");
  doc.select(light);
  d.app->setGizmoMode(editor::GizmoMode::Translate);
  d.frame(2);
  const Rect vr = d.app->viewportRect();
  const Vec2 vs{std::floor(vr.width()), std::floor(vr.height())};
  const auto cam = d.app->camera().toCamera(vs.x / vs.y);
  const Vec3 o = light->transform().worldPosition();
  const float len = length(o - cam.position) * 0.18F;
  const auto o2 = projectToScreen(cam, o, vs), x2 = projectToScreen(cam, o + Vec3{len, 0, 0}, vs);
  REQUIRE(o2.has_value());
  REQUIRE(x2.has_value());
  const Vec2 mid = vr.min() + (*o2 + *x2) * 0.5F;
  const Vec2 dirPx = *x2 - *o2;
  d.drag(mid, mid + dirPx * 0.8F);
  const Vec3 after = light->transform().worldPosition();
  CHECK_MSG(after.x > o.x + 0.2F, "moved along +X, x=" + std::to_string(after.x));
  CHECK_NEAR(after.y, o.y, 1e-3F);
  CHECK_NEAR(after.z, o.z, 1e-3F);
  CHECK(doc.undoLabel().rfind("Move", 0) == 0);
  REQUIRE(doc.undo().ok());
  CHECK_NEAR(doc.scene().find("Directional Light")->transform().worldPosition().x, o.x, 1e-4F);
}

TEST(EditorApp, GizmoEscapeCancelsDrag) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  auto* light = doc.scene().find("Directional Light");
  doc.select(light);
  d.frame(2);
  const Rect vr = d.app->viewportRect();
  const Vec2 vs{std::floor(vr.width()), std::floor(vr.height())};
  const auto cam = d.app->camera().toCamera(vs.x / vs.y);
  const Vec3 o = light->transform().worldPosition();
  const float len = length(o - cam.position) * 0.18F;
  const auto o2 = projectToScreen(cam, o, vs), x2 = projectToScreen(cam, o + Vec3{len, 0, 0}, vs);
  const Vec2 mid = vr.min() + (*o2 + *x2) * 0.5F;
  d.drag(mid, mid + (*x2 - *o2) * 0.8F, 0, false);
  CHECK(light->transform().worldPosition().x > o.x + 0.1F);
  d.key(Key::Escape);
  d.frame(2);
  d.up();
  d.frame(2);
  CHECK_NEAR(doc.scene().find("Directional Light")->transform().worldPosition().x, o.x, 1e-4F);
  CHECK(!doc.canUndo());
}

TEST(EditorApp, CameraControlsFromMouse) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  const Vec2 c = AppDriver::centre(d.app->viewportRect());
  const float dist0 = d.app->camera().distance;
  d.move(c);
  d.frame();
  d.wheel(3);
  d.frame(2);
  CHECK(d.app->camera().distance < dist0);
  const float yaw0 = d.app->camera().yaw;
  d.mods = Modifiers::Alt;
  d.app->feed(platform::KeyEvent{Key::LeftAlt, 0, platform::KeyAction::Press, Modifiers::Alt});
  d.drag(c, c + Vec2{80, 0});
  d.app->feed(platform::KeyEvent{Key::LeftAlt, 0, platform::KeyAction::Release, Modifiers::None});
  d.mods = Modifiers::None;
  CHECK(std::fabs(d.app->camera().yaw - yaw0) > 0.1F);
  const Vec3 t0 = d.app->camera().target;
  d.drag(c, c + Vec2{60, 20}, 2);
  CHECK(length(d.app->camera().target - t0) > 0.05F);
  // keys W/E/R switch tools while hovering
  d.move(c);
  d.frame();
  d.key(Key::E);
  d.frame();
  CHECK(d.app->gizmoMode() == editor::GizmoMode::Rotate);
  d.key(Key::R);
  d.frame();
  CHECK(d.app->gizmoMode() == editor::GizmoMode::Scale);
  d.key(Key::W);
  d.frame();
  CHECK(d.app->gizmoMode() == editor::GizmoMode::Translate);
}

TEST(EditorApp, InspectorEditsPropertiesWithUndo) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  doc.select(doc.scene().find("Main Camera"));
  d.frame(3);
  CHECK(d.rect("btn:Add Component").has_value());
  REQUIRE(d.click("btn:Add Component"));
  d.frame(2);
  REQUIRE(d.click("item:Rigidbody"));
  CHECK(doc.scene().find("Main Camera")->getComponent<runtime::Rigidbody>() != nullptr);
  REQUIRE(doc.undo().ok());
  CHECK(doc.scene().find("Main Camera")->getComponent<runtime::Rigidbody>() == nullptr);
}

TEST(EditorApp, NewProjectDialogCreatesProject) {
  TempDir dir;
  AppDriver d;
  REQUIRE(d.app != nullptr);
  REQUIRE(d.click("menu:File"));
  REQUIRE(d.click("item:New Project..."));
  d.frame(2);
  REQUIRE(d.app->dialogOpen());
  REQUIRE(d.click("input:Name##np"));
  d.key(Key::A, Modifiers::Control);
  d.type("Demo");
  d.frame();
  REQUIRE(d.click("input:Directory##np"));
  d.key(Key::A, Modifiers::Control);
  d.type(dir.path.string());
  d.frame();
  d.screenshot("editor_new_project");
  REQUIRE(d.click("btn:Create"));
  d.frame(2);
  CHECK(!d.app->dialogOpen());
  CHECK(d.app->document().hasProject());
  CHECK(std::filesystem::exists(dir.path / "Demo" / "Demo.mindev"));
  CHECK_EQ(d.app->document().scenePath(), std::string("scenes/main.minseen"));
}

TEST(EditorApp, UnsavedChangesAskBeforeDiscarding) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  d.app->document().createEmpty("Dirty");
  d.frame(2);
  d.app->requestQuit();
  d.frame(3);
  CHECK(d.app->dialogOpen());
  CHECK(!d.app->quitRequested());
  REQUIRE(d.click("btn:Cancel"));
  CHECK(!d.app->dialogOpen());
  CHECK(!d.app->quitRequested());
  d.app->requestQuit();
  d.frame(3);
  REQUIRE(d.click("btn:Discard"));
  CHECK(d.app->quitRequested());
}

TEST(EditorApp, ScriptsCompileWithDiagnosticsAndHotReload) {
  TempDir dir;
  writeText(dir.path / "scripts" / "hero.mins", kHeroScript);
  AppDriver d;
  REQUIRE(d.app != nullptr);
  REQUIRE(d.app->document().createProject(dir.path, "S").ok());
  REQUIRE(d.app->openScript("scripts/hero.mins").ok());
  d.frame(3);
  CodeEditor* ed = d.app->scriptEditor("scripts/hero.mins");
  REQUIRE(ed != nullptr);
  CHECK(!ed->modified());
  // break the file in the editor buffer (unsaved) and compile
  std::string broken = ed->text();
  broken.replace(broken.find("hp = 10"), 7, "hp = ;");
  ed->setText(broken);
  d.app->compileScripts();
  CHECK(!ed->diagnostics().empty());
  CHECK(!d.app->dialogErrors().empty());
  CHECK(d.app->document().scriptHost()->isScriptClass("Hero"));  // the old program survives
  d.frame(3);
  d.screenshot("editor_script_error");
  // fix and save
  std::string fixed = ed->text();
  fixed.replace(fixed.find("hp = ;"), 6, "hp = 99");
  ed->setText(fixed);
  d.frame(2);
  REQUIRE(d.app->saveActiveScript().ok());
  CHECK(ed->diagnostics().empty());
  auto* go = d.app->document().createEmpty("H");
  auto sc = d.app->document().addScript(go, "Hero");
  REQUIRE(sc.ok());
  CHECK_EQ(sc.value()->getInt("hp").value(), 99);
}

TEST(EditorApp, NewScriptDialogWritesFileAndOpensIt) {
  TempDir dir;
  AppDriver d;
  REQUIRE(d.app != nullptr);
  REQUIRE(d.app->document().createProject(dir.path, "S").ok());
  d.frame(2);
  REQUIRE(d.click("menu:Scripts"));
  REQUIRE(d.click("item:New Script..."));
  d.frame(2);
  REQUIRE(d.click("input:Class name##ns"));
  d.key(Key::A, Modifiers::Control);
  d.type("Spinner");
  d.frame();
  REQUIRE(d.click("btn:Create"));
  d.frame(2);
  CHECK(std::filesystem::exists(dir.path / "scripts" / "Spinner.mins"));
  CHECK(d.app->scriptEditor("scripts/Spinner.mins") != nullptr);
  CHECK(d.app->document().scriptHost()->isScriptClass("Spinner"));
  d.frame(3);
  d.screenshot("editor_scripts");
}

TEST(EditorApp, BuildDialogPacksProject) {
  TempDir dir;
  AppDriver d;
  REQUIRE(d.app != nullptr);
  REQUIRE(d.app->document().createProject(dir.path, "B").ok());
  d.frame(2);
  REQUIRE(d.click("menu:File"));
  REQUIRE(d.click("item:Build..."));
  d.frame(2);
  REQUIRE(d.click("btn:Build"));
  d.frame(2);
  const auto out = dir.path / "build" / "game.mingol";
  CHECK(std::filesystem::exists(out));
  auto ar = package::ArchiveReader::openFile(out);
  REQUIRE(ar.ok());
  CHECK(ar.value().contains("project.mindev"));
  CHECK(ar.value().contains("scenes/main.minseen"));
}

TEST(EditorApp, ConsoleShowsEngineAndEditorMessages) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  d.app->document().console().add(LogLevel::Error, "test", "visible error line");
  d.frame(3);
  CHECK_EQ(d.app->document().console().count(LogLevel::Error), 1u);
  CHECK(d.app->gui().errors().empty());
}

TEST(EditorApp, RandomInputNeverBreaksTheFrame) {
  AppDriver d;
  REQUIRE(d.app != nullptr);
  std::mt19937 rng(7);
  const Key keys[] = {Key::A, Key::Z, Key::Y, Key::D, Key::Delete, Key::Escape, Key::Enter, Key::W, Key::E, Key::R, Key::F, Key::Backspace, Key::S, Key::P};
  for (int i = 0; i < 400; ++i) {
    switch (rng() % 7) {
      case 0: d.move({static_cast<float>(rng() % 1280), static_cast<float>(rng() % 720)}); break;
      case 1: d.down(static_cast<int>(rng() % 3)); break;
      case 2: d.up(static_cast<int>(rng() % 3)); break;
      case 3: d.wheel(static_cast<float>(static_cast<int>(rng() % 5) - 2)); break;
      case 4: d.key(keys[rng() % 14], (rng() & 1) != 0 ? Modifiers::Control : Modifiers::None); break;
      case 5: d.type("ab"); break;
      default: break;
    }
    d.frame();
    REQUIRE(d.app->gui().errors().empty());
  }
  if (d.app->document().playing()) REQUIRE(d.app->document().stop().ok());
  d.app->document().commit("final");
  CHECK(d.app->dock().valid());
}
