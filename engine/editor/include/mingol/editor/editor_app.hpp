// SPDX-License-Identifier: GPL-2.0-only
// The Mingol editor application logic: menus, dock layout, panels, dialogs, viewport interaction.
// It is window-system independent: feed it platform events, call frame(), render the viewport, draw
// the returned DrawList. The `mingol-editor` executable (apps/editor) wires it to a GLFW window and
// Vulkan; tests drive it with synthetic events.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>

#include "mingol/editor/code_editor.hpp"
#include "mingol/editor/document.hpp"
#include "mingol/editor/profiler.hpp"
#include "mingol/editor/viewport_math.hpp"
#include "mingol/gui/context.hpp"
#include "mingol/gui/dock.hpp"
#include "mingol/rhi/device.hpp"

namespace mingol::editor {

using gui::Rect;

struct EditorAppDesc {
  rhi::Device* device = nullptr;          // optional: without a GPU the viewport shows only the overlay
  renderer::Renderer* renderer = nullptr;
  EditorConfig config;
  std::filesystem::path playerExe;        // runtime executable embedded by "single exe" builds (optional)
};

enum class GizmoMode : std::uint8_t { Translate, Rotate, Scale };

class EditorApp {
 public:
  static Result<std::unique_ptr<EditorApp>> create(const EditorAppDesc& desc);
  ~EditorApp();
  EditorApp(const EditorApp&) = delete;
  EditorApp& operator=(const EditorApp&) = delete;

  EditorDocument& document() { return *doc_; }
  gui::Context& gui() { return ctx_; }
  Profiler& profiler() { return profiler_; }
  gui::DockLayout& dock() { return dock_; }

  void feed(const platform::Event& e);
  // Builds one UI frame (also advances the simulation while playing).
  const gui::DrawList& frame(Vec2 displaySize, double timeSeconds, float dt);

  // Renders the scene into the viewport texture for the size the viewport panel had in the last frame().
  // Returns the texture to register under gui::kTextureUserBase (null without a GPU or when the panel is hidden).
  Result<std::shared_ptr<rhi::Texture>> renderViewport();
  const std::shared_ptr<rhi::Texture>& viewportTexture() const { return vpTexture_; }
  std::uint64_t viewportTextureGeneration() const { return vpGeneration_; }

  bool quitRequested() const { return quit_; }
  // Asks to quit; shows the unsaved-changes dialog when needed.
  void requestQuit();

  // ---- state exposed for tests and the host ----
  GizmoMode gizmoMode() const { return gizmoMode_; }
  void setGizmoMode(GizmoMode m) { gizmoMode_ = m; }
  EditorCamera& camera() { return camera_; }
  Rect viewportRect() const { return vpRect_; }  // screen rect of the viewport image in the last frame
  const std::string& statusMessage() const { return status_; }
  bool dialogOpen() const { return dialog_ != Dialog::None; }
  std::string activeScriptPath() const { return activeScript_; }
  CodeEditor* scriptEditor(const std::string& path);
  Result<void> openScript(const std::string& path);
  Result<void> saveActiveScript();
  void compileScripts();  // compiles with unsaved editor text; shows diagnostics in the editors
  Result<void> saveLayout();
  void resetLayout();
  const std::vector<std::string>& dialogErrors() const { return errors_; }
  // Screen rect of a UI element drawn in the last frame ("btn:Play", "menu:File", "item:Save Scene",
  // "node:<object name>", "input:<label>"...). For tests and automation; nullopt when not visible.
  std::optional<Rect> trackedRect(const std::string& key) const {
    const auto it = tracked_.find(key);
    if (it == tracked_.end()) return std::nullopt;
    return it->second;
  }

  // Picking helper (also used by the viewport): nearest scene object under a viewport pixel, or null.
  scene::GameObject* pickObject(Vec2 pixel, Vec2 viewportSize);

 private:
  enum class Dialog : std::uint8_t { None, NewProject, OpenProject, SaveSceneAs, Build, NewScript, Message, ConfirmDiscard, Rename };

  EditorApp() = default;

  // frame pieces (editor_app.cpp)
  void handleShortcuts();
  void drawMenuBar(float height);
  void drawToolbar(const Rect& r);
  void drawStatusBar(const Rect& r);
  void drawPanel(const std::string& name, const Rect& content);
  void drawDialogs();
  void setDefaultLayout();
  void showMessage(const std::string& title, const std::string& text);
  void setStatus(std::string s) { status_ = std::move(s); }
  void requestDiscardOk(std::function<void()> action);
  void toggleKeysDown(const platform::Event& e);
  template <class T>
  bool check(Result<T> r, const char* what) {
    if (r.ok()) return true;
    doc_->console().add(LogLevel::Error, "editor", std::string(what) + ": " + r.error().describe());
    showMessage(what, r.error().message);
    setStatus(std::string(what) + ": " + r.error().message);
    return false;
  }
  void track(const std::string& key) { tracked_[key] = ctx_.lastItemRect(); }
  bool buttonT(std::string_view label, Vec2 size = {0, 0}) {
    const bool r = ctx_.button(label, size);
    track("btn:" + std::string(label));
    return r;
  }
  bool inputT(std::string_view label, std::string& s, bool* entered = nullptr) {
    const bool r = ctx_.inputText(label, s, entered);
    track("input:" + std::string(label));
    return r;
  }
  bool menuItemT(std::string_view label, std::string_view shortcut = {}, bool selected = false, bool enabled = true) {
    const bool r = ctx_.menuItem(label, shortcut, selected, enabled);
    track("item:" + std::string(label));
    return r;
  }
  bool beginMenuT(std::string_view label, bool enabled = true) {
    const bool r = ctx_.beginMenu(label, enabled);
    track("menu:" + std::string(label));
    return r;
  }
  std::map<std::string, Rect> tracked_;

  // panels
  void panelHierarchy();
  void panelInspector();
  void panelViewport();
  void panelConsole();
  void panelAssets();
  void panelScripts();
  void panelProfiler();
  bool drawProperty(Property& p);
  void hierarchyNode(scene::GameObject* go, int depth);

  // viewport helpers
  void viewportInput(const gui::ItemState& st);
  void drawViewportOverlay(gui::DrawList& dl, const Rect& r, const renderer::Camera& cam);
  struct GizmoHit {
    int axis = -1;  // 0..2, 3 = uniform scale
  };
  GizmoHit hitGizmo(const renderer::Camera& cam, Vec2 size, Vec2 mouse) const;
  void beginGizmoDrag(const GizmoHit& hit, const renderer::Camera& cam, Vec2 size, Vec2 mouse);
  void updateGizmoDrag(const renderer::Camera& cam, Vec2 size, Vec2 mouse);
  void endGizmoDrag(bool cancel);
  float gizmoLength(const renderer::Camera& cam, Vec3 at) const;
  renderer::Camera currentCamera(float aspect, bool* usingGameCamera = nullptr);

  gui::Context ctx_;
  gui::DockLayout dock_;
  Profiler profiler_;
  std::unique_ptr<EditorDocument> doc_;
  EditorAppDesc desc_;
  bool quit_ = false;
  std::string status_ = "Ready";
  double time_ = 0;
  float dt_ = 0;
  std::set<platform::Key> keysDown_;

  // dialogs
  Dialog dialog_ = Dialog::None;
  bool modalPending_ = false;
  std::string title_, message_;
  std::function<void()> pendingDiscard_;
  std::string inName_, inDir_, inPath_, inScene_, inScriptName_, inRename_;
  std::vector<std::string> errors_;
  struct BuildState {
    std::string output = "build/game.mingol";
    std::string keyFile;
    std::string playerExe;
    bool encrypt = false, singleExe = false;
    std::string result;
  } build_;

  // hierarchy
  struct HierarchyAction {
    enum class Kind { None, Reparent, Delete, Duplicate, CreateChild } kind = Kind::None;
    int a = -1, b = -1;
  } hAction_;
  std::string renameBuffer_;

  // scripts
  struct OpenScript {
    CodeEditor editor;
  };
  std::map<std::string, OpenScript> scripts_;
  std::string activeScript_;

  // console
  int consoleLevel_ = 0;
  std::string consoleFilter_;
  bool consoleAutoScroll_ = true;
  std::uint64_t consoleSeen_ = 0;

  // viewport
  EditorCamera camera_;
  GizmoMode gizmoMode_ = GizmoMode::Translate;
  Rect vpRect_{};
  Vec2 vpSize_{0, 0};
  std::shared_ptr<rhi::Texture> vpTexture_;
  std::uint64_t vpGeneration_ = 0;
  std::unique_ptr<rhi::CommandList> vpTransition_;
  bool vpVisible_ = false;
  std::string vpNotice_;
  struct GizmoDrag {
    bool active = false;
    int axis = -1;
    GizmoMode mode = GizmoMode::Translate;
    scene::GameObjectId object;
    Vec3 startPos{0, 0, 0}, startScale{1, 1, 1}, axisDir{1, 0, 0}, origin{0, 0, 0};
    Quat startRot;
    float startParam = 0, length = 1;
    Vec2 startMouse{0, 0};
    bool changed = false;
  } drag_;
  int vpDragButton_ = -1;  // 1 = fly look, 2 = pan, 0 = orbit (alt+left)
  bool scriptFocused_ = false;
  std::unordered_map<const scene::GameObject*, int> hIdx_;
};

}  // namespace mingol::editor
