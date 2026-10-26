// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/sceneio/scene_io.hpp"

#include <cstdio>
#include <unordered_map>

#include "json_util.hpp"
#include "mingol/core/fs.hpp"
#include "mingol/core/hash.hpp"

namespace mingol::sceneio {

namespace j = ju;
using json::Value;

namespace {

constexpr const char* kFormatTag = "minseen";

Result<std::string> integrityOf(const json::Object& body) {
  json::Object copy = body;
  copy.erase("integrity");
  auto text = json::write(Value(std::move(copy)), {.pretty = false});
  if (!text.ok()) return Error(text.error().code, "cannot write the scene: " + text.error().message);
  const std::string& s = text.value();
  char buf[32];
  std::snprintf(buf, sizeof buf, "crc32:%08x", crc32({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()}));
  return std::string(buf);
}

}  // namespace

Result<void> ComponentRegistry::registerType(std::string typeName, SaveFn save, LoadFn load) {
  if (typeName.empty()) return makeError(ErrorCode::InvalidArgument, "component type name must not be empty");
  if (!save || !load) return makeError(ErrorCode::InvalidArgument, "component type '{}' needs both a save and a load function", typeName);
  if (types_.count(typeName) != 0) return makeError(ErrorCode::AlreadyExists, "component type '{}' is already registered", typeName);
  types_[std::move(typeName)] = Entry{std::move(save), std::move(load)};
  return {};
}

Result<Value> ComponentRegistry::save(const scene::Component& c, const SceneIoContext& ctx) const {
  const auto it = types_.find(c.typeName());
  if (it == types_.end()) return makeError(ErrorCode::Unsupported, "component type '{}' is not registered for serialisation", c.typeName());
  return it->second.save(c, ctx);
}

Result<void> ComponentRegistry::load(const std::string& typeName, scene::GameObject& go, const Value& data, const SceneIoContext& ctx,
                                     LoadReport& report) const {
  const auto it = types_.find(typeName);
  if (it == types_.end()) return makeError(ErrorCode::Unsupported, ".minseen: unknown component type '{}'", typeName);
  return it->second.load(go, data, ctx, report);
}

// ---- saving ----------------------------------------------------------------------------------------------

Result<std::string> saveScene(const scene::Scene& scene, const SceneIoContext& ctx, const ComponentRegistry& registry,
                              const SaveOptions& options) {
  json::Array objects;
  std::unordered_map<std::uint64_t, std::int64_t> fileIds;  // packed GameObjectId -> id in the file
  auto pack = [](scene::GameObjectId id) { return (static_cast<std::uint64_t>(id.index) << 32) | id.generation; };

  struct Walker {
    const scene::Scene& scene;
    const SceneIoContext& ctx;
    const ComponentRegistry& registry;
    json::Array& objects;
    std::unordered_map<std::uint64_t, std::int64_t>& ids;
    decltype(pack)& packFn;

    Result<void> visit(scene::GameObject& go, std::int64_t parentFileId) {
      if (go.markedForDestroy()) return {};
      const std::int64_t id = static_cast<std::int64_t>(objects.size()) + 1;
      ids[packFn(go.id())] = id;
      json::Object o;
      o.set("id", id);
      o.set("name", go.name());
      o.set("parent", parentFileId);
      o.set("active", go.activeSelf());
      o.set("position", ju::vec3(go.transform().localPosition()));
      o.set("rotation", ju::quat(go.transform().localRotation()));
      o.set("scale", ju::vec3(go.transform().localScale()));
      json::Array comps;
      Result<void> err;
      go.forEachComponent([&](scene::Component& c) {
        if (!err.ok()) return;
        auto data = registry.save(c, ctx);
        if (!data.ok()) {
          err = Error(data.error().code, "object '" + go.name() + "', component '" + c.typeName() + "': " + data.error().message);
          return;
        }
        json::Object co;
        co.set("type", c.typeName());
        co.set("enabled", c.enabled());
        co.set("data", std::move(data).value());
        comps.push_back(Value(std::move(co)));
      });
      MINGOL_TRY(err);
      o.set("components", Value(std::move(comps)));
      objects.push_back(Value(std::move(o)));
      for (const scene::GameObjectId cid : go.children()) {
        if (scene::GameObject* child = scene.get(cid)) MINGOL_TRY(visit(*child, id));
      }
      return {};
    }
  } walker{scene, ctx, registry, objects, fileIds, pack};

  for (scene::GameObject* root : scene.rootObjects()) MINGOL_TRY(walker.visit(*root, 0));

  json::Object doc;
  doc.set("format", kFormatTag);
  doc.set("formatVersion", kMinseenFormatVersion);
  doc.set("name", options.sceneName);
  json::Object env;
  env.set("ambient", ju::vec3(scene.environment.ambient));
  env.set("clearColor", Value(json::Array{ju::num(scene.environment.clearColor[0]), ju::num(scene.environment.clearColor[1]),
                                          ju::num(scene.environment.clearColor[2]), ju::num(scene.environment.clearColor[3])}));
  doc.set("environment", Value(std::move(env)));
  doc.set("objects", Value(std::move(objects)));
  MINGOL_TRY_ASSIGN(std::string integrity, integrityOf(doc));
  doc.set("integrity", std::move(integrity));
  auto text = json::write(Value(std::move(doc)), {.pretty = options.pretty});
  if (!text.ok()) return Error(text.error().code, "cannot write the scene: " + text.error().message);
  return std::move(text).value() + "\n";
}

// ---- loading ---------------------------------------------------------------------------------------------

namespace {

struct ObjectSpec {
  std::int64_t id = 0;
  std::int64_t parent = 0;
  std::string name;
  bool active = true;
  Vec3 position, scale{1, 1, 1};
  Quat rotation;
  struct Comp { std::string type; bool enabled = true; const Value* data = nullptr; };
  std::vector<Comp> comps;
};

Result<std::vector<ObjectSpec>> parseObjects(const json::Object& doc, const LoadOptions& options) {
  const Value* ov = doc.find("objects");
  if (ov == nullptr || !ov->isArray()) return j::corrupt("'objects' must be an array");
  const auto& arr = ov->asArray();
  if (arr.size() > options.maxObjects) return j::corrupt("too many objects (" + std::to_string(arr.size()) + ", limit " + std::to_string(options.maxObjects) + ")");
  std::vector<ObjectSpec> specs;
  specs.reserve(arr.size());
  std::unordered_map<std::int64_t, bool> seen;
  for (std::size_t i = 0; i < arr.size(); ++i) {
    const std::string where = "objects[" + std::to_string(i) + "]";
    MINGOL_TRY_ASSIGN(const json::Object* o, j::object(arr[i], where));
    ObjectSpec s;
    MINGOL_TRY_ASSIGN(s.id, j::getInt(*o, "id", 0, where));
    if (s.id < 1) return j::corrupt(where + ".id must be an integer >= 1");
    if (seen.count(s.id) != 0) return j::corrupt(where + ".id " + std::to_string(s.id) + " is used twice");
    MINGOL_TRY_ASSIGN(s.parent, j::getInt(*o, "parent", 0, where));
    if (s.parent != 0 && seen.count(s.parent) == 0) return j::corrupt(where + ".parent " + std::to_string(s.parent) + " does not refer to an earlier object");
    MINGOL_TRY_ASSIGN(s.name, j::getString(*o, "name", "GameObject", where));
    MINGOL_TRY_ASSIGN(s.active, j::getBool(*o, "active", true, where));
    MINGOL_TRY_ASSIGN(s.position, j::getVec3(*o, "position", Vec3(0, 0, 0), false, where));
    MINGOL_TRY_ASSIGN(s.rotation, j::getQuat(*o, "rotation", Quat{}, false, where));
    MINGOL_TRY_ASSIGN(s.scale, j::getVec3(*o, "scale", Vec3(1, 1, 1), false, where));
    const float qlen = std::sqrt(s.rotation.x * s.rotation.x + s.rotation.y * s.rotation.y + s.rotation.z * s.rotation.z + s.rotation.w * s.rotation.w);
    if (!(qlen > 1e-6F)) return j::corrupt(where + ".rotation must not be the zero quaternion");
    if (const Value* cv = o->find("components")) {
      if (!cv->isArray()) return j::corrupt(where + ".components must be an array");
      for (std::size_t k = 0; k < cv->asArray().size(); ++k) {
        const std::string cw = where + ".components[" + std::to_string(k) + "]";
        MINGOL_TRY_ASSIGN(const json::Object* co, j::object(cv->asArray()[k], cw));
        ObjectSpec::Comp c;
        MINGOL_TRY_ASSIGN(c.type, j::getString(*co, "type", "", cw));
        if (c.type.empty()) return j::corrupt(cw + ".type is required");
        MINGOL_TRY_ASSIGN(c.enabled, j::getBool(*co, "enabled", true, cw));
        c.data = co->find("data");
        if (c.data == nullptr) return j::corrupt(cw + ".data is required");
        s.comps.push_back(std::move(c));
      }
    }
    seen[s.id] = true;
    specs.push_back(std::move(s));
  }
  return specs;
}

}  // namespace

Result<LoadReport> loadScene(scene::Scene& scene, std::string_view text, const SceneIoContext& ctx, const ComponentRegistry& registry,
                             const LoadOptions& options) {
  auto parsed = json::parse(text);
  if (!parsed.ok()) return Error(parsed.error().code, ".minseen: " + parsed.error().message);
  const Value& root = parsed.value();
  if (!root.isObject()) return j::corrupt("the document must be a JSON object");
  const json::Object& doc = root.asObject();

  MINGOL_TRY_ASSIGN(std::string tag, j::getString(doc, "format", "", "document"));
  if (tag != kFormatTag) return j::corrupt("'format' is \"" + tag + "\", expected \"minseen\"");
  const Value* ver = doc.find("formatVersion");
  if (ver == nullptr || !ver->isInt() || ver->asInt() < 1) return j::corrupt("'formatVersion' must be an integer >= 1");
  if (ver->asInt() > kMinseenFormatVersion) {
    return makeError(ErrorCode::VersionMismatch, ".minseen: file format version {} is newer than this engine supports ({}); update Mingol", ver->asInt(),
                     kMinseenFormatVersion);
  }
  if (options.verifyIntegrity) {
    if (const Value* iv = doc.find("integrity")) {
      if (!iv->isString()) return j::corrupt("'integrity' must be a string");
      auto expect = integrityOf(doc);
      if (!expect.ok() || iv->asString() != expect.value()) return Error(ErrorCode::Corrupt, ".minseen: integrity check failed (the file was modified or damaged)");
    }
  }

  Vec3 ambient = scene.environment.ambient;
  float clear[4] = {scene.environment.clearColor[0], scene.environment.clearColor[1], scene.environment.clearColor[2], scene.environment.clearColor[3]};
  if (const Value* ev = doc.find("environment")) {
    MINGOL_TRY_ASSIGN(const json::Object* eo, j::object(*ev, "environment"));
    MINGOL_TRY_ASSIGN(ambient, j::getVec3(*eo, "ambient", ambient, false, "environment"));
    if (eo->contains("clearColor")) {
      MINGOL_TRY_ASSIGN(auto cc, j::getFloats(*eo, "clearColor", 4, "environment"));
      for (int i = 0; i < 4; ++i) clear[i] = cc[static_cast<std::size_t>(i)];
    }
  }

  MINGOL_TRY_ASSIGN(std::vector<ObjectSpec> specs, parseObjects(doc, options));

  // Unknown component types are detected before anything is created.
  LoadReport report;
  for (const ObjectSpec& s : specs) {
    for (const auto& c : s.comps) {
      if (!registry.has(c.type) && !options.skipUnknownComponents) {
        return makeError(ErrorCode::Unsupported, ".minseen: object '{}' has unknown component type '{}'", s.name, c.type);
      }
    }
  }

  if (options.replaceExisting) scene.clear();

  std::vector<scene::GameObject*> created;
  std::vector<scene::GameObject*> roots;
  std::unordered_map<std::int64_t, scene::GameObject*> byId;
  auto rollback = [&]() {
    for (scene::GameObject* r : roots) scene.destroy(r);
    scene.flushDestroyed();
  };

  for (const ObjectSpec& s : specs) {
    scene::GameObject* parent = s.parent != 0 ? byId[s.parent] : nullptr;
    scene::GameObject* go = scene.createObject(s.name, parent);
    if (go == nullptr) {
      rollback();
      return makeError(ErrorCode::Internal, ".minseen: could not create object '{}'", s.name);
    }
    go->setActive(false);  // no component callback may run on a half-built scene
    created.push_back(go);
    byId[s.id] = go;
    if (parent == nullptr) roots.push_back(go);
    go->transform().setLocal(s.position, normalize(s.rotation), s.scale);
    for (const auto& c : s.comps) {
      if (!registry.has(c.type)) {
        report.warnings.push_back("object '" + s.name + "': unknown component type '" + c.type + "' skipped");
        continue;
      }
      const std::size_t before = go->componentCount();
      const auto r = registry.load(c.type, *go, *c.data, ctx, report);
      if (!r.ok()) {
        rollback();
        return Error(r.error().code, "object '" + s.name + "', component '" + c.type + "': " + r.error().message);
      }
      if (go->componentCount() != before + 1) {
        rollback();
        return makeError(ErrorCode::Internal, "the loader of component type '{}' must add exactly one component", c.type);
      }
      scene::Component* last = nullptr;
      go->forEachComponent([&](scene::Component& comp) { last = &comp; });
      if (!c.enabled && last != nullptr) last->setEnabled(false);
      ++report.componentsCreated;
    }
    ++report.objectsCreated;
  }

  // Everything built and validated: apply the environment and let objects become active (parents first).
  scene.environment.ambient = ambient;
  for (int i = 0; i < 4; ++i) scene.environment.clearColor[i] = clear[i];
  for (std::size_t i = 0; i < specs.size(); ++i) created[i]->setActive(specs[i].active);
  return report;
}

Result<void> saveSceneFile(const std::string& path, const scene::Scene& scene, const SceneIoContext& ctx, const ComponentRegistry& registry,
                           const SaveOptions& options) {
  MINGOL_TRY_ASSIGN(std::string text, saveScene(scene, ctx, registry, options));
  return fs::writeFileAtomic(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

Result<LoadReport> loadSceneFile(const std::string& path, scene::Scene& scene, const SceneIoContext& ctx, const ComponentRegistry& registry,
                                 const LoadOptions& options) {
  MINGOL_TRY_ASSIGN(auto bytes, fs::readFile(path, 256u << 20));
  return loadScene(scene, {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, ctx, registry, options);
}

}  // namespace mingol::sceneio
