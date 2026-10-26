// SPDX-License-Identifier: GPL-2.0-only
// EditorDocument without a GPU: projects, scenes, selection, operations, history, play mode, scripts.
#include "editor_util.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/scripting/script_component.hpp"

using namespace mingol;
using namespace mingol::editor;
using namespace mingol::test;

namespace {

std::unique_ptr<EditorDocument> makeDoc(EditorConfig cfg = {}) {
  auto d = EditorDocument::create(nullptr, nullptr, cfg);
  if (!d.ok()) { fail(__FILE__, __LINE__, "create failed: " + d.error().describe()); return nullptr; }
  return std::move(d).value();
}

std::unique_ptr<EditorDocument> makeProject(const TempDir& dir, bool withScript = true) {
  auto d = makeDoc();
  if (!d) return nullptr;
  if (withScript) writeText(dir.path / "scripts" / "hero.mins", kHeroScript);
  auto r = d->createProject(dir.path, "Test");
  if (!r.ok()) { fail(__FILE__, __LINE__, "createProject failed: " + r.error().describe()); return nullptr; }
  return d;
}

}  // namespace

TEST(EditorDoc, NewDocumentHasDefaultsAndIsClean) {
  auto d = makeDoc();
  REQUIRE(d != nullptr);
  CHECK_EQ(d->allObjects().size(), 2u);
  CHECK(d->scene().find("Main Camera") != nullptr);
  CHECK(!d->canUndo());
  CHECK(!d->canRedo());
  CHECK(!d->hasProject());
  CHECK(!d->playing());
}

TEST(EditorDoc, CreateUndoRedoRestoresSelectionAndLabels) {
  auto d = makeDoc();
  REQUIRE(d != nullptr);
  auto* a = d->createEmpty("A");
  CHECK_EQ(d->selected(), a);
  CHECK(d->dirty());
  CHECK_EQ(d->undoLabel(), std::string("Create A"));
  auto* b = d->createEmpty("B", a);
  CHECK_EQ(b->parent(), a);
  CHECK_EQ(d->allObjects().size(), 4u);
  REQUIRE(d->undo().ok());
  CHECK(d->scene().find("B") == nullptr);
  CHECK(d->scene().find("A") != nullptr);
  CHECK_EQ(d->selected(), d->scene().find("A"));  // selection restored by index
  CHECK_EQ(d->redoLabel(), std::string("Create B"));
  REQUIRE(d->redo().ok());
  CHECK(d->scene().find("B") != nullptr);
  CHECK_EQ(d->selected(), d->scene().find("B"));
  REQUIRE(d->undo().ok());
  REQUIRE(d->undo().ok());
  CHECK(!d->dirty());
  CHECK(!d->undo().ok());  // nothing left
  CHECK_EQ(d->allObjects().size(), 2u);
}

TEST(EditorDoc, NewActionAfterUndoDropsRedoBranch) {
  auto d = makeDoc();
  d->createEmpty("A");
  d->createEmpty("B");
  REQUIRE(d->undo().ok());
  CHECK(d->canRedo());
  d->createEmpty("C");
  CHECK(!d->canRedo());
  CHECK(d->scene().find("B") == nullptr);
}

TEST(EditorDoc, PropertyEditsAreLiveUntilCommitted) {
  auto d = makeDoc();
  auto* go = d->createEmpty("P");
  auto props = describeTransform(*go);
  REQUIRE(d->setProperty(props[0], PropValue(Vec3{4, 5, 6})).ok());
  CHECK_NEAR(go->transform().localPosition().x, 4.0F, 1e-6F);
  CHECK(d->dirty());
  CHECK(d->commit("Move P"));
  CHECK_EQ(d->undoLabel(), std::string("Move P"));
  CHECK(!d->commit("again"));  // nothing changed
  REQUIRE(d->undo().ok());
  CHECK_NEAR(d->scene().find("P")->transform().localPosition().x, 0.0F, 1e-6F);
  REQUIRE(d->redo().ok());
  CHECK_NEAR(d->scene().find("P")->transform().localPosition().z, 6.0F, 1e-6F);
  CHECK(!d->setProperty(props[0], PropValue(Vec3{NAN, 0, 0})).ok());
}

TEST(EditorDoc, DeleteDuplicateReparentRename) {
  auto d = makeDoc();
  auto* root = d->createEmpty("Root");
  auto* child = d->createEmpty("Child", root);
  REQUIRE(d->addComponent(child, "Camera").ok());
  REQUIRE(d->addComponent(child, "Rigidbody").ok());

  auto dup = d->duplicate(root);
  REQUIRE(dup.ok());
  CHECK(dup.value() != root);
  CHECK_EQ(dup.value()->children().size(), 1u);
  auto* dupChild = d->scene().get(dup.value()->children()[0]);
  REQUIRE(dupChild != nullptr);
  CHECK(dupChild->getComponent<scene::CameraComponent>() != nullptr);
  CHECK(dupChild->getComponent<runtime::Rigidbody>() != nullptr);
  CHECK_EQ(d->selected(), dup.value());

  CHECK(!d->reparent(root, child).ok());  // cycle
  CHECK(!d->reparent(root, root).ok());
  REQUIRE(d->reparent(dupChild, nullptr).ok());
  CHECK(dupChild->parent() == nullptr);

  CHECK(!d->rename(root, "").ok());
  REQUIRE(d->rename(root, "Renamed").ok());
  CHECK(d->scene().find("Renamed") != nullptr);

  const std::size_t before = d->allObjects().size();
  REQUIRE(d->deleteObject(root).ok());
  CHECK_EQ(d->allObjects().size(), before - 2);  // root + child
  CHECK(d->selected() == nullptr || d->selected() != root);
  REQUIRE(d->undo().ok());
  CHECK_EQ(d->allObjects().size(), before);
}

TEST(EditorDoc, ComponentAddRemoveAndErrors) {
  auto d = makeDoc();
  auto* go = d->createEmpty("C");
  for (const std::string& t : addableComponentTypes()) CHECK_MSG(d->addComponent(go, t).ok(), "add " + t);
  CHECK(!d->addComponent(go, "Nope").ok());
  CHECK_EQ(go->componentCount(), addableComponentTypes().size());
  auto* cam = go->getComponent<scene::CameraComponent>();
  REQUIRE(d->removeComponent(go, cam).ok());
  CHECK(go->getComponent<scene::CameraComponent>() == nullptr);
  auto* other = d->createEmpty("O");
  CHECK(!d->removeComponent(other, go->getComponent<scene::LightComponent>()).ok());  // belongs elsewhere
  REQUIRE(d->undo().ok());  // Create O
  REQUIRE(d->undo().ok());  // Remove Camera
  CHECK(d->scene().find("C")->getComponent<scene::CameraComponent>() != nullptr);
}

TEST(EditorDoc, PrimitivesNeedARenderer) {
  auto d = makeDoc();
  CHECK(!d->createPrimitive(PrimitiveKind::Box).ok());
  CHECK(!d->instantiateModelAsset("x.glb").ok());
}

TEST(EditorDoc, HistoryIsBoundedByStepsAndBytes) {
  EditorConfig cfg;
  cfg.maxUndoSteps = 5;
  auto d = makeDoc(cfg);
  for (int i = 0; i < 20; ++i) d->createEmpty("o" + std::to_string(i));
  CHECK(d->historySize() <= 5);
  int undone = 0;
  while (d->canUndo()) { REQUIRE(d->undo().ok()); ++undone; }
  CHECK(undone <= 4);

  EditorConfig cfg2;
  cfg2.maxUndoBytes = 4096;
  auto d2 = makeDoc(cfg2);
  for (int i = 0; i < 100; ++i) d2->createEmpty("o" + std::to_string(i));
  CHECK(d2->historyBytes() <= 4096 + 16384);  // always keeps at least the current state
  CHECK(d2->historySize() < 100);
}

TEST(EditorDoc, ProjectCreateSaveReopenRoundTrip) {
  TempDir dir;
  {
    auto d = makeProject(dir);
    REQUIRE(d != nullptr);
    CHECK(d->hasProject());
    CHECK_EQ(d->scenePath(), std::string("scenes/main.minseen"));
    CHECK(!d->dirty());
    CHECK(std::filesystem::exists(dir.path / "Test.mindev"));
    CHECK(std::filesystem::exists(dir.path / "scenes" / "main.minseen"));
    auto* go = d->createEmpty("Saved");
    REQUIRE(d->addComponent(go, "Light").ok());
    CHECK(d->dirty());
    REQUIRE(d->saveScene().ok());
    CHECK(!d->dirty());
    auto* go2 = d->createEmpty("Unsaved");
    (void)go2;
    CHECK(d->dirty());
    REQUIRE(d->undo().ok());
    CHECK(!d->dirty());  // back at the saved state
  }
  auto d = makeDoc();
  REQUIRE(d->openProject(dir.path / "Test.mindev").ok());
  CHECK_EQ(d->project()->name, std::string("Test"));
  CHECK(d->scene().find("Saved") != nullptr);
  CHECK(d->scene().find("Saved")->getComponent<scene::LightComponent>() != nullptr);
  CHECK(d->scene().find("Unsaved") == nullptr);
  CHECK(!d->dirty());
  CHECK(!d->canUndo());
  // creating the same project again must not overwrite
  auto d2 = makeDoc();
  CHECK(!d2->createProject(dir.path, "Test").ok());
  CHECK(!d2->createProject(dir.path, "bad/name").ok());
}

TEST(EditorDoc, SceneSaveAsAddsToProjectAndOpenReplaces) {
  TempDir dir;
  auto d = makeProject(dir, false);
  REQUIRE(d != nullptr);
  d->createEmpty("InMain");
  REQUIRE(d->saveScene().ok());
  REQUIRE(d->newScene(false).ok());
  CHECK_EQ(d->allObjects().size(), 0u);
  CHECK(d->scenePath().empty());
  CHECK(!d->saveScene().ok());  // no path yet
  d->createEmpty("InLevel");
  CHECK(!d->saveSceneAs("../escape.minseen").ok());
  CHECK(!d->saveSceneAs("scenes/level.txt").ok());
  REQUIRE(d->saveSceneAs("scenes/level.minseen").ok());
  CHECK_EQ(d->project()->scenes.size(), 2u);
  REQUIRE(d->openScene("scenes/main.minseen").ok());
  CHECK(d->scene().find("InMain") != nullptr);
  CHECK(d->scene().find("InLevel") == nullptr);
  CHECK(!d->openScene("scenes/missing.minseen").ok());
  CHECK(!d->openScene("/abs.minseen").ok());
  // the project file on disk lists both scenes
  auto reloaded = package::loadMindev(dir.path / "Test.mindev");
  REQUIRE(reloaded.ok());
  CHECK_EQ(reloaded.value().scenes.size(), 2u);
}

TEST(EditorDoc, ScriptDiagnosticsAndHotReload) {
  TempDir dir;
  auto d = makeProject(dir);
  REQUIRE(d != nullptr);
  CHECK(d->scriptHost()->isScriptClass("Hero"));
  CHECK_EQ(d->scriptFiles().size(), 1u);

  auto* go = d->createEmpty("H");
  auto sc = d->addScript(go, "Hero");
  REQUIRE(sc.ok());
  CHECK(!d->addScript(go, "Nope").ok());
  REQUIRE(sc.value()->setInt("hp", 77).ok());

  // broken unsaved override: diagnostics with location, nothing changes
  auto diag = d->compileScripts({{"scripts/hero.mins", "class Hero extends Script {\n  public int hp = ;\n}\n"}});
  CHECK(!diag.ok);
  REQUIRE(!diag.errors.empty());
  CHECK_EQ(diag.errors[0].file, std::string("scripts/hero.mins"));
  CHECK_EQ(diag.errors[0].line, 2u);
  CHECK(d->scriptHost()->isScriptClass("Hero"));
  CHECK_EQ(go->getComponent<scripting::ScriptComponent>()->getInt("hp").value(), 77);

  // valid override: new field appears, old values migrate
  diag = d->compileScripts({{"scripts/hero.mins", "class Hero extends Script { public int hp = 1; public int mana = 5; }"}});
  CHECK(diag.ok);
  auto* s2 = go->getComponent<scripting::ScriptComponent>();
  CHECK_EQ(s2->getInt("hp").value(), 77);
  CHECK_EQ(s2->getInt("mana").value(), 5);

  // scripts survive a snapshot undo/redo cycle (fields are saved)
  d->commit("edit");
  REQUIRE(d->undo().ok());
  REQUIRE(d->redo().ok());
  CHECK_EQ(d->scene().find("H")->getComponent<scripting::ScriptComponent>()->getInt("hp").value(), 77);
}

TEST(EditorDoc, PlayModeRunsScriptsAndStopRestores) {
  TempDir dir;
  auto d = makeProject(dir);
  REQUIRE(d != nullptr);
  auto* go = d->createEmpty("H");
  REQUIRE(d->addScript(go, "Hero").ok());
  go->transform().setLocalPosition({1, 2, 3});
  d->commit("pos");
  d->select(go);
  const bool dirtyBefore = d->dirty();

  REQUIRE(d->play().ok());
  CHECK(d->playing());
  CHECK(!d->play().ok());
  CHECK(!d->canUndo());
  CHECK(!d->saveScene().ok());
  for (int i = 0; i < 5; ++i) d->tick(1.0F / 60.0F);
  auto* sc = d->scene().find("H")->getComponent<scripting::ScriptComponent>();
  CHECK_EQ(sc->getInt("ticks").value(), 5);
  d->scene().find("H")->transform().setLocalPosition({9, 9, 9});  // play-mode change
  d->createEmpty("PlayOnly");
  CHECK(d->playTime() > 0.07);

  d->setPaused(true);
  d->tick(1.0F);
  CHECK_EQ(sc->getInt("ticks").value(), 5);
  d->stepFrame();
  CHECK_EQ(sc->getInt("ticks").value(), 6);

  REQUIRE(d->stop().ok());
  CHECK(!d->playing());
  CHECK(d->scene().find("PlayOnly") == nullptr);
  CHECK_NEAR(d->scene().find("H")->transform().localPosition().x, 1.0F, 1e-6F);
  CHECK_EQ(d->scene().find("H")->getComponent<scripting::ScriptComponent>()->getInt("ticks").value(), 0);
  CHECK_EQ(d->selected(), d->scene().find("H"));
  CHECK_EQ(d->dirty(), dirtyBefore);
  CHECK(!d->stop().ok());
  d->tick(1.0F);  // no-op outside play mode
}

TEST(EditorDoc, SetActiveAndEnvironmentSurviveUndo) {
  auto d = makeDoc();
  auto* go = d->createEmpty("X");
  REQUIRE(d->setActive(go, false).ok());
  CHECK(!d->scene().find("X")->activeSelf());
  REQUIRE(d->undo().ok());
  CHECK(d->scene().find("X")->activeSelf());
}
