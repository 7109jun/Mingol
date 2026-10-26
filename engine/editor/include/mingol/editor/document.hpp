// SPDX-License-Identifier: GPL-2.0-only
// EditorDocument: everything the editor edits and every operation on it, without any UI.
//
//  * owns the Runtime (scene + physics + audio), the asset table, the script host and the project
//  * selection, scene operations (create/duplicate/delete/reparent/components)
//  * undo/redo by scene snapshots (.minseen text): simple and exact, memory bounded by a byte cap
//  * play mode: snapshot -> run -> restore
//
// Threading: single threaded (the editor main thread).
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mingol/assets/audio.hpp"
#include "mingol/assets/model.hpp"
#include "mingol/core/error.hpp"
#include "mingol/editor/console.hpp"
#include "mingol/editor/properties.hpp"
#include "mingol/package/mindev.hpp"
#include "mingol/renderer/renderer.hpp"
#include "mingol/rhi/device.hpp"
#include "mingol/runtime/model_instance.hpp"
#include "mingol/runtime/runtime.hpp"
#include "mingol/sceneio/scene_io.hpp"
#include "mingol/sceneio/standard_assets.hpp"
#include "mingol/scripting/script_host.hpp"

namespace mingol::editor {

enum class AssetType : std::uint8_t { Model, Audio, Texture, Script, Scene, Other };
const char* toString(AssetType t) noexcept;

struct AssetEntry {
  std::string path;  // relative to the project root, '/' separators
  AssetType type = AssetType::Other;
  std::uint64_t size = 0;
};

struct EditorConfig {
  runtime::RuntimeDesc runtime;
  std::size_t maxUndoBytes = 64ull << 20;
  std::size_t maxUndoSteps = 200;
};

enum class PrimitiveKind : std::uint8_t { Box, Sphere, Plane };

class EditorDocument {
 public:
  // `device`/`renderer` may be null (no GPU): mesh/material assets are then unavailable and scenes
  // that reference them fail to load with a clear error.
  static Result<std::unique_ptr<EditorDocument>> create(rhi::Device* device, renderer::Renderer* renderer, const EditorConfig& cfg = {});
  ~EditorDocument();
  EditorDocument(const EditorDocument&) = delete;
  EditorDocument& operator=(const EditorDocument&) = delete;

  // ---- services ----
  runtime::Runtime& runtime() { return *runtime_; }
  scene::Scene& scene() { return runtime_->scene(); }
  const sceneio::AssetTable& assets() const { return assets_; }
  sceneio::AssetTable& assets() { return assets_; }
  scripting::ScriptHost* scriptHost() { return host_.get(); }
  ConsoleLog& console() { return *console_; }
  std::shared_ptr<ConsoleLog> consolePtr() { return console_; }
  PropertyEnv propertyEnv() const { return PropertyEnv{&assets_}; }

  // ---- project ----
  // Creates <dir>/<name>.mindev plus assets/, scripts/, scenes/ and an empty scenes/main.minseen,
  // then opens it. Fails if the .mindev already exists.
  Result<void> createProject(const std::filesystem::path& dir, const std::string& name);
  Result<void> openProject(const std::filesystem::path& mindev);
  Result<void> saveProject();
  const package::ProjectFile* project() const { return project_ ? &*project_ : nullptr; }
  const std::filesystem::path& projectRoot() const { return root_; }
  const std::filesystem::path& projectFile() const { return projectFile_; }
  bool hasProject() const { return project_.has_value(); }

  // Rebuilds the asset table: built-ins (needs a renderer) + glTF models and audio under assetDirs.
  // Problems with single files are logged and the file is skipped.
  Result<void> rescanAssets();
  const std::vector<AssetEntry>& assetList() const { return assetList_; }
  // Registers a model under `path` (name prefix of all its sub-assets); used by rescanAssets and tests.
  Result<void> registerModel(const std::string& path, assets::ModelData model);
  // Instantiates a registered model as a new object tree under `parent` (undoable).
  Result<scene::GameObject*> instantiateModelAsset(const std::string& path, scene::GameObject* parent = nullptr);

  // ---- scenes ----
  // `withDefaults` adds a Main Camera and a Directional Light.
  Result<void> newScene(bool withDefaults = true);
  Result<void> openScene(const std::string& relPath);
  Result<void> saveScene();                              // needs a scene path (see saveSceneAs)
  Result<void> saveSceneAs(const std::string& relPath);  // adds it to the project's scene list
  const std::string& scenePath() const { return scenePath_; }
  bool dirty() const;

  // ---- selection ----
  scene::GameObject* selected() const;
  void select(scene::GameObject* object);
  // Index of `object` in save order (parents before children, depth first), -1 if not in the scene.
  int objectIndex(const scene::GameObject* object) const;
  scene::GameObject* objectAtIndex(int index) const;
  std::vector<scene::GameObject*> allObjects() const;  // save order

  // ---- scene operations (each is one undo step; they return the affected object) ----
  scene::GameObject* createEmpty(const std::string& name = "GameObject", scene::GameObject* parent = nullptr);
  Result<scene::GameObject*> createPrimitive(PrimitiveKind kind, scene::GameObject* parent = nullptr);
  scene::GameObject* createCamera(scene::GameObject* parent = nullptr);
  scene::GameObject* createLight(renderer::LightType type, scene::GameObject* parent = nullptr);
  Result<scene::GameObject*> duplicate(scene::GameObject* object);
  Result<void> deleteObject(scene::GameObject* object);
  Result<void> reparent(scene::GameObject* object, scene::GameObject* newParent, bool keepWorld = true);
  Result<void> rename(scene::GameObject* object, const std::string& name);
  Result<void> setActive(scene::GameObject* object, bool active);
  Result<scene::Component*> addComponent(scene::GameObject* object, const std::string& typeName);
  Result<scripting::ScriptComponent*> addScript(scene::GameObject* object, const std::string& className);
  Result<void> removeComponent(scene::GameObject* object, scene::Component* component);

  // ---- property editing ----
  // Applies the value (live, no history entry). Call commit() when the edit is finished.
  Result<void> setProperty(const Property& property, const PropValue& value);

  // ---- history ----
  // Records the current scene as a new undo step if it differs from the last recorded state.
  bool commit(const std::string& label);
  bool canUndo() const;
  bool canRedo() const;
  Result<void> undo();
  Result<void> redo();
  std::string undoLabel() const;  // label of the step that undo() would revert; "" if none
  std::string redoLabel() const;
  std::size_t historySize() const { return states_.size(); }
  std::size_t historyBytes() const { return historyBytes_; }

  // ---- play mode ----
  bool playing() const { return playing_; }
  bool paused() const { return paused_; }
  Result<void> play();
  Result<void> stop();
  void setPaused(bool paused) { paused_ = paused; }
  // Advances the simulation by dt (real seconds) when playing and not paused; no-op otherwise.
  void tick(float dt);
  void stepFrame(float dt = 1.0F / 60.0F);  // advances exactly one frame while paused
  double playTime() const { return playTime_; }

  // ---- scripts ----
  struct ScriptDiagnostics {
    bool ok = true;
    std::vector<script::Diagnostic> errors;
    std::string failure;  // non-compile failures (static init, missing file)
  };
  // Compiles every .mins under scriptDirs (plus unsaved overrides in `overrides`, keyed by project
  // relative path). On success the host is created or hot-reloaded; on failure nothing changes.
  ScriptDiagnostics compileScripts(const std::map<std::string, std::string>& overrides = {});
  std::vector<std::string> scriptFiles() const;  // project-relative .mins paths, sorted

  // Standard failure reporting helper: logs to the console and returns the error.
  template <class T>
  Result<T> report(Result<T> r, const char* what) {
    if (!r.ok()) console_->add(LogLevel::Error, "editor", std::string(what) + ": " + r.error().describe());
    return r;
  }

 private:
  EditorDocument() = default;

  struct HistoryState {
    std::string label;
    std::string text;  // .minseen
    int selection = -1;
    std::uint64_t id = 0;
  };

  sceneio::SceneIoContext ioContext();
  Result<std::string> snapshot();
  Result<void> restore(const std::string& text, int selection);
  void resetHistory();
  void trimHistory();
  Result<void> loadSceneText(const std::string& text);
  Result<void> fillRegistryAndHost();
  void cloneInto(scene::GameObject& src, scene::GameObject* parent, scene::GameObject*& out, Result<void>& status);
  void populateDefaults();
  void rescanAssetsBuiltinOnly();
  bool dirty_before_play() const;
  bool playDirty_ = false;

  rhi::Device* device_ = nullptr;
  renderer::Renderer* renderer_ = nullptr;
  EditorConfig cfg_;
  std::shared_ptr<ConsoleLog> console_;
  std::unique_ptr<runtime::Runtime> runtime_;
  sceneio::AssetTable assets_;
  sceneio::ComponentRegistry registry_;
  std::unique_ptr<scripting::ScriptHost> host_;

  std::optional<package::ProjectFile> project_;
  std::filesystem::path root_, projectFile_;
  std::vector<AssetEntry> assetList_;

  struct ModelAsset {
    assets::ModelData data;
    runtime::GpuModel gpu;
  };
  std::map<std::string, ModelAsset> models_;
  std::map<std::string, std::shared_ptr<const assets::AudioClip>> audio_;
  sceneio::BuiltinAssets builtins_;
  void addModelAssets(const std::string& path, const ModelAsset& m);
  Result<void> ensureBuiltins();

  std::string scenePath_;
  scene::GameObjectId selected_;

  std::vector<HistoryState> states_;
  std::size_t cursor_ = 0;
  std::size_t historyBytes_ = 0;
  std::uint64_t nextId_ = 1;
  std::uint64_t savedId_ = 0;  // history state id that matches the file on disk (0 = never saved)
  bool liveEdit_ = false;      // setProperty() changes not yet committed

  bool playing_ = false, paused_ = false;
  std::string playSnapshot_;
  int playSelection_ = -1;
  double playTime_ = 0.0;
};

}  // namespace mingol::editor
