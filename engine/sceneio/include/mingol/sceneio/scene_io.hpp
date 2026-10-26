// SPDX-License-Identifier: GPL-2.0-only
// .minseen — the JSON text scene file. Saves/loads a Scene (GameObject hierarchy, transforms, components,
// environment). See docs/MINSEEN.md for the format.
//
// Assets are referenced by *name*; the AssetTable maps names to loaded assets (and back when saving). Script
// components are saved as class name + data fields and recreated through a ScriptHost.
//
// Loading is all-or-nothing: the document is validated first, objects are created inactive (so no script or
// component callback runs on half-built state), and any failure destroys everything the load created.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mingol/assets/audio.hpp"
#include "mingol/core/error.hpp"
#include "mingol/core/json.hpp"
#include "mingol/physics/physics.hpp"
#include "mingol/renderer/animation.hpp"
#include "mingol/renderer/renderer.hpp"
#include "mingol/scene/scene.hpp"
#include "mingol/scripting/script_host.hpp"

namespace mingol::sceneio {

inline constexpr int kMinseenFormatVersion = 1;

// Named loaded assets, shared between scene saving and loading.
class AssetTable {
 public:
  void addMesh(std::string name, std::shared_ptr<const renderer::Mesh> v) { add(meshes_, std::move(name), std::move(v)); }
  void addMaterial(std::string name, std::shared_ptr<const renderer::Material> v) { add(materials_, std::move(name), std::move(v)); }
  void addAudioClip(std::string name, std::shared_ptr<const assets::AudioClip> v) { add(clips_, std::move(name), std::move(v)); }
  void addSkeleton(std::string name, std::shared_ptr<const renderer::Skeleton> v) { add(skeletons_, std::move(name), std::move(v)); }
  void addAnimationClip(std::string name, std::shared_ptr<const renderer::AnimationClip> v) { add(anims_, std::move(name), std::move(v)); }
  void addCollisionMesh(std::string name, std::shared_ptr<const physics::MeshShapeData> v) { add(collision_, std::move(name), std::move(v)); }

  // Sorted names of everything registered (editor asset pickers).
  std::vector<std::string> meshNames() const { return keys(meshes_); }
  std::vector<std::string> materialNames() const { return keys(materials_); }
  std::vector<std::string> audioClipNames() const { return keys(clips_); }
  std::vector<std::string> skeletonNames() const { return keys(skeletons_); }
  std::vector<std::string> animationClipNames() const { return keys(anims_); }
  std::vector<std::string> collisionMeshNames() const { return keys(collision_); }
  void clear() { meshes_.clear(); materials_.clear(); clips_.clear(); skeletons_.clear(); anims_.clear(); collision_.clear(); }

  std::shared_ptr<const renderer::Mesh> mesh(const std::string& n) const { return find(meshes_, n); }
  std::shared_ptr<const renderer::Material> material(const std::string& n) const { return find(materials_, n); }
  std::shared_ptr<const assets::AudioClip> audioClip(const std::string& n) const { return find(clips_, n); }
  std::shared_ptr<const renderer::Skeleton> skeleton(const std::string& n) const { return find(skeletons_, n); }
  std::shared_ptr<const renderer::AnimationClip> animationClip(const std::string& n) const { return find(anims_, n); }
  std::shared_ptr<const physics::MeshShapeData> collisionMesh(const std::string& n) const { return find(collision_, n); }

  // Reverse lookups; empty string when the pointer is not registered.
  std::string nameOf(const std::shared_ptr<const renderer::Mesh>& p) const { return rev(meshes_, p); }
  std::string nameOf(const std::shared_ptr<const renderer::Material>& p) const { return rev(materials_, p); }
  std::string nameOf(const std::shared_ptr<const assets::AudioClip>& p) const { return rev(clips_, p); }
  std::string nameOf(const std::shared_ptr<const renderer::Skeleton>& p) const { return rev(skeletons_, p); }
  std::string nameOf(const std::shared_ptr<const renderer::AnimationClip>& p) const { return rev(anims_, p); }
  std::string nameOf(const std::shared_ptr<const physics::MeshShapeData>& p) const { return rev(collision_, p); }

 private:
  template <class T> using Map = std::map<std::string, std::shared_ptr<const T>>;
  template <class T> static std::vector<std::string> keys(const Map<T>& m) {
    std::vector<std::string> out;
    for (const auto& kv : m) out.push_back(kv.first);
    return out;
  }
  template <class T> static void add(Map<T>& m, std::string n, std::shared_ptr<const T> v) { m[std::move(n)] = std::move(v); }
  template <class T> static std::shared_ptr<const T> find(const Map<T>& m, const std::string& n) {
    const auto it = m.find(n);
    return it == m.end() ? nullptr : it->second;
  }
  template <class T> static std::string rev(const Map<T>& m, const std::shared_ptr<const T>& p) {
    if (p == nullptr) return {};
    for (const auto& [k, v] : m) if (v == p) return k;
    return {};
  }
  Map<renderer::Mesh> meshes_;
  Map<renderer::Material> materials_;
  Map<assets::AudioClip> clips_;
  Map<renderer::Skeleton> skeletons_;
  Map<renderer::AnimationClip> anims_;
  Map<physics::MeshShapeData> collision_;
};

struct SceneIoContext {
  const AssetTable* assets = nullptr;        // required when the scene references assets
  scripting::ScriptHost* scripts = nullptr;  // required when the scene has Script components
};

struct LoadReport {
  std::size_t objectsCreated = 0;
  std::size_t componentsCreated = 0;
  std::vector<std::string> warnings;  // ignored fields, skipped unknown components, ...
};

// Per-component-type (de)serialisation. Register custom component types to make them saveable.
class ComponentRegistry {
 public:
  using SaveFn = std::function<Result<json::Value>(const scene::Component&, const SceneIoContext&)>;
  // Creates the component on `go` from `data`. Must not leave a half-initialised component on failure.
  using LoadFn = std::function<Result<void>(scene::GameObject& go, const json::Value& data, const SceneIoContext&, LoadReport&)>;

  // Registry with Camera, Light, MeshRenderer, Animator, Rigidbody, Collider, AudioSource, AudioListener, Script.
  static ComponentRegistry withBuiltins();

  Result<void> registerType(std::string typeName, SaveFn save, LoadFn load);
  bool has(const std::string& typeName) const { return types_.count(typeName) != 0; }
  Result<json::Value> save(const scene::Component& c, const SceneIoContext& ctx) const;
  Result<void> load(const std::string& typeName, scene::GameObject& go, const json::Value& data, const SceneIoContext& ctx, LoadReport& report) const;

 private:
  struct Entry { SaveFn save; LoadFn load; };
  std::map<std::string, Entry> types_;
};

struct SaveOptions {
  std::string sceneName;
  bool pretty = true;
};
struct LoadOptions {
  bool verifyIntegrity = true;
  bool skipUnknownComponents = false;  // true: unknown component types become warnings instead of errors
  bool replaceExisting = false;        // true: Scene::clear() once the document has been validated
  std::size_t maxObjects = 1u << 20;
};

// Serialises every live (not pending destruction) object, parents before children.
Result<std::string> saveScene(const scene::Scene& scene, const SceneIoContext& ctx, const ComponentRegistry& registry,
                              const SaveOptions& options = {});
Result<LoadReport> loadScene(scene::Scene& scene, std::string_view text, const SceneIoContext& ctx, const ComponentRegistry& registry,
                             const LoadOptions& options = {});

Result<void> saveSceneFile(const std::string& path, const scene::Scene& scene, const SceneIoContext& ctx, const ComponentRegistry& registry,
                           const SaveOptions& options = {});
Result<LoadReport> loadSceneFile(const std::string& path, scene::Scene& scene, const SceneIoContext& ctx, const ComponentRegistry& registry,
                                 const LoadOptions& options = {});

}  // namespace mingol::sceneio
