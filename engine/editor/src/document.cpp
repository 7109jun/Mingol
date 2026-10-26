// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/document.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <system_error>

#include "mingol/assets/audio.hpp"
#include "mingol/core/fs.hpp"
#include "mingol/core/hash.hpp"
#include "mingol/renderer/mesh.hpp"
#include "mingol/runtime/audio_components.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/sceneio/standard_assets.hpp"
#include "mingol/scripting/script_component.hpp"

namespace mingol::editor {

namespace fsys = std::filesystem;

namespace {

constexpr const char* kEngineVersion = "0.1.0";

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

AssetType typeOfExtension(const std::string& ext) {
  if (ext == ".gltf" || ext == ".glb") return AssetType::Model;
  if (ext == ".wav" || ext == ".ogg") return AssetType::Audio;
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") return AssetType::Texture;
  if (ext == ".mins") return AssetType::Script;
  if (ext == ".minseen") return AssetType::Scene;
  return AssetType::Other;
}

std::string relPath(const fsys::path& root, const fsys::path& p) {
  std::error_code ec;
  const fsys::path r = fsys::relative(p, root, ec);
  return ec ? p.generic_string() : r.generic_string();
}

void collect(scene::Scene& s, scene::GameObject* go, std::vector<scene::GameObject*>& out) {
  out.push_back(go);
  for (const scene::GameObjectId id : go->children()) {
    if (auto* c = s.get(id); c != nullptr && !c->markedForDestroy()) collect(s, c, out);
  }
}

bool isDescendantOrSelf(const scene::GameObject* root, const scene::GameObject* x) {
  for (const scene::GameObject* p = x; p != nullptr; p = p->parent()) {
    if (p == root) return true;
  }
  return false;
}

}  // namespace

const char* toString(AssetType t) noexcept {
  switch (t) {
    case AssetType::Model: return "Model";
    case AssetType::Audio: return "Audio";
    case AssetType::Texture: return "Texture";
    case AssetType::Script: return "Script";
    case AssetType::Scene: return "Scene";
    case AssetType::Other: return "Other";
  }
  return "Other";
}

// ---- creation -------------------------------------------------------------------------------
Result<std::unique_ptr<EditorDocument>> EditorDocument::create(rhi::Device* device, renderer::Renderer* renderer, const EditorConfig& cfg) {
  std::unique_ptr<EditorDocument> d(new EditorDocument());
  d->device_ = device;
  d->renderer_ = renderer;
  d->cfg_ = cfg;
  d->console_ = std::make_shared<ConsoleLog>();
  d->console_->install(d->console_);
  MINGOL_TRY_ASSIGN(d->runtime_, runtime::Runtime::create(cfg.runtime));
  d->registry_ = sceneio::ComponentRegistry::withBuiltins();
  MINGOL_TRY_ASSIGN(d->host_, scripting::ScriptHost::create({}));
  MINGOL_TRY(d->ensureBuiltins());
  d->assets_.clear();
  d->rescanAssetsBuiltinOnly();
  MINGOL_TRY(d->newScene(true));
  return d;
}

EditorDocument::~EditorDocument() {
  if (runtime_ && host_) {
    scripting::ScriptHost::Scope sc(*host_, &runtime_->scene());
    runtime_->scene().clear();
  }
  if (console_) ConsoleLog::uninstall(console_);
}

sceneio::SceneIoContext EditorDocument::ioContext() {
  sceneio::SceneIoContext c;
  c.assets = &assets_;
  c.scripts = host_.get();
  return c;
}

Result<void> EditorDocument::ensureBuiltins() {
  if (renderer_ == nullptr || builtins_.box != nullptr) return {};
  MINGOL_TRY_ASSIGN(builtins_, sceneio::BuiltinAssets::create(*renderer_));
  return {};
}

void EditorDocument::rescanAssetsBuiltinOnly() {
  assets_.clear();
  builtins_.addTo(assets_);
  for (const auto& [name, clip] : audio_) assets_.addAudioClip(name, clip);
  for (const auto& [path, m] : models_) addModelAssets(path, m);
}

void EditorDocument::addModelAssets(const std::string& path, const ModelAsset& m) { sceneio::addModelAssets(assets_, path, m.gpu); }

// ---- project --------------------------------------------------------------------------------
Result<void> EditorDocument::createProject(const fsys::path& dir, const std::string& name) {
  if (name.empty() || name.size() > package::kMaxProjectNameLength || name.find_first_of("/\\") != std::string::npos)
    return Error{ErrorCode::InvalidArgument, "invalid project name"};
  const fsys::path file = dir / (name + ".mindev");
  std::error_code ec;
  if (fsys::exists(file, ec)) return Error{ErrorCode::AlreadyExists, file.string() + " already exists"};
  for (const char* sub : {"assets", "scripts", "scenes"}) {
    fsys::create_directories(dir / sub, ec);
    if (ec) return Error{ErrorCode::Io, "cannot create " + (dir / sub).string() + ": " + ec.message()};
  }
  package::ProjectFile p = package::makeNewProject(name, kEngineVersion);
  MINGOL_TRY(package::saveMindev(file, p));
  MINGOL_TRY(openProject(file));
  MINGOL_TRY(newScene(true));
  MINGOL_TRY(saveSceneAs("scenes/main.minseen"));
  return {};
}

Result<void> EditorDocument::openProject(const fsys::path& mindev) {
  MINGOL_TRY_ASSIGN(package::ProjectFile p, package::loadMindev(mindev));
  std::error_code ec;
  const fsys::path abs = fsys::absolute(mindev, ec);
  {
    scripting::ScriptHost::Scope sc(*host_, &scene());
    scene().clear();
  }
  models_.clear();
  audio_.clear();
  selected_ = {};
  project_ = std::move(p);
  projectFile_ = abs;
  root_ = abs.parent_path();
  scenePath_.clear();
  if (auto r = rescanAssets(); !r.ok()) console_->add(LogLevel::Warn, "editor", "asset scan: " + r.error().describe());
  const ScriptDiagnostics sd = compileScripts();
  if (!sd.ok) console_->add(LogLevel::Error, "script", sd.failure.empty() ? std::to_string(sd.errors.size()) + " script error(s)" : sd.failure);
  if (!project_->entryScene.empty() && fsys::exists(root_ / project_->entryScene, ec)) {
    MINGOL_TRY(openScene(project_->entryScene));
  } else {
    MINGOL_TRY(newScene(true));
  }
  return {};
}

Result<void> EditorDocument::saveProject() {
  if (!project_) return Error{ErrorCode::InvalidArgument, "no project is open"};
  project_->engineVersion = kEngineVersion;
  return package::saveMindev(projectFile_, *project_);
}

Result<void> EditorDocument::rescanAssets() {
  assetList_.clear();
  if (!project_) {
    rescanAssetsBuiltinOnly();
    return {};
  }
  std::vector<std::string> dirs = project_->assetDirs;
  dirs.insert(dirs.end(), project_->scriptDirs.begin(), project_->scriptDirs.end());
  for (const std::string& d : dirs) {
    std::error_code ec;
    const fsys::path base = root_ / d;
    if (!fsys::is_directory(base, ec)) continue;
    std::vector<fsys::path> files;
    for (fsys::recursive_directory_iterator it(base, fsys::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file(ec)) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const fsys::path& f : files) {
      const std::string rel = relPath(root_, f);
      const AssetType t = typeOfExtension(lower(f.extension().string()));
      std::error_code sec;
      assetList_.push_back({rel, t, static_cast<std::uint64_t>(fsys::file_size(f, sec))});
      if (t == AssetType::Audio && audio_.count(rel) == 0) {
        auto bytes = fs::readFile(f);
        if (!bytes.ok()) { console_->add(LogLevel::Warn, "assets", rel + ": " + bytes.error().describe()); continue; }
        auto clip = assets::decodeAudio(bytes.value());
        if (!clip.ok()) { console_->add(LogLevel::Warn, "assets", rel + ": " + clip.error().describe()); continue; }
        audio_[rel] = std::make_shared<const assets::AudioClip>(std::move(clip).value());
      } else if (t == AssetType::Model && models_.count(rel) == 0) {
        if (renderer_ == nullptr) { console_->add(LogLevel::Warn, "assets", rel + ": no renderer, model skipped"); continue; }
        auto m = assets::loadGltfFile(f);
        if (!m.ok()) { console_->add(LogLevel::Warn, "assets", rel + ": " + m.error().describe()); continue; }
        if (auto r = registerModel(rel, std::move(m).value()); !r.ok()) console_->add(LogLevel::Warn, "assets", rel + ": " + r.error().describe());
      }
    }
  }
  for (const std::string& s : project_->scenes) {
    std::error_code ec;
    if (fsys::exists(root_ / s, ec)) assetList_.push_back({s, AssetType::Scene, static_cast<std::uint64_t>(fsys::file_size(root_ / s, ec))});
  }
  rescanAssetsBuiltinOnly();
  return {};
}

Result<void> EditorDocument::registerModel(const std::string& path, assets::ModelData model) {
  if (renderer_ == nullptr || device_ == nullptr) return Error{ErrorCode::Unsupported, "models need a GPU renderer"};
  MINGOL_TRY_ASSIGN(runtime::GpuModel gpu, runtime::uploadModel(*device_, *renderer_, model));
  ModelAsset ma{std::move(model), std::move(gpu)};
  addModelAssets(path, ma);
  models_[path] = std::move(ma);
  return {};
}

// ---- history --------------------------------------------------------------------------------
Result<std::string> EditorDocument::snapshot() {
  scripting::ScriptHost::Scope sc(*host_, &scene());
  sceneio::SaveOptions opt;
  opt.sceneName = scenePath_.empty() ? "untitled" : scenePath_;
  opt.pretty = false;
  return sceneio::saveScene(scene(), ioContext(), registry_, opt);
}

Result<void> EditorDocument::restore(const std::string& text, int selection) {
  scripting::ScriptHost::Scope sc(*host_, &scene());
  sceneio::LoadOptions lo;
  lo.replaceExisting = true;
  selected_ = {};
  auto r = sceneio::loadScene(scene(), text, ioContext(), registry_, lo);
  if (!r.ok()) return r.error();
  for (const std::string& w : r.value().warnings) console_->add(LogLevel::Warn, "scene", w);
  select(objectAtIndex(selection));
  return {};
}

Result<void> EditorDocument::loadSceneText(const std::string& text) { return restore(text, -1); }

void EditorDocument::resetHistory() {
  states_.clear();
  historyBytes_ = 0;
  cursor_ = 0;
  liveEdit_ = false;
  auto s = snapshot();
  if (!s.ok()) {
    console_->add(LogLevel::Error, "editor", "snapshot failed: " + s.error().describe());
    states_.push_back({"initial", {}, -1, nextId_++});
  } else {
    historyBytes_ = s.value().size();
    states_.push_back({"initial", std::move(s).value(), objectIndex(selected()), nextId_++});
  }
  savedId_ = states_[0].id;  // a freshly loaded/created scene is clean
}

void EditorDocument::trimHistory() {
  while (states_.size() > 1 && (states_.size() > cfg_.maxUndoSteps || historyBytes_ > cfg_.maxUndoBytes) && cursor_ > 0) {
    historyBytes_ -= states_.front().text.size();
    states_.erase(states_.begin());
    --cursor_;
  }
}

bool EditorDocument::commit(const std::string& label) {
  if (playing_) return false;
  auto s = snapshot();
  if (!s.ok()) {
    console_->add(LogLevel::Error, "editor", "snapshot failed: " + s.error().describe());
    return false;
  }
  liveEdit_ = false;
  if (!states_.empty() && states_[cursor_].text == s.value()) {
    states_[cursor_].selection = objectIndex(selected());
    return false;
  }
  while (states_.size() > cursor_ + 1) {
    historyBytes_ -= states_.back().text.size();
    states_.pop_back();
  }
  historyBytes_ += s.value().size();
  states_.push_back({label, std::move(s).value(), objectIndex(selected()), nextId_++});
  cursor_ = states_.size() - 1;
  trimHistory();
  return true;
}

bool EditorDocument::canUndo() const { return !playing_ && cursor_ > 0; }
bool EditorDocument::canRedo() const { return !playing_ && cursor_ + 1 < states_.size(); }
std::string EditorDocument::undoLabel() const { return canUndo() ? states_[cursor_].label : std::string(); }
std::string EditorDocument::redoLabel() const { return canRedo() ? states_[cursor_ + 1].label : std::string(); }

Result<void> EditorDocument::undo() {
  if (!canUndo()) return Error{ErrorCode::InvalidArgument, "nothing to undo"};
  // Uncommitted live edits are discarded together with the step (they are part of "now").
  MINGOL_TRY(restore(states_[cursor_ - 1].text, states_[cursor_ - 1].selection));
  --cursor_;
  liveEdit_ = false;
  return {};
}

Result<void> EditorDocument::redo() {
  if (!canRedo()) return Error{ErrorCode::InvalidArgument, "nothing to redo"};
  MINGOL_TRY(restore(states_[cursor_ + 1].text, states_[cursor_ + 1].selection));
  ++cursor_;
  liveEdit_ = false;
  return {};
}

bool EditorDocument::dirty() const {
  if (playing_) return playDirty_;
  if (liveEdit_) return true;
  return states_.empty() || states_[cursor_].id != savedId_;
}

// ---- selection ------------------------------------------------------------------------------
std::vector<scene::GameObject*> EditorDocument::allObjects() const {
  std::vector<scene::GameObject*> out;
  auto& s = const_cast<EditorDocument*>(this)->scene();
  for (scene::GameObject* r : s.rootObjects()) {
    if (!r->markedForDestroy()) collect(s, r, out);
  }
  return out;
}

int EditorDocument::objectIndex(const scene::GameObject* object) const {
  if (object == nullptr) return -1;
  const auto all = allObjects();
  for (std::size_t i = 0; i < all.size(); ++i)
    if (all[i] == object) return static_cast<int>(i);
  return -1;
}

scene::GameObject* EditorDocument::objectAtIndex(int index) const {
  if (index < 0) return nullptr;
  const auto all = allObjects();
  return static_cast<std::size_t>(index) < all.size() ? all[static_cast<std::size_t>(index)] : nullptr;
}

scene::GameObject* EditorDocument::selected() const {
  if (!selected_.valid()) return nullptr;
  auto* o = const_cast<EditorDocument*>(this)->scene().get(selected_);
  return (o != nullptr && !o->markedForDestroy()) ? o : nullptr;
}

void EditorDocument::select(scene::GameObject* object) { selected_ = object != nullptr ? object->id() : scene::GameObjectId{}; }

// ---- scenes ---------------------------------------------------------------------------------
void EditorDocument::populateDefaults() {
  auto* cam = scene().createObject("Main Camera");
  cam->addComponent<scene::CameraComponent>();
  cam->transform().setLocalPosition({0, 2, 6});
  cam->transform().setLocalRotation(Quat::fromAxisAngle({1, 0, 0}, -0.3F));
  auto* sun = scene().createObject("Directional Light");
  auto* l = sun->addComponent<scene::LightComponent>();
  l->type = renderer::LightType::Directional;
  l->castShadow = true;
  sun->transform().setLocalRotation(Quat::fromAxisAngle({1, 0, 0}, -0.9F));
}

Result<void> EditorDocument::newScene(bool withDefaults) {
  if (playing_) MINGOL_TRY(stop());
  {
    scripting::ScriptHost::Scope sc(*host_, &scene());
    scene().clear();
    scene().environment = scene::SceneEnvironment{};
    if (withDefaults) populateDefaults();
  }
  selected_ = {};
  scenePath_.clear();
  resetHistory();
  return {};
}

Result<void> EditorDocument::openScene(const std::string& relPathStr) {
  if (!project_) return Error{ErrorCode::InvalidArgument, "no project is open"};
  if (!package::isValidRelativePath(relPathStr)) return Error{ErrorCode::InvalidArgument, "invalid scene path"};
  if (playing_) MINGOL_TRY(stop());
  MINGOL_TRY_ASSIGN(std::string text, fs::readTextFile(root_ / relPathStr));
  MINGOL_TRY(restore(text, -1));
  scenePath_ = relPathStr;
  resetHistory();
  savedId_ = states_[0].id;
  return {};
}

Result<void> EditorDocument::saveScene() {
  if (scenePath_.empty()) return Error{ErrorCode::InvalidArgument, "the scene has no path yet (use Save As)"};
  return saveSceneAs(scenePath_);
}

Result<void> EditorDocument::saveSceneAs(const std::string& relPathStr) {
  if (!project_) return Error{ErrorCode::InvalidArgument, "no project is open"};
  if (!package::isValidRelativePath(relPathStr) || relPathStr.size() <= 8 || relPathStr.compare(relPathStr.size() - 8, 8, ".minseen") != 0)
    return Error{ErrorCode::InvalidArgument, "scene path must be project relative and end with .minseen"};
  if (playing_) return Error{ErrorCode::InvalidArgument, "stop play mode before saving"};
  commit("edit");  // make sure the state on disk is the state in history
  std::error_code ec;
  fsys::create_directories((root_ / relPathStr).parent_path(), ec);
  {
    scripting::ScriptHost::Scope sc(*host_, &scene());
    sceneio::SaveOptions opt;
    opt.sceneName = relPathStr;
    MINGOL_TRY(sceneio::saveSceneFile((root_ / relPathStr).string(), scene(), ioContext(), registry_, opt));
  }
  scenePath_ = relPathStr;
  savedId_ = states_[cursor_].id;
  auto& scenes = project_->scenes;
  bool changed = false;
  if (std::find(scenes.begin(), scenes.end(), relPathStr) == scenes.end()) { scenes.push_back(relPathStr); changed = true; }
  if (project_->entryScene.empty()) { project_->entryScene = relPathStr; changed = true; }
  if (changed) MINGOL_TRY(saveProject());
  return {};
}

// ---- scene operations -----------------------------------------------------------------------
scene::GameObject* EditorDocument::createEmpty(const std::string& name, scene::GameObject* parent) {
  scripting::ScriptHost::Scope sc(*host_, &scene());
  auto* go = scene().createObject(name.empty() ? "GameObject" : name, parent);
  select(go);
  commit("Create " + go->name());
  return go;
}

Result<scene::GameObject*> EditorDocument::createPrimitive(PrimitiveKind kind, scene::GameObject* parent) {
  if (builtins_.box == nullptr) return Error{ErrorCode::Unsupported, "primitives need a GPU renderer"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  const char* names[] = {"Box", "Sphere", "Plane"};
  auto* go = scene().createObject(names[static_cast<int>(kind)], parent);
  auto* mr = go->addComponent<scene::MeshRenderer>();
  mr->mesh = kind == PrimitiveKind::Box ? builtins_.box : kind == PrimitiveKind::Sphere ? builtins_.sphere : builtins_.plane;
  mr->material = builtins_.material;
  select(go);
  commit(std::string("Create ") + names[static_cast<int>(kind)]);
  return go;
}

scene::GameObject* EditorDocument::createCamera(scene::GameObject* parent) {
  scripting::ScriptHost::Scope sc(*host_, &scene());
  auto* go = scene().createObject("Camera", parent);
  go->addComponent<scene::CameraComponent>();
  select(go);
  commit("Create Camera");
  return go;
}

scene::GameObject* EditorDocument::createLight(renderer::LightType type, scene::GameObject* parent) {
  scripting::ScriptHost::Scope sc(*host_, &scene());
  const char* n = type == renderer::LightType::Directional ? "Directional Light" : type == renderer::LightType::Point ? "Point Light" : "Spot Light";
  auto* go = scene().createObject(n, parent);
  auto* l = go->addComponent<scene::LightComponent>();
  l->type = type;
  if (type == renderer::LightType::Directional) go->transform().setLocalRotation(Quat::fromAxisAngle({1, 0, 0}, -0.9F));
  else go->transform().setLocalPosition({0, 3, 0});
  select(go);
  commit(std::string("Create ") + n);
  return go;
}

void EditorDocument::cloneInto(scene::GameObject& src, scene::GameObject* parent, scene::GameObject*& out, Result<void>& status) {
  auto* go = scene().createObject(src.name(), parent);
  out = go;
  go->setActive(false);  // no callbacks until fully built
  go->transform().setLocal(src.transform().localPosition(), src.transform().localRotation(), src.transform().localScale());
  const sceneio::SceneIoContext ctx = ioContext();
  src.forEachComponent([&](scene::Component& c) {
    if (!status.ok()) return;
    auto saved = registry_.save(c, ctx);
    if (!saved.ok()) { status = saved.error(); return; }
    sceneio::LoadReport rep;
    if (auto r = registry_.load(c.typeName(), *go, saved.value(), ctx, rep); !r.ok()) status = r.error();
    else for (const std::string& w : rep.warnings) console_->add(LogLevel::Warn, "scene", w);
  });
  const std::vector<scene::GameObjectId> kids = src.children();
  for (const scene::GameObjectId id : kids) {
    if (!status.ok()) return;
    auto* k = scene().get(id);
    if (k == nullptr || k->markedForDestroy()) continue;
    scene::GameObject* dummy = nullptr;
    cloneInto(*k, go, dummy, status);
  }
  go->setActive(src.activeSelf());
}

Result<scene::GameObject*> EditorDocument::duplicate(scene::GameObject* object) {
  if (object == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  scene::GameObject* copy = nullptr;
  Result<void> status;
  cloneInto(*object, object->parent(), copy, status);
  if (!status.ok()) {
    if (copy != nullptr) { scene().destroy(copy); scene().flushDestroyed(); }
    return status.error();
  }
  select(copy);
  commit("Duplicate " + copy->name());
  return copy;
}

Result<void> EditorDocument::deleteObject(scene::GameObject* object) {
  if (object == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  const std::string name = object->name();
  scene().destroy(object);
  scene().flushDestroyed();
  commit("Delete " + name);
  return {};
}

Result<void> EditorDocument::reparent(scene::GameObject* object, scene::GameObject* newParent, bool keepWorld) {
  if (object == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  if (newParent != nullptr && isDescendantOrSelf(object, newParent)) return Error{ErrorCode::InvalidArgument, "cannot parent an object under itself or its descendants"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  MINGOL_TRY(object->setParent(newParent, keepWorld));
  commit("Reparent " + object->name());
  return {};
}

Result<void> EditorDocument::rename(scene::GameObject* object, const std::string& name) {
  if (object == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  if (name.empty()) return Error{ErrorCode::InvalidArgument, "name must not be empty"};
  object->setName(name);
  commit("Rename to " + name);
  return {};
}

Result<void> EditorDocument::setActive(scene::GameObject* object, bool active) {
  if (object == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  object->setActive(active);
  commit(std::string(active ? "Activate " : "Deactivate ") + object->name());
  return {};
}

Result<scene::Component*> EditorDocument::addComponent(scene::GameObject* go, const std::string& type) {
  if (go == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  scene::Component* c = nullptr;
  if (type == "Camera") c = go->addComponent<scene::CameraComponent>();
  else if (type == "Light") c = go->addComponent<scene::LightComponent>();
  else if (type == "MeshRenderer") c = go->addComponent<scene::MeshRenderer>();
  else if (type == "Animator") c = go->addComponent<scene::Animator>();
  else if (type == "Rigidbody") c = go->addComponent<runtime::Rigidbody>();
  else if (type == "Collider") c = runtime::Collider::addBox(*go, {0.5F, 0.5F, 0.5F});
  else if (type == "AudioSource") c = go->addComponent<runtime::AudioSource>();
  else if (type == "AudioListener") c = go->addComponent<runtime::AudioListener>();
  else return Error{ErrorCode::InvalidArgument, "unknown component type '" + type + "'"};
  commit("Add " + type);
  return c;
}

Result<scripting::ScriptComponent*> EditorDocument::addScript(scene::GameObject* go, const std::string& className) {
  if (go == nullptr) return Error{ErrorCode::InvalidArgument, "no object"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  MINGOL_TRY_ASSIGN(scripting::ScriptComponent * c, host_->addScript(*go, className));
  commit("Add script " + className);
  return c;
}

Result<void> EditorDocument::removeComponent(scene::GameObject* go, scene::Component* component) {
  if (go == nullptr || component == nullptr) return Error{ErrorCode::InvalidArgument, "no object/component"};
  bool owned = false;
  go->forEachComponent([&](scene::Component& c) { owned = owned || &c == component; });
  if (!owned) return Error{ErrorCode::InvalidArgument, "component does not belong to the object"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  const std::string t = component->typeName();
  go->removeComponent(component);
  scene().flushDestroyed();
  commit("Remove " + t);
  return {};
}

Result<scene::GameObject*> EditorDocument::instantiateModelAsset(const std::string& path, scene::GameObject* parent) {
  const auto it = models_.find(path);
  if (it == models_.end()) return Error{ErrorCode::NotFound, "model '" + path + "' is not loaded"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  const std::string stem = fsys::path(path).stem().string();
  MINGOL_TRY_ASSIGN(runtime::ModelInstance inst, runtime::instantiateModel(scene(), it->second.data, it->second.gpu, stem.empty() ? "model" : stem, parent));
  select(inst.root);
  commit("Instantiate " + stem);
  return inst.root;
}

Result<void> EditorDocument::setProperty(const Property& property, const PropValue& value) {
  if (!property.set) return Error{ErrorCode::InvalidArgument, "property '" + property.name + "' is read-only"};
  scripting::ScriptHost::Scope sc(*host_, &scene());
  MINGOL_TRY(property.set(value));
  liveEdit_ = true;
  return {};
}

// ---- play mode ------------------------------------------------------------------------------
Result<void> EditorDocument::play() {
  if (playing_) return Error{ErrorCode::InvalidArgument, "already playing"};
  commit("edit");
  MINGOL_TRY_ASSIGN(playSnapshot_, snapshot());
  playSelection_ = objectIndex(selected());
  playing_ = true;
  paused_ = false;
  playDirty_ = dirty_before_play();
  playTime_ = 0;
  runtime_->setTimeScale(1.0F);
  return {};
}

bool EditorDocument::dirty_before_play() const { return liveEdit_ || states_.empty() || states_[cursor_].id != savedId_; }

Result<void> EditorDocument::stop() {
  if (!playing_) return Error{ErrorCode::InvalidArgument, "not playing"};
  playing_ = false;
  paused_ = false;
  MINGOL_TRY(restore(playSnapshot_, playSelection_));
  playSnapshot_.clear();
  liveEdit_ = false;
  return {};
}

void EditorDocument::tick(float dt) {
  if (!playing_ || paused_) return;
  scripting::ScriptHost::Scope sc(*host_, &scene());
  runtime_->tick(dt);
  playTime_ += dt;
}

void EditorDocument::stepFrame(float dt) {
  if (!playing_) return;
  scripting::ScriptHost::Scope sc(*host_, &scene());
  runtime_->tick(dt);
  playTime_ += dt;
}

// ---- scripts --------------------------------------------------------------------------------
std::vector<std::string> EditorDocument::scriptFiles() const {
  std::vector<std::string> out;
  if (!project_) return out;
  for (const std::string& d : project_->scriptDirs) {
    std::error_code ec;
    const fsys::path base = root_ / d;
    if (!fsys::is_directory(base, ec)) continue;
    for (fsys::recursive_directory_iterator it(base, fsys::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file(ec) && it->path().extension() == ".mins") out.push_back(relPath(root_, it->path()));
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

EditorDocument::ScriptDiagnostics EditorDocument::compileScripts(const std::map<std::string, std::string>& overrides) {
  ScriptDiagnostics result;
  std::map<std::string, std::string> texts;
  for (const std::string& f : scriptFiles()) {
    auto t = fs::readTextFile(root_ / f);
    if (!t.ok()) {
      result.ok = false;
      result.failure = f + ": " + t.error().describe();
      return result;
    }
    texts[f] = std::move(t).value();
  }
  for (const auto& [k, v] : overrides) texts[k] = v;
  std::vector<script::SourceInput> sources;
  for (auto& [k, v] : texts) sources.push_back({k, v});

  // Structured diagnostics first (the host reports them only as text).
  {
    std::vector<script::SourceInput> all;
    all.push_back({"<engine-api>", scripting::engineApiSource()});
    all.insert(all.end(), sources.begin(), sources.end());
    script::Diagnostics diags;
    auto program = script::compile(all, diags);
    if (program == nullptr) {
      result.ok = false;
      result.errors = diags.errors();
      for (const auto& e : result.errors) console_->add(LogLevel::Error, "script", e.format());
      return result;
    }
  }
  scripting::ScriptHost::Scope sc(*host_, &scene());
  auto rep = host_->reload(sources);
  if (!rep.ok()) {
    result.ok = false;
    result.failure = rep.error().describe();
    console_->add(LogLevel::Error, "script", result.failure);
    return result;
  }
  for (const std::string& m : rep.value().messages) console_->add(LogLevel::Info, "script", m);
  if (rep.value().componentsDisabled > 0)
    console_->add(LogLevel::Warn, "script", std::to_string(rep.value().componentsDisabled) + " script component(s) disabled by reload");
  return result;
}

}  // namespace mingol::editor
