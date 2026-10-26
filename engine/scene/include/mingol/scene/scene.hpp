// SPDX-License-Identifier: GPL-2.0-only
// GameObject + Component scene: a hierarchy of named objects, each with a Transform and any number
// of Components that receive lifecycle callbacks.
//
// Ownership: the Scene owns its GameObjects; a GameObject owns its Components. References to
// objects are GameObjectId handles (safe after destruction: they simply stop resolving) or raw
// pointers that stay valid until the object is destroyed (destruction happens at flush points, not
// in the middle of a callback).
//
// Threading: a Scene is single-threaded. Use it from the thread that runs the game loop.
//
// Callback order for one component: onAwake (once, when the object first becomes active in the
// hierarchy) -> onEnable -> onStart (once, before its first fixed/update) -> onFixedUpdate /
// onUpdate / onLateUpdate each frame -> onDisable -> onDestroy.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/scene/transform.hpp"

namespace mingol::scene {

class Scene;
class GameObject;

struct GameObjectId {
  std::uint32_t index = 0;       // slot + 1; 0 means "no object"
  std::uint32_t generation = 0;
  bool valid() const noexcept { return index != 0; }
  friend bool operator==(GameObjectId a, GameObjectId b) { return a.index == b.index && a.generation == b.generation; }
};

class Component {
 public:
  Component() { ++liveCount_; }
  Component(const Component&) = delete;
  Component& operator=(const Component&) = delete;
  virtual ~Component() { --liveCount_; }

  // Stable name used by serialisation and scripting. Must be unique per component class.
  virtual const char* typeName() const = 0;

  GameObject& gameObject() const noexcept { return *owner_; }
  Scene& scene() const noexcept;
  bool enabled() const noexcept { return enabled_; }
  void setEnabled(bool enabled);
  // enabled() and the owner is active in the hierarchy.
  bool activeAndEnabled() const noexcept;

  // Number of Component instances alive in the process (leak checks in tests).
  static std::int64_t liveCount() noexcept { return liveCount_.load(); }

 protected:
  virtual void onAwake() {}
  virtual void onEnable() {}
  virtual void onStart() {}
  virtual void onFixedUpdate(float /*dt*/) {}
  virtual void onUpdate(float /*dt*/) {}
  virtual void onLateUpdate(float /*dt*/) {}
  virtual void onDisable() {}
  virtual void onDestroy() {}

 private:
  friend class GameObject;
  friend class Scene;
  GameObject* owner_ = nullptr;
  bool enabled_ = true;
  bool awoken_ = false;
  bool started_ = false;
  bool enableCalled_ = false;  // onEnable has run and onDisable has not
  bool destroyed_ = false;
  static inline std::atomic<std::int64_t> liveCount_{0};
};

class GameObject {
 public:
  GameObject(const GameObject&) = delete;
  GameObject& operator=(const GameObject&) = delete;
  ~GameObject() { --liveCount_; }

  GameObjectId id() const noexcept { return id_; }
  const std::string& name() const noexcept { return name_; }
  void setName(std::string name) { name_ = std::move(name); }
  Scene& scene() const noexcept { return *scene_; }
  Transform& transform() noexcept { return transform_; }
  const Transform& transform() const noexcept { return transform_; }

  // ---- hierarchy ----
  GameObject* parent() const noexcept;
  const std::vector<GameObjectId>& children() const noexcept { return children_; }
  // Fails when the new parent is this object or one of its descendants, or belongs to another scene.
  // With keepWorldTransform the object stays where it is in the world; otherwise its local values
  // are kept and it moves with the new parent. nullptr makes it a root.
  Result<void> setParent(GameObject* parent, bool keepWorldTransform = true);

  // ---- activation ----
  bool activeSelf() const noexcept { return activeSelf_; }
  bool activeInHierarchy() const noexcept;
  void setActive(bool active);

  // ---- components ----
  // Constructs T (must derive from Component) and attaches it. Awake/Enable run immediately when the
  // object is active in the hierarchy, otherwise when it becomes active.
  template <class T, class... Args>
  T* addComponent(Args&&... args) {
    static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
    auto owned = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = owned.get();
    attach(std::move(owned));
    return raw;
  }
  template <class T>
  T* getComponent() const {
    for (const auto& c : components_) {
      if (!c->destroyed_) {
        if (auto* t = dynamic_cast<T*>(c.get())) return t;
      }
    }
    return nullptr;
  }
  template <class T>
  std::vector<T*> getComponents() const {
    std::vector<T*> out;
    for (const auto& c : components_) {
      if (!c->destroyed_) {
        if (auto* t = dynamic_cast<T*>(c.get())) out.push_back(t);
      }
    }
    return out;
  }
  std::size_t componentCount() const noexcept;
  // Visits live components in attach order.
  void forEachComponent(const std::function<void(Component&)>& fn) const;
  // Schedules the component for destruction at the next flush; it stops receiving callbacks now.
  void removeComponent(Component* component);

  bool markedForDestroy() const noexcept { return destroying_; }

  static std::int64_t liveCount() noexcept { return liveCount_.load(); }

 private:
  friend class Scene;
  friend class Component;
  friend class Transform;
  GameObject(Scene& scene, GameObjectId id, std::string name);
  void attach(std::unique_ptr<Component> c);

  Scene* scene_;
  GameObjectId id_;
  std::string name_;
  Transform transform_;
  GameObjectId parent_;
  std::vector<GameObjectId> children_;
  std::vector<std::unique_ptr<Component>> components_;
  bool activeSelf_ = true;
  bool destroying_ = false;
  bool wasActive_ = false;  // effective state at the last activation sweep
  static inline std::atomic<std::int64_t> liveCount_{0};
};

struct SceneEnvironment {
  Vec3 ambient{0.03F, 0.03F, 0.03F};
  float clearColor[4] = {0.05F, 0.05F, 0.07F, 1.0F};
};

class Scene {
 public:
  Scene() = default;
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  ~Scene();  // destroys every object, running onDisable/onDestroy

  // Creates an object, optionally under `parent` (which must belong to this scene). Parenting a new
  // object is always keepWorld-neutral because the new transform is the identity.
  GameObject* createObject(std::string name = "GameObject", GameObject* parent = nullptr);
  // Schedules the object and all of its descendants for destruction at the next flush.
  void destroy(GameObject* object);
  // Destroys all pending objects/components now. Called automatically at the end of every frame
  // phase, and by destroy()/removeComponent() when called outside of a callback.
  void flushDestroyed();
  // Destroys everything immediately (unload).
  void clear();

  GameObject* get(GameObjectId id) const noexcept;
  GameObject* find(const std::string& name) const;  // first match in creation order, depth-agnostic
  std::vector<GameObject*> rootObjects() const;
  std::size_t objectCount() const noexcept { return list_.size(); }  // includes objects pending destruction
  template <class T>
  std::vector<T*> findComponents() const {
    std::vector<T*> out;
    for (GameObject* o : list_) {
      if (o->destroying_) continue;
      for (T* c : o->getComponents<T>()) out.push_back(c);
    }
    return out;
  }
  void forEachObject(const std::function<void(GameObject&)>& fn) const;

  // Frame phases. Objects/components created during a phase first receive callbacks in the next
  // one; destruction requested during a phase takes effect at the end of that phase.
  void fixedUpdate(float dt);
  void update(float dt);
  void lateUpdate(float dt);

  SceneEnvironment environment;

  // Non-owning registry of engine services (physics, audio, ...) that components look up. The
  // registrant keeps the object alive for as long as the scene may call into it. Passing nullptr
  // unregisters. At most one service per type.
  template <class T>
  void setService(T* service) {
    if (service != nullptr) services_[std::type_index(typeid(T))] = service;
    else services_.erase(std::type_index(typeid(T)));
  }
  template <class T>
  T* service() const {
    const auto it = services_.find(std::type_index(typeid(T)));
    return it == services_.end() ? nullptr : static_cast<T*>(it->second);
  }

  // Number of callback exceptions that were caught and logged (the faulting component is disabled).
  std::size_t callbackFailures() const noexcept { return callbackFailures_; }

 private:
  friend class GameObject;
  friend class Component;
  friend class Transform;

  enum class Phase { Fixed, Update, Late };
  void runPhase(Phase phase, float dt);
  void ensureAwake(GameObject& go);  // awake + enable pass for an object that just became active
  void refreshActivation(GameObject& go);  // recursive enable/disable sweep after an activation change
  void runCallback(Component& c, const char* what, const std::function<void(Component&)>& fn);
  void destroyNow(GameObject& go);
  void collectSubtree(GameObject& root, std::vector<GameObject*>& out) const;
  void markDestroying(GameObject& go);

  std::vector<std::unique_ptr<GameObject>> slots_;
  std::vector<std::uint32_t> generations_;
  std::vector<std::uint32_t> freeSlots_;
  std::vector<GameObject*> list_;  // creation order
  std::unordered_map<std::type_index, void*> services_;
  std::vector<GameObjectId> pendingDestroy_;
  std::vector<GameObjectId> removalOwners_;  // objects with individually removed components
  int callbackDepth_ = 0;
  std::size_t callbackFailures_ = 0;
  bool clearing_ = false;
};

inline Scene& Component::scene() const noexcept { return owner_->scene(); }

}  // namespace mingol::scene
