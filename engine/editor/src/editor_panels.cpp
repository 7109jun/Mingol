// SPDX-License-Identifier: GPL-2.0-only
// Hierarchy, inspector, console, assets, scripts and profiler panels.
#include <algorithm>
#include <cmath>

#include "mingol/editor/editor_app.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/scripting/script_component.hpp"

namespace mingol::editor {

using gui::Color;
using gui::TreeFlags;

namespace {
Color levelColor(const gui::Style& s, LogLevel l) {
  switch (l) {
    case LogLevel::Warn: return s.warning;
    case LogLevel::Error:
    case LogLevel::Fatal: return s.error;
    case LogLevel::Trace:
    case LogLevel::Debug: return s.textDim;
    default: return s.text;
  }
}
}  // namespace

// ---- hierarchy ------------------------------------------------------------------------------
void EditorApp::hierarchyNode(scene::GameObject* go, int depth) {
  const int idx = hIdx_.count(go) != 0 ? hIdx_[go] : -1;
  ctx_.pushID(static_cast<std::int64_t>(idx));
  const bool hasKids = !go->children().empty();
  TreeFlags flags = TreeFlags::DefaultOpen;
  if (!hasKids) flags = flags | TreeFlags::Leaf;
  if (doc_->selected() == go) flags = flags | TreeFlags::Selected;
  std::string label = go->name();
  if (!go->activeSelf()) label += " (inactive)";
  label += "##node";
  const bool open = ctx_.treeNode(label, flags);
  track("node:" + go->name());
  const gui::ItemState item = ctx_.lastItem();
  if (item.clicked) doc_->select(go);
  if (item.doubleClicked) {
    inRename_ = go->name();
    doc_->select(go);
    dialog_ = Dialog::Rename;
    modalPending_ = true;
  }
  ctx_.dragSource("go", std::to_string(idx), go->name());
  std::string payload;
  if (ctx_.dropTarget("go", &payload)) {
    hAction_.kind = HierarchyAction::Kind::Reparent;
    hAction_.a = std::atoi(payload.c_str());
    hAction_.b = idx;
  }
  if (ctx_.beginContextMenu("nodemenu")) {
    doc_->select(go);
    if (menuItemT("Create Empty Child")) { hAction_.kind = HierarchyAction::Kind::CreateChild; hAction_.a = idx; ctx_.closeCurrentPopup(); }
    if (menuItemT("Duplicate")) { hAction_.kind = HierarchyAction::Kind::Duplicate; hAction_.a = idx; ctx_.closeCurrentPopup(); }
    if (menuItemT("Rename")) {
      inRename_ = go->name();
      dialog_ = Dialog::Rename;
      ctx_.closeCurrentPopup();
      modalPending_ = true;
    }
    if (menuItemT(go->activeSelf() ? "Deactivate" : "Activate")) { check(doc_->setActive(go, !go->activeSelf()), "Set active"); ctx_.closeCurrentPopup(); }
    ctx_.menuSeparator();
    if (menuItemT("Delete")) { hAction_.kind = HierarchyAction::Kind::Delete; hAction_.a = idx; ctx_.closeCurrentPopup(); }
    ctx_.endPopup();
  }
  if (open) {
    const std::vector<scene::GameObjectId> kids = go->children();
    for (const scene::GameObjectId id : kids) {
      if (auto* c = go->scene().get(id); c != nullptr && !c->markedForDestroy()) hierarchyNode(c, depth + 1);
    }
    ctx_.treePop();
  }
  ctx_.popID();
}

void EditorApp::panelHierarchy() {
  hIdx_.clear();
  const auto all = doc_->allObjects();
  for (std::size_t i = 0; i < all.size(); ++i) hIdx_[all[i]] = static_cast<int>(i);
  hAction_ = {};
  for (scene::GameObject* r : doc_->scene().rootObjects()) {
    if (!r->markedForDestroy()) hierarchyNode(r, 0);
  }
  ctx_.spacing();
  ctx_.interactiveArea("##rootdrop", {0, std::max(24.0F, ctx_.availableHeight())});
  const Rect area = ctx_.lastItemRect();
  ctx_.draw().addRectOutline(area, ctx_.isDragging() ? ctx_.style.accent : ctx_.style.border);
  if (!ctx_.isDragging()) ctx_.draw().addText({area.x0 + 6, area.y0 + 4}, "drop here to unparent", ctx_.style.textDim, ctx_.style.fontScale);
  std::string payload;
  if (ctx_.dropTarget("go", &payload)) {
    hAction_.kind = HierarchyAction::Kind::Reparent;
    hAction_.a = std::atoi(payload.c_str());
    hAction_.b = -1;
  }
  if (ctx_.beginContextMenu("bgmenu")) {
    if (menuItemT("Create Empty")) { doc_->createEmpty(); ctx_.closeCurrentPopup(); }
    ctx_.endPopup();
  }
  // apply the deferred action (the tree was being traversed above)
  auto* a = doc_->objectAtIndex(hAction_.a);
  switch (hAction_.kind) {
    case HierarchyAction::Kind::Reparent:
      if (a != nullptr) check(doc_->reparent(a, doc_->objectAtIndex(hAction_.b)), "Reparent");
      break;
    case HierarchyAction::Kind::Delete:
      if (a != nullptr) check(doc_->deleteObject(a), "Delete");
      break;
    case HierarchyAction::Kind::Duplicate:
      if (a != nullptr) check(doc_->duplicate(a), "Duplicate");
      break;
    case HierarchyAction::Kind::CreateChild:
      if (a != nullptr) doc_->createEmpty("GameObject", a);
      break;
    case HierarchyAction::Kind::None: break;
  }
}

// ---- inspector ------------------------------------------------------------------------------
bool EditorApp::drawProperty(Property& p) {
  const PropValue v = p.get();
  ctx_.pushID(p.name);
  bool edited = false, commitNow = false;
  auto apply = [&](const PropValue& nv) {
    if (auto r = doc_->setProperty(p, nv); !r.ok()) doc_->console().add(LogLevel::Warn, "inspector", p.name + ": " + r.error().message);
    else edited = true;
  };
  if (p.readOnly() || p.kind == PropKind::Label) {
    ctx_.label(p.name + ": " + toDisplayString(v));
  } else {
    switch (p.kind) {
      case PropKind::Bool: {
        bool b = std::get<bool>(v);
        if (ctx_.checkbox(p.name, b)) { apply(PropValue(b)); commitNow = true; }
        break;
      }
      case PropKind::Int: {
        int i = static_cast<int>(std::get<std::int64_t>(v));
        if (ctx_.dragInt(p.name, i, static_cast<float>(std::max(0.05, p.speed)), static_cast<int>(p.min), static_cast<int>(p.max))) apply(PropValue(static_cast<std::int64_t>(i)));
        break;
      }
      case PropKind::Float: {
        float f = static_cast<float>(std::get<double>(v));
        if (ctx_.dragFloat(p.name, f, static_cast<float>(p.speed), static_cast<float>(p.min), static_cast<float>(p.max))) apply(PropValue(static_cast<double>(f)));
        break;
      }
      case PropKind::Vec3: {
        const Vec3 x = std::get<Vec3>(v);
        float a[3] = {x.x, x.y, x.z};
        if (ctx_.dragFloat3(p.name, a, static_cast<float>(std::max(0.001, p.speed)))) apply(PropValue(Vec3{a[0], a[1], a[2]}));
        break;
      }
      case PropKind::Color3: {
        const Vec3 x = std::get<Vec3>(v);
        float c[4] = {x.x, x.y, x.z, 1.0F};
        if (ctx_.colorEdit(p.name, c)) apply(PropValue(Vec3{c[0], c[1], c[2]}));
        break;
      }
      case PropKind::Color4: {
        const Vec4 x = std::get<Vec4>(v);
        float c[4] = {x.x, x.y, x.z, x.w};
        if (ctx_.colorEdit(p.name, c)) apply(PropValue(Vec4{c[0], c[1], c[2], c[3]}));
        break;
      }
      case PropKind::Enum: {
        int i = static_cast<int>(std::get<std::int64_t>(v));
        if (ctx_.combo(p.name, i, p.enumNames)) { apply(PropValue(static_cast<std::int64_t>(i))); commitNow = true; }
        break;
      }
      case PropKind::String: {
        std::string s = std::get<std::string>(v);
        if (inputT(p.name, s)) apply(PropValue(s));
        break;
      }
      case PropKind::Asset: {
        const std::string cur = std::get<std::string>(v);
        std::vector<std::string> items{"(none)"};
        items.insert(items.end(), p.assetChoices.begin(), p.assetChoices.end());
        int i = 0;
        for (std::size_t k = 1; k < items.size(); ++k)
          if (items[k] == cur) i = static_cast<int>(k);
        if (ctx_.combo(p.name, i, items)) { apply(PropValue(i == 0 ? std::string() : items[static_cast<std::size_t>(i)])); commitNow = true; }
        break;
      }
      case PropKind::Label: break;
    }
  }
  if (edited && (commitNow || ctx_.itemEditFinished())) doc_->commit("Edit " + p.name);
  else if (!edited && ctx_.itemEditFinished()) doc_->commit("Edit " + p.name);
  ctx_.popID();
  return edited;
}

void EditorApp::panelInspector() {
  scene::GameObject* go = doc_->selected();
  if (go == nullptr) {
    ctx_.labelDim("Nothing selected.");
    return;
  }
  bool active = go->activeSelf();
  if (ctx_.checkbox("##active", active)) check(doc_->setActive(go, active), "Set active");
  ctx_.sameLine();
  std::string name = go->name();
  bool entered = false;
  ctx_.setNextWidth(std::max(60.0F, ctx_.availableWidth()));
  if (inputT("##name", name, &entered)) go->setName(name);
  if (ctx_.itemEditFinished()) {
    if (go->name().empty()) go->setName("GameObject");
    doc_->commit("Rename " + go->name());
  }
  ctx_.separator();

  if (ctx_.collapsingHeader("Transform", true)) {
    auto props = describeTransform(*go);
    for (Property& p : props) drawProperty(p);
  }
  std::vector<scene::Component*> comps;
  go->forEachComponent([&](scene::Component& c) { comps.push_back(&c); });
  scene::Component* toRemove = nullptr;
  int i = 0;
  for (scene::Component* c : comps) {
    ctx_.pushID(static_cast<std::int64_t>(i++));
    std::string title = c->typeName();
    if (auto* sc = dynamic_cast<scripting::ScriptComponent*>(c)) title = "Script: " + sc->className();
    if (ctx_.collapsingHeader(title + "##comp", true)) {
      bool en = c->enabled();
      if (ctx_.checkbox("Enabled", en)) { c->setEnabled(en); doc_->commit("Toggle " + title); }
      auto props = describeComponent(*c, doc_->propertyEnv());
      for (Property& p : props) drawProperty(p);
      if (ctx_.smallButton("Remove")) toRemove = c;
    }
    ctx_.popID();
  }
  if (toRemove != nullptr) check(doc_->removeComponent(go, toRemove), "Remove component");

  ctx_.spacing();
  if (buttonT("Add Component")) ctx_.openPopupAtLastItem("addcomp");
  if (ctx_.beginPopup("addcomp")) {
    for (const std::string& t : addableComponentTypes()) {
      if (menuItemT(t)) { check(doc_->addComponent(go, t), "Add component"); ctx_.closeCurrentPopup(); }
    }
    if (auto* host = doc_->scriptHost()) {
      const auto classes = host->scriptClasses();
      if (!classes.empty()) ctx_.menuSeparator();
      for (const std::string& c : classes) {
        if (menuItemT("Script: " + c)) { check(doc_->addScript(go, c), "Add script"); ctx_.closeCurrentPopup(); }
      }
    }
    ctx_.endPopup();
  }
}

// ---- console --------------------------------------------------------------------------------
void EditorApp::panelConsole() {
  static const std::vector<std::string> levels = {"All", "Debug+", "Info+", "Warn+", "Error+"};
  if (buttonT("Clear")) doc_->console().clear();
  ctx_.sameLine();
  ctx_.setNextWidth(110);
  ctx_.combo("##lvl", consoleLevel_, levels);
  ctx_.sameLine();
  ctx_.checkbox("Auto-scroll", consoleAutoScroll_);
  ctx_.sameLine();
  ctx_.setNextWidth(std::max(80.0F, ctx_.availableWidth()));
  inputT("##filter", consoleFilter_);
  ConsoleFilter f;
  static const LogLevel mins[] = {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error};
  f.minLevel = mins[std::clamp(consoleLevel_, 0, 4)];
  f.text = consoleFilter_;
  auto entries = doc_->console().filtered(f);
  if (entries.size() > 500) entries.erase(entries.begin(), entries.end() - 500);
  ctx_.beginChild("##consolelist", {0, 0}, true);
  for (const ConsoleEntry& e : entries) {
    ctx_.labelColored("[" + std::string(toString(e.level)) + "] [" + e.category + "] " + e.message, levelColor(ctx_.style, e.level));
  }
  const std::uint64_t total = doc_->console().totalAdded();
  if (consoleAutoScroll_ && total != consoleSeen_) ctx_.setScrollOffset(1e9F);
  consoleSeen_ = total;
  ctx_.endChild();
}

// ---- assets ---------------------------------------------------------------------------------
void EditorApp::panelAssets() {
  if (!doc_->hasProject()) {
    ctx_.labelDim("Open or create a project (File menu).");
    return;
  }
  if (buttonT("Rescan")) check(doc_->rescanAssets(), "Rescan assets");
  ctx_.sameLine();
  ctx_.labelDim("Copy .glb/.gltf/.wav/.ogg/.mins files into the project's assets/scripts folders, then Rescan.");
  ctx_.separator();
  int i = 0;
  for (const AssetEntry& a : doc_->assetList()) {
    ctx_.pushID(static_cast<std::int64_t>(i++));
    const std::string label = std::string("[") + toString(a.type) + "] " + a.path + "##asset";
    ctx_.selectable(label, false);
    track("asset:" + a.path);
    const gui::ItemState st = ctx_.lastItem();
    ctx_.dragSource("asset", a.path, a.path);
    if (st.doubleClicked) {
      if (a.type == AssetType::Script) check(openScript(a.path), "Open script");
      else if (a.type == AssetType::Model) check(doc_->instantiateModelAsset(a.path), "Instantiate model");
      else if (a.type == AssetType::Scene) requestDiscardOk([this, p = a.path] { check(doc_->openScene(p), "Open scene"); });
    }
    ctx_.popID();
  }
  if (doc_->assetList().empty()) ctx_.labelDim("(no assets)");
  ctx_.spacing();
  ctx_.labelDim("Double-click: model -> add to scene, script -> open, scene -> open.");
}

// ---- scripts --------------------------------------------------------------------------------
void EditorApp::panelScripts() {
  scriptFocused_ = false;
  if (!doc_->hasProject()) {
    ctx_.labelDim("Open or create a project to edit scripts.");
    return;
  }
  if (buttonT("Compile")) compileScripts();
  ctx_.sameLine();
  if (buttonT("Save") && !activeScript_.empty()) check(saveActiveScript(), "Save script");
  ctx_.sameLine();
  const auto files = doc_->scriptFiles();
  std::vector<std::string> items = files;
  int cur = -1;
  for (std::size_t k = 0; k < files.size(); ++k)
    if (files[k] == activeScript_) cur = static_cast<int>(k);
  if (!items.empty()) {
    ctx_.setNextWidth(std::max(120.0F, ctx_.availableWidth()));
    int sel = std::max(cur, 0);
    if (ctx_.combo("##scriptfile", sel, items) && sel != cur) check(openScript(items[static_cast<std::size_t>(sel)]), "Open script");
  } else {
    ctx_.labelDim("No scripts yet: Scripts > New Script...");
  }
  const auto it = scripts_.find(activeScript_);
  if (it == scripts_.end()) return;
  CodeEditor& ed = it->second.editor;
  const float diagH = errors_.empty() ? 0.0F : std::min(120.0F, ctx_.lineHeight() * static_cast<float>(errors_.size() + 1) + 8);
  ed.draw(ctx_, "code##" + activeScript_, {0, std::max(40.0F, ctx_.availableHeight() - diagH - ctx_.lineHeight() - 6)});
  scriptFocused_ = ed.focused();
  ctx_.labelDim(activeScript_ + (ed.modified() ? " *" : "") + "   Ln " + std::to_string(ed.cursorLine()) + ", Col " + std::to_string(ed.cursorColumn()));
  if (!errors_.empty()) {
    ctx_.beginChild("##diag", {0, 0}, true);
    for (std::size_t k = 0; k < errors_.size(); ++k) {
      ctx_.pushID(static_cast<std::int64_t>(k));
      ctx_.selectable(errors_[k] + "##err", false);
      if (ctx_.lastItem().clicked) {
        // "<file>:<line>:<col>: error: msg"
        const std::string& e = errors_[k];
        const auto c1 = e.find(':');
        const auto c2 = c1 == std::string::npos ? c1 : e.find(':', c1 + 1);
        if (c2 != std::string::npos) {
          const std::string file = e.substr(0, c1);
          const int line = std::atoi(e.c_str() + c1 + 1);
          if (scripts_.count(file) != 0) {
            activeScript_ = file;
            scripts_[file].editor.gotoLine(line);
          }
        }
      }
      ctx_.popID();
    }
    ctx_.endChild();
  }
}

// ---- profiler -------------------------------------------------------------------------------
void EditorApp::panelProfiler() {
  char buf[96];
  std::snprintf(buf, sizeof buf, "%.1f FPS   frame %.2f ms", profiler_.fps(), profiler_.frameMs());
  ctx_.label(buf);
  const Rect r = ctx_.allocate({0, 70});
  gui::DrawList& dl = ctx_.draw();
  dl.addRect(r, ctx_.style.frame);
  const auto& h = profiler_.frameHistory();
  float mx = 1.0F;
  for (float v : h) mx = std::max(mx, v);
  if (h.size() > 1) {
    for (std::size_t k = 1; k < h.size(); ++k) {
      const float x0 = r.x0 + r.width() * static_cast<float>(k - 1) / static_cast<float>(Profiler::kHistory - 1);
      const float x1 = r.x0 + r.width() * static_cast<float>(k) / static_cast<float>(Profiler::kHistory - 1);
      const float y0 = r.y1 - 2 - (r.height() - 4) * h[k - 1] / mx;
      const float y1 = r.y1 - 2 - (r.height() - 4) * h[k] / mx;
      dl.addLine({x0, y0}, {x1, y1}, ctx_.style.accent, 1.0F);
    }
  }
  ctx_.labelDim("scale: " + std::to_string(static_cast<int>(mx)) + " ms");
  ctx_.separator();
  for (const ProfilerScopeStats& s : profiler_.scopes()) {
    std::snprintf(buf, sizeof buf, "%-14s last %6.2f  avg %6.2f  max %6.2f ms", s.name.c_str(), s.lastMs, s.avgMs, s.maxMs);
    ctx_.label(buf);
  }
  ctx_.separator();
  ctx_.label("Objects: " + std::to_string(static_cast<long>(profiler_.counter("objects"))));
  if (desc_.renderer != nullptr) {
    const auto& st = desc_.renderer->stats();
    ctx_.label("Draw calls: " + std::to_string(st.drawCalls) + "  shadow: " + std::to_string(st.shadowDrawCalls));
  }
  ctx_.label("History: " + std::to_string(doc_->historySize()) + " steps, " + std::to_string(doc_->historyBytes() / 1024) + " KiB");
}

}  // namespace mingol::editor
