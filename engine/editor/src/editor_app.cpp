// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/editor_app.hpp"

#include <algorithm>
#include <cmath>

#include "mingol/core/fs.hpp"
#include "mingol/package/archive.hpp"
#include "mingol/scripting/script_host.hpp"

namespace mingol::editor {

using platform::Key;

namespace {

constexpr const char* kPanels[] = {"Viewport", "Hierarchy", "Inspector", "Console", "Assets", "Scripts", "Profiler"};

std::string trim(std::string s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

const char* modalId(int d) {
  static const char* ids[] = {"", "dlg_new_project", "dlg_open_project", "dlg_save_scene_as", "dlg_build", "dlg_new_script", "dlg_message", "dlg_confirm", "dlg_rename"};
  return ids[d];
}

}  // namespace

Result<std::unique_ptr<EditorApp>> EditorApp::create(const EditorAppDesc& desc) {
  std::unique_ptr<EditorApp> app(new EditorApp());
  app->desc_ = desc;
  MINGOL_TRY_ASSIGN(app->doc_, EditorDocument::create(desc.device, desc.renderer, desc.config));
  app->setDefaultLayout();
  return app;
}

EditorApp::~EditorApp() = default;

void EditorApp::setDefaultLayout() {
  dock_ = gui::DockLayout();
  dock_.addPanel("Viewport");
  dock_.addPanel("Scripts");
  (void)dock_.dockRelative("Hierarchy", "Viewport", gui::DockZone::Left, 0.2F);
  (void)dock_.dockRelative("Inspector", "Viewport", gui::DockZone::Right, 0.26F);
  (void)dock_.dockRelative("Console", "Viewport", gui::DockZone::Bottom, 0.28F);
  (void)dock_.dockRelative("Assets", "Console", gui::DockZone::Center);
  (void)dock_.dockRelative("Profiler", "Console", gui::DockZone::Center);
  dock_.activate("Console");
  dock_.activate("Viewport");
}

void EditorApp::resetLayout() { setDefaultLayout(); }

Result<void> EditorApp::saveLayout() {
  if (!doc_->hasProject()) return Error{ErrorCode::InvalidArgument, "no project is open"};
  MINGOL_TRY_ASSIGN(std::string text, json::write(dock_.toJson()));
  return fs::writeTextFileAtomic(doc_->projectRoot() / ".mingol-editor-layout.json", text);
}

void EditorApp::showMessage(const std::string& title, const std::string& text) {
  title_ = title;
  message_ = text;
  dialog_ = Dialog::Message;
  modalPending_ = true;
}

void EditorApp::requestDiscardOk(std::function<void()> action) {
  if (!doc_->dirty()) {
    action();
    return;
  }
  pendingDiscard_ = std::move(action);
  dialog_ = Dialog::ConfirmDiscard;
  modalPending_ = true;
}

void EditorApp::requestQuit() {
  requestDiscardOk([this] { quit_ = true; });
}

void EditorApp::feed(const platform::Event& e) {
  ctx_.feed(e);
  if (const auto* k = std::get_if<platform::KeyEvent>(&e)) {
    if (k->action == platform::KeyAction::Release) keysDown_.erase(k->key);
    else keysDown_.insert(k->key);
  } else if (const auto* f = std::get_if<platform::FocusEvent>(&e)) {
    if (!f->focused) keysDown_.clear();
  } else if (std::holds_alternative<platform::CloseRequestedEvent>(e)) {
    requestQuit();
  }
}

CodeEditor* EditorApp::scriptEditor(const std::string& path) {
  const auto it = scripts_.find(path);
  return it == scripts_.end() ? nullptr : &it->second.editor;
}

const gui::DrawList& EditorApp::frame(Vec2 display, double time, float dt) {
  profiler_.beginFrame();
  tracked_.clear();
  time_ = time;
  dt_ = dt;
  {
    Profiler::Scoped s(profiler_, "simulation");
    doc_->tick(std::min(dt, 0.1F));
  }
  Profiler::Scoped ui(profiler_, "ui");
  ctx_.beginFrame(display, time);
  const float rowH = ctx_.itemHeight();
  const float menuH = rowH + 6;
  const float toolH = rowH + 10;
  const float statusH = rowH + 2;
  handleShortcuts();
  drawMenuBar(menuH);
  drawToolbar({0, menuH, display.x, menuH + toolH});
  drawStatusBar({0, display.y - statusH, display.x, display.y});
  vpVisible_ = false;
  const Rect area{0, menuH + toolH, display.x, display.y - statusH};
  const auto slots = gui::dockSpace(ctx_, dock_, area);
  for (const gui::DockSlot& s : slots) {
    if (s.active) drawPanel(s.name, s.content);
  }
  drawDialogs();
  ctx_.endFrame();
  profiler_.setCounter("objects", static_cast<double>(doc_->scene().objectCount()));
  profiler_.endFrame();
  return ctx_.drawList();
}

void EditorApp::drawPanel(const std::string& name, const Rect& content) {
  const bool scrolls = name != "Viewport" && name != "Scripts" && name != "Console";
  ctx_.beginPanel(name, content, scrolls, true);
  if (name == "Viewport") panelViewport();
  else if (name == "Hierarchy") panelHierarchy();
  else if (name == "Inspector") panelInspector();
  else if (name == "Console") panelConsole();
  else if (name == "Assets") panelAssets();
  else if (name == "Scripts") panelScripts();
  else if (name == "Profiler") panelProfiler();
  ctx_.endPanel();
}

// ---- shortcuts ------------------------------------------------------------------------------
void EditorApp::handleShortcuts() {
  if (ctx_.wantTextInput() || ctx_.anyPopupOpen() || dialog_ != Dialog::None) return;
  const bool ctrl = ctx_.ctrl(), shift = ctx_.shift();
  if (ctrl && ctx_.keyPressed(Key::S)) {
    if (scriptFocused_ && !shift) {
      check(saveActiveScript(), "Save script");
    } else if (shift || doc_->scenePath().empty()) {
      if (doc_->hasProject()) {
        inScene_ = doc_->scenePath().empty() ? "scenes/new_scene.minseen" : doc_->scenePath();
        dialog_ = Dialog::SaveSceneAs;
        modalPending_ = true;
      }
    } else if (check(doc_->saveScene(), "Save scene")) {
      setStatus("Saved " + doc_->scenePath());
    }
  } else if (ctrl && !shift && ctx_.keyPressed(Key::Z, true)) {
    if (doc_->canUndo()) check(doc_->undo(), "Undo");
  } else if (ctrl && (ctx_.keyPressed(Key::Y, true) || (shift && ctx_.keyPressed(Key::Z, true)))) {
    if (doc_->canRedo()) check(doc_->redo(), "Redo");
  } else if (ctrl && ctx_.keyPressed(Key::D)) {
    if (auto* s = doc_->selected()) check(doc_->duplicate(s), "Duplicate");
  } else if (ctrl && ctx_.keyPressed(Key::B)) {
    compileScripts();
  } else if (ctrl && ctx_.keyPressed(Key::P)) {
    if (doc_->playing()) check(doc_->stop(), "Stop");
    else check(doc_->play(), "Play");
  } else if (ctx_.keyPressed(Key::Delete) && !scriptFocused_) {
    if (auto* s = doc_->selected()) check(doc_->deleteObject(s), "Delete");
  }
}

// ---- menu / toolbar / status ----------------------------------------------------------------
void EditorApp::drawMenuBar(float height) {
  ctx_.beginMainMenuBar({0, 0, ctx_.displaySize().x, height});
  if (beginMenuT("File")) {
    if (menuItemT("New Project...")) {
      requestDiscardOk([this] {
        inName_ = "MyGame";
        if (inDir_.empty()) inDir_ = std::filesystem::current_path().string();
        dialog_ = Dialog::NewProject;
        modalPending_ = true;
      });
    }
    if (menuItemT("Open Project...")) {
      requestDiscardOk([this] {
        dialog_ = Dialog::OpenProject;
        modalPending_ = true;
      });
    }
    ctx_.menuSeparator();
    if (menuItemT("New Scene")) requestDiscardOk([this] { check(doc_->newScene(true), "New scene"); });
    if (ctx_.beginMenu("Open Scene", doc_->hasProject())) {
      if (doc_->hasProject()) {
        for (const std::string& s : doc_->project()->scenes) {
          if (menuItemT(s, {}, s == doc_->scenePath())) requestDiscardOk([this, s] { check(doc_->openScene(s), "Open scene"); });
        }
      }
      ctx_.endMenu();
    }
    if (menuItemT("Save Scene", "Ctrl+S", false, doc_->hasProject() && !doc_->scenePath().empty() && !doc_->playing())) {
      if (check(doc_->saveScene(), "Save scene")) setStatus("Saved " + doc_->scenePath());
    }
    if (menuItemT("Save Scene As...", "Ctrl+Shift+S", false, doc_->hasProject() && !doc_->playing())) {
      inScene_ = doc_->scenePath().empty() ? "scenes/new_scene.minseen" : doc_->scenePath();
      dialog_ = Dialog::SaveSceneAs;
      modalPending_ = true;
    }
    if (menuItemT("Save Project", {}, false, doc_->hasProject())) {
      if (check(doc_->saveProject(), "Save project")) setStatus("Project saved");
    }
    ctx_.menuSeparator();
    if (menuItemT("Build...", {}, false, doc_->hasProject())) {
      build_.playerExe = desc_.playerExe.string();
      build_.result.clear();
      dialog_ = Dialog::Build;
      modalPending_ = true;
    }
    ctx_.menuSeparator();
    if (menuItemT("Exit")) requestQuit();
    ctx_.endMenu();
  }
  if (beginMenuT("Edit")) {
    const std::string u = doc_->undoLabel(), r = doc_->redoLabel();
    if (menuItemT(u.empty() ? "Undo" : "Undo " + u, "Ctrl+Z", false, doc_->canUndo())) check(doc_->undo(), "Undo");
    if (menuItemT(r.empty() ? "Redo" : "Redo " + r, "Ctrl+Y", false, doc_->canRedo())) check(doc_->redo(), "Redo");
    ctx_.menuSeparator();
    auto* sel = doc_->selected();
    if (menuItemT("Duplicate", "Ctrl+D", false, sel != nullptr)) check(doc_->duplicate(sel), "Duplicate");
    if (menuItemT("Delete", "Del", false, sel != nullptr)) check(doc_->deleteObject(sel), "Delete");
    if (menuItemT("Rename", {}, false, sel != nullptr)) {
      inRename_ = sel->name();
      dialog_ = Dialog::Rename;
      modalPending_ = true;
    }
    ctx_.endMenu();
  }
  if (beginMenuT("GameObject")) {
    if (menuItemT("Create Empty")) doc_->createEmpty();
    if (beginMenuT("3D Object")) {
      if (menuItemT("Box")) check(doc_->createPrimitive(PrimitiveKind::Box), "Create box");
      if (menuItemT("Sphere")) check(doc_->createPrimitive(PrimitiveKind::Sphere), "Create sphere");
      if (menuItemT("Plane")) check(doc_->createPrimitive(PrimitiveKind::Plane), "Create plane");
      ctx_.endMenu();
    }
    if (menuItemT("Camera")) doc_->createCamera();
    if (beginMenuT("Light")) {
      if (menuItemT("Directional")) doc_->createLight(renderer::LightType::Directional);
      if (menuItemT("Point")) doc_->createLight(renderer::LightType::Point);
      if (menuItemT("Spot")) doc_->createLight(renderer::LightType::Spot);
      ctx_.endMenu();
    }
    ctx_.endMenu();
  }
  if (beginMenuT("Scripts")) {
    if (menuItemT("Compile && Reload", "Ctrl+B", false, doc_->hasProject())) compileScripts();
    if (menuItemT("New Script...", {}, false, doc_->hasProject())) {
      inScriptName_ = "NewScript";
      dialog_ = Dialog::NewScript;
      modalPending_ = true;
    }
    if (menuItemT("Save Script", {}, false, !activeScript_.empty())) check(saveActiveScript(), "Save script");
    ctx_.endMenu();
  }
  if (beginMenuT("View")) {
    for (const char* p : kPanels) {
      const bool shown = dock_.contains(p);
      if (menuItemT(p, {}, shown)) {
        if (shown) dock_.removePanel(p);
        else dock_.addPanel(p);
      }
    }
    ctx_.menuSeparator();
    if (menuItemT("Reset Layout")) resetLayout();
    if (menuItemT("Save Layout", {}, false, doc_->hasProject())) check(saveLayout(), "Save layout");
    ctx_.endMenu();
  }
  if (beginMenuT("Play")) {
    if (menuItemT(doc_->playing() ? "Stop" : "Play", "Ctrl+P")) {
      if (doc_->playing()) check(doc_->stop(), "Stop");
      else check(doc_->play(), "Play");
    }
    if (menuItemT("Pause", {}, doc_->paused(), doc_->playing())) doc_->setPaused(!doc_->paused());
    if (menuItemT("Step", {}, false, doc_->playing() && doc_->paused())) doc_->stepFrame();
    ctx_.endMenu();
  }
  ctx_.endMainMenuBar();
}

void EditorApp::drawToolbar(const Rect& r) {
  ctx_.beginPanel("toolbar", r, false, true);
  if (buttonT(doc_->playing() ? "Stop" : "Play")) {
    if (doc_->playing()) check(doc_->stop(), "Stop");
    else check(doc_->play(), "Play");
  }
  ctx_.sameLine();
  if (buttonT(doc_->paused() ? "Resume" : "Pause") && doc_->playing()) doc_->setPaused(!doc_->paused());
  ctx_.sameLine();
  if (buttonT("Step") && doc_->playing()) {
    doc_->setPaused(true);
    doc_->stepFrame();
  }
  ctx_.sameLine();
  ctx_.label(" | ");
  ctx_.sameLine();
  if (buttonT("Undo") && doc_->canUndo()) check(doc_->undo(), "Undo");
  ctx_.sameLine();
  if (buttonT("Redo") && doc_->canRedo()) check(doc_->redo(), "Redo");
  ctx_.sameLine();
  ctx_.label(" | ");
  ctx_.sameLine();
  int mode = static_cast<int>(gizmoMode_);
  if (ctx_.radio("Move", mode, 0)) gizmoMode_ = GizmoMode::Translate;
  ctx_.sameLine();
  if (ctx_.radio("Rotate", mode, 1)) gizmoMode_ = GizmoMode::Rotate;
  ctx_.sameLine();
  if (ctx_.radio("Scale", mode, 2)) gizmoMode_ = GizmoMode::Scale;
  ctx_.endPanel();
}

void EditorApp::drawStatusBar(const Rect& r) {
  ctx_.beginPanel("statusbar", r, false, true);
  std::string left = doc_->playing() ? (doc_->paused() ? "PAUSED  " : "PLAYING  ") : "";
  left += doc_->hasProject() ? doc_->project()->name : std::string("(no project)");
  left += "  |  " + (doc_->scenePath().empty() ? std::string("(unsaved scene)") : doc_->scenePath());
  if (doc_->dirty()) left += " *";
  left += "  |  " + status_;
  ctx_.label(left);
  ctx_.endPanel();
}

// ---- scripts --------------------------------------------------------------------------------
Result<void> EditorApp::openScript(const std::string& path) {
  if (!doc_->hasProject()) return Error{ErrorCode::InvalidArgument, "no project is open"};
  if (!package::isValidRelativePath(path)) return Error{ErrorCode::InvalidArgument, "invalid script path"};
  if (scripts_.count(path) == 0) {
    MINGOL_TRY_ASSIGN(std::string text, fs::readTextFile(doc_->projectRoot() / path, 16u << 20));
    scripts_[path].editor.setText(text);
  }
  activeScript_ = path;
  dock_.addPanel("Scripts");
  dock_.activate("Scripts");
  return {};
}

Result<void> EditorApp::saveActiveScript() {
  const auto it = scripts_.find(activeScript_);
  if (it == scripts_.end()) return Error{ErrorCode::InvalidArgument, "no script is open"};
  MINGOL_TRY(fs::writeTextFileAtomic(doc_->projectRoot() / activeScript_, it->second.editor.text()));
  it->second.editor.markSaved();
  compileScripts();
  return {};
}

void EditorApp::compileScripts() {
  if (!doc_->hasProject()) return;
  std::map<std::string, std::string> overrides;
  for (auto& [path, s] : scripts_) overrides[path] = s.editor.text();
  const auto r = doc_->compileScripts(overrides);
  for (auto& [path, s] : scripts_) s.editor.clearDiagnostics();
  if (r.ok) {
    setStatus("Scripts compiled");
    return;
  }
  errors_.clear();
  for (const auto& e : r.errors) {
    errors_.push_back(e.format());
    if (auto* ed = scriptEditor(e.file)) {
      auto d = ed->diagnostics();
      d.push_back({static_cast<int>(e.line), static_cast<int>(e.col), e.message});
      ed->setDiagnostics(std::move(d));
    }
  }
  setStatus(r.errors.empty() ? "Script error: " + r.failure : std::to_string(r.errors.size()) + " script error(s)");
}

// ---- dialogs --------------------------------------------------------------------------------
void EditorApp::drawDialogs() {
  if (dialog_ == Dialog::None) return;
  const char* id = modalId(static_cast<int>(dialog_));
  if (modalPending_) {
    ctx_.openModal(id);  // opened here, with an empty ID stack, so beginModal sees the same id
    modalPending_ = false;
  }
  if (!ctx_.isModalOpen()) {  // closed from outside (Escape)
    dialog_ = Dialog::None;
    return;
  }
  const float w = std::min(560.0F, ctx_.displaySize().x - 40);
  auto close = [&] {
    ctx_.closeModal();
    dialog_ = Dialog::None;
  };
  switch (dialog_) {
    case Dialog::NewProject:
      if (ctx_.beginModal(id, "New Project", {w, 190})) {
        inputT("Name##np", inName_);
        inputT("Directory##np", inDir_);
        ctx_.spacing();
        if (buttonT("Create")) {
          const std::string name = trim(inName_), dir = trim(inDir_);
          close();
          if (check(doc_->createProject(std::filesystem::path(dir) / name, name), "Create project")) {
            setStatus("Created project " + name);
            scripts_.clear();
            activeScript_.clear();
          }
        }
        ctx_.sameLine();
        if (buttonT("Cancel")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::OpenProject:
      if (ctx_.beginModal(id, "Open Project", {w, 150})) {
        inputT("Path to .mindev##op", inPath_);
        ctx_.spacing();
        if (buttonT("Open")) {
          const std::string p = trim(inPath_);
          close();
          scripts_.clear();
          activeScript_.clear();
          if (check(doc_->openProject(p), "Open project")) {
            setStatus("Opened " + p);
            if (auto t = fs::readTextFile(doc_->projectRoot() / ".mingol-editor-layout.json", 1u << 20); t.ok()) {
              if (auto j = json::parse(t.value()); j.ok()) {
                if (auto l = gui::DockLayout::fromJson(j.value()); l.ok()) dock_ = std::move(l).value();
              }
            }
          }
        }
        ctx_.sameLine();
        if (buttonT("Cancel")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::SaveSceneAs:
      if (ctx_.beginModal(id, "Save Scene As", {w, 150})) {
        inputT("Scene path##ss", inScene_);
        ctx_.labelDim("project relative, ends with .minseen");
        if (buttonT("Save")) {
          const std::string p = trim(inScene_);
          close();
          if (check(doc_->saveSceneAs(p), "Save scene")) setStatus("Saved " + p);
        }
        ctx_.sameLine();
        if (buttonT("Cancel")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::Rename:
      if (ctx_.beginModal(id, "Rename", {w, 130})) {
        bool entered = false;
        inputT("Name##rn", inRename_, &entered);
        if (buttonT("OK") || entered) {
          const std::string n = trim(inRename_);
          close();
          if (auto* s = doc_->selected()) check(doc_->rename(s, n), "Rename");
        }
        ctx_.sameLine();
        if (buttonT("Cancel")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::NewScript:
      if (ctx_.beginModal(id, "New Script", {w, 150})) {
        inputT("Class name##ns", inScriptName_);
        if (buttonT("Create")) {
          const std::string name = trim(inScriptName_);
          close();
          const bool valid = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) != 0 || name[0] == '_') &&
                             std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; });
          if (!valid) {
            showMessage("New script", "Class names use letters, digits and '_' and must not start with a digit.");
          } else {
            const std::string dir = doc_->project()->scriptDirs.empty() ? "scripts" : doc_->project()->scriptDirs[0];
            const std::string rel = dir + "/" + name + ".mins";
            const auto abs = doc_->projectRoot() / rel;
            std::error_code ec;
            if (std::filesystem::exists(abs, ec)) {
              showMessage("New script", rel + " already exists.");
            } else {
              const std::string text = "class " + name + " extends Script {\n    override void onStart() {\n        Log.info(\"" + name + " started\");\n    }\n\n    override void onUpdate(float dt) {\n    }\n}\n";
              if (check(fs::writeTextFileAtomic(abs, text), "Create script")) {
                (void)doc_->rescanAssets();
                if (check(openScript(rel), "Open script")) compileScripts();
              }
            }
          }
        }
        ctx_.sameLine();
        if (buttonT("Cancel")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::Build:
      if (ctx_.beginModal(id, "Build", {w, 330})) {
        inputT("Output##b", build_.output);
        ctx_.checkbox("Encrypt##b", build_.encrypt);
        if (build_.encrypt) inputT("Key file##b", build_.keyFile);
        ctx_.checkbox("Single executable##b", build_.singleExe);
        if (build_.singleExe) inputT("Player exe##b", build_.playerExe);
        if (doc_->dirty()) ctx_.labelColored("Unsaved scene changes are not part of the build.", ctx_.style.warning);
        if (!build_.result.empty()) ctx_.label(build_.result);
        if (buttonT("Build")) {
          build_.result.clear();
          package::ProjectPackOptions po;
          bool ok = true;
          if (build_.encrypt) {
            auto text = fs::readTextFile(trim(build_.keyFile), 4096);
            auto key = text.ok() ? package::ArchiveKey::fromHex(trim(text.value())) : Result<package::ArchiveKey>(text.error());
            if (!key.ok()) { build_.result = "Key error: " + key.error().message; ok = false; }
            else po.write.key = key.value();
          }
          if (ok) {
            package::ProjectPackReport rep;
            auto bytes = package::packProject(doc_->projectFile(), po, &rep);
            if (!bytes.ok()) { build_.result = "Pack failed: " + bytes.error().message; ok = false; }
            else {
              std::filesystem::path out = trim(build_.output);
              if (out.is_relative()) out = doc_->projectRoot() / out;
              std::error_code ec;
              std::filesystem::create_directories(out.parent_path(), ec);
              Result<void> w2 = {};
              if (!build_.singleExe) {
                w2 = fs::writeFileAtomic(out, bytes.value());
              } else {
                const std::filesystem::path tmp = out.string() + ".tmp";
                w2 = fs::writeFileAtomic(tmp, bytes.value());
                if (w2.ok()) w2 = package::packExecutable(trim(build_.playerExe), tmp, out);
                std::filesystem::remove(tmp, ec);
              }
              if (!w2.ok()) build_.result = "Write failed: " + w2.error().message;
              else {
                build_.result = "Built " + out.string() + " (" + std::to_string(rep.files) + " files, " + std::to_string(rep.bytes) + " bytes)";
                doc_->console().add(LogLevel::Info, "build", build_.result);
              }
            }
          }
        }
        ctx_.sameLine();
        if (buttonT("Close")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::Message:
      if (ctx_.beginModal(id, title_, {w, 150})) {
        ctx_.label(message_);
        ctx_.spacing();
        if (buttonT("OK")) close();
        ctx_.endModal();
      }
      break;
    case Dialog::ConfirmDiscard:
      if (ctx_.beginModal(id, "Unsaved changes", {w, 130})) {
        ctx_.label("The current scene has unsaved changes.");
        ctx_.spacing();
        const bool canSave = doc_->hasProject() && !doc_->scenePath().empty() && !doc_->playing();
        if (buttonT("Save") && canSave) {
          auto act = std::move(pendingDiscard_);
          close();
          if (check(doc_->saveScene(), "Save scene") && act) act();
        }
        ctx_.sameLine();
        if (buttonT("Discard")) {
          auto act = std::move(pendingDiscard_);
          close();
          if (doc_->playing()) (void)doc_->stop();
          if (act) act();
        }
        ctx_.sameLine();
        if (buttonT("Cancel")) {
          pendingDiscard_ = nullptr;
          close();
        }
        ctx_.endModal();
      }
      break;
    case Dialog::None: break;
  }
}

}  // namespace mingol::editor
