// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/scene/scene.hpp"

#include <algorithm>
#include <exception>
#include <unordered_set>

#include "mingol/core/log.hpp"

namespace mingol::scene {

// ---- Component ------------------------------------------------------------------------------

bool Component::activeAndEnabled() const noexcept { return enabled_ && owner_->activeInHierarchy(); }

void Component::setEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (destroyed_ || owner_->destroying_ || !owner_->wasActive_ || !awoken_) return;
  Scene& s = *owner_->scene_;
  if (enabled && !enableCalled_) {
    enableCalled_ = true;
    s.runCallback(*this, "onEnable", [](Component& c) { c.onEnable(); });
  } else if (!enabled && enableCalled_) {
    enableCalled_ = false;
    s.runCallback(*this, "onDisable", [](Component& c) { c.onDisable(); });
  }
}

// ---- GameObject -----------------------------------------------------------------------------

GameObject::GameObject(Scene& scene, GameObjectId id, std::string name)
    : scene_(&scene), id_(id), name_(std::move(name)), transform_(*this) {
  ++liveCount_;
}

GameObject* GameObject::parent() const noexcept { return scene_->get(parent_); }

bool GameObject::activeInHierarchy() const noexcept {
  for (const GameObject* o = this; o != nullptr; o = o->parent()) {
    if (!o->activeSelf_) return false;
  }
  return true;
}

void GameObject::setActive(bool active) {
  if (activeSelf_ == active) return;
  activeSelf_ = active;
  if (!destroying_) scene_->refreshActivation(*this);
}

Result<void> GameObject::setParent(GameObject* newParent, bool keepWorldTransform) {
  if (destroying_) return makeError(ErrorCode::InvalidArgument, "object '{}' is being destroyed", name_);
  if (newParent != nullptr) {
    if (newParent->scene_ != scene_) {
      return makeError(ErrorCode::InvalidArgument, "cannot parent '{}' under an object of another scene", name_);
    }
    if (newParent->destroying_) {
      return makeError(ErrorCode::InvalidArgument, "new parent '{}' is being destroyed", newParent->name_);
    }
    for (const GameObject* o = newParent; o != nullptr; o = o->parent()) {
      if (o == this) {
        return makeError(ErrorCode::InvalidArgument, "parenting '{}' under '{}' would create a cycle", name_,
                         newParent->name_);
      }
    }
  }
  GameObject* oldParent = parent();
  if (oldParent == newParent) return {};
  const Mat4 world = transform_.worldMatrix();  // before the change

  if (oldParent != nullptr) {
    auto& v = oldParent->children_;
    v.erase(std::remove(v.begin(), v.end(), id_), v.end());
  }
  parent_ = newParent != nullptr ? newParent->id_ : GameObjectId{};
  if (newParent != nullptr) newParent->children_.push_back(id_);

  if (keepWorldTransform) {
    // The cached world matrix is outdated now (new parent): mark the subtree stale, then derive
    // the local transform that reproduces the previous world matrix under the new parent.
    transform_.stale_ = false;  // markStale() returns early on an already-stale node
    transform_.markStale();
    transform_.setLocalFromWorld(world);
  } else {
    transform_.markStale();
  }
  scene_->refreshActivation(*this);
  return {};
}

void GameObject::attach(std::unique_ptr<Component> c) {
  Component* raw = c.get();
  raw->owner_ = this;
  components_.push_back(std::move(c));
  if (wasActive_ && !destroying_) {
    raw->awoken_ = true;
    scene_->runCallback(*raw, "onAwake", [](Component& x) { x.onAwake(); });
    if (raw->enabled_ && !raw->destroyed_ && !raw->enableCalled_) {
      raw->enableCalled_ = true;
      scene_->runCallback(*raw, "onEnable", [](Component& x) { x.onEnable(); });
    }
  }
}

std::size_t GameObject::componentCount() const noexcept {
  std::size_t n = 0;
  for (const auto& c : components_) n += c->destroyed_ ? 0 : 1;
  return n;
}

void GameObject::forEachComponent(const std::function<void(Component&)>& fn) const {
  for (std::size_t i = 0; i < components_.size(); ++i) {
    if (!components_[i]->destroyed_) fn(*components_[i]);
  }
}

void GameObject::removeComponent(Component* component) {
  for (auto& c : components_) {
    if (c.get() == component && !c->destroyed_) {
      scene_->removalOwners_.push_back(id_);
      c->destroyed_ = true;
      if (scene_->callbackDepth_ == 0) scene_->flushDestroyed();
      return;
    }
  }
}

// ---- Scene ----------------------------------------------------------------------------------

Scene::~Scene() { clear(); }

GameObject* Scene::createObject(std::string name, GameObject* parent) {
  if (clearing_) {
    MINGOL_LOG_ERROR("scene", "createObject('{}') ignored: the scene is being cleared", name);
    return nullptr;
  }
  if (parent != nullptr && (parent->scene_ != this || parent->destroying_)) {
    MINGOL_LOG_ERROR("scene", "createObject('{}'): invalid parent", name);
    return nullptr;
  }
  std::uint32_t slot = 0;
  if (!freeSlots_.empty()) {
    slot = freeSlots_.back();
    freeSlots_.pop_back();
  } else {
    slot = static_cast<std::uint32_t>(slots_.size());
    slots_.emplace_back();
    generations_.push_back(1);
  }
  const GameObjectId id{slot + 1, generations_[slot]};
  slots_[slot].reset(new GameObject(*this, id, std::move(name)));
  GameObject* go = slots_[slot].get();
  list_.push_back(go);
  if (parent != nullptr) {
    go->parent_ = parent->id_;
    parent->children_.push_back(id);
  }
  refreshActivation(*go);
  return go;
}

GameObject* Scene::get(GameObjectId id) const noexcept {
  if (id.index == 0 || id.index > slots_.size()) return nullptr;
  const std::uint32_t slot = id.index - 1;
  if (generations_[slot] != id.generation) return nullptr;
  return slots_[slot].get();
}

GameObject* Scene::find(const std::string& name) const {
  for (GameObject* o : list_) {
    if (!o->destroying_ && o->name_ == name) return o;
  }
  return nullptr;
}

std::vector<GameObject*> Scene::rootObjects() const {
  std::vector<GameObject*> roots;
  for (GameObject* o : list_) {
    if (!o->destroying_ && !o->parent_.valid()) roots.push_back(o);
  }
  return roots;
}

void Scene::forEachObject(const std::function<void(GameObject&)>& fn) const {
  const std::vector<GameObject*> snapshot = list_;
  for (GameObject* o : snapshot) {
    if (get(o->id_) == o && !o->destroying_) fn(*o);
  }
}

void Scene::runCallback(Component& c, const char* what, const std::function<void(Component&)>& fn) {
  ++callbackDepth_;
  try {
    fn(c);
  } catch (const std::exception& e) {
    ++callbackFailures_;
    MINGOL_LOG_ERROR("scene", "{} of component '{}' on object '{}' threw: {}; the component is disabled", what,
                     c.typeName(), c.owner_->name_, e.what());
    c.enabled_ = false;
  } catch (...) {
    ++callbackFailures_;
    MINGOL_LOG_ERROR("scene", "{} of component '{}' on object '{}' threw an unknown exception; the component is disabled",
                     what, c.typeName(), c.owner_->name_);
    c.enabled_ = false;
  }
  --callbackDepth_;
  if (!c.enabled_ && c.enableCalled_ && !c.destroyed_) {
    // Faulted: run onDisable once so the component can release what onEnable acquired.
    c.enableCalled_ = false;
    ++callbackDepth_;
    try {
      c.onDisable();
    } catch (...) {
      MINGOL_LOG_ERROR("scene", "onDisable of component '{}' also threw; ignored", c.typeName());
    }
    --callbackDepth_;
  }
}

void Scene::ensureAwake(GameObject& go) {
  for (std::size_t i = 0; i < go.components_.size(); ++i) {
    Component* c = go.components_[i].get();
    if (c->destroyed_ || go.destroying_) continue;
    if (!c->awoken_) {
      c->awoken_ = true;
      runCallback(*c, "onAwake", [](Component& x) { x.onAwake(); });
    }
    if (c->enabled_ && !c->destroyed_ && !c->enableCalled_) {
      c->enableCalled_ = true;
      runCallback(*c, "onEnable", [](Component& x) { x.onEnable(); });
    }
  }
}

void Scene::refreshActivation(GameObject& root) {
  std::vector<GameObject*> stack{&root};
  while (!stack.empty()) {
    GameObject* o = stack.back();
    stack.pop_back();
    const bool effective = o->activeInHierarchy() && !o->destroying_;
    if (effective && !o->wasActive_) {
      o->wasActive_ = true;
      ensureAwake(*o);
    } else if (!effective && o->wasActive_) {
      o->wasActive_ = false;
      for (std::size_t i = o->components_.size(); i-- > 0;) {
        Component* c = o->components_[i].get();
        if (c->enableCalled_ && !c->destroyed_) {
          c->enableCalled_ = false;
          runCallback(*c, "onDisable", [](Component& x) { x.onDisable(); });
        }
      }
    }
    for (const GameObjectId id : o->children_) {
      if (GameObject* child = get(id)) stack.push_back(child);
    }
  }
}

void Scene::collectSubtree(GameObject& root, std::vector<GameObject*>& out) const {
  // Post-order: every child appears before its parent.
  std::vector<std::pair<GameObject*, bool>> stack{{&root, false}};
  while (!stack.empty()) {
    auto [o, expanded] = stack.back();
    stack.pop_back();
    if (expanded) {
      out.push_back(o);
      continue;
    }
    stack.push_back({o, true});
    for (const GameObjectId id : o->children_) {
      if (GameObject* child = get(id)) stack.push_back({child, false});
    }
  }
}

void Scene::markDestroying(GameObject& root) {
  std::vector<GameObject*> stack{&root};
  while (!stack.empty()) {
    GameObject* o = stack.back();
    stack.pop_back();
    o->destroying_ = true;
    for (const GameObjectId id : o->children_) {
      if (GameObject* child = get(id)) stack.push_back(child);
    }
  }
}

void Scene::destroy(GameObject* object) {
  if (object == nullptr || object->scene_ != this || object->destroying_) return;
  markDestroying(*object);
  pendingDestroy_.push_back(object->id_);
  if (callbackDepth_ == 0) flushDestroyed();
}

void Scene::destroyNow(GameObject& go) {
  ++callbackDepth_;
  for (std::size_t i = go.components_.size(); i-- > 0;) {
    Component* c = go.components_[i].get();
    if (c->enableCalled_ && !c->destroyed_) {
      c->enableCalled_ = false;
      runCallback(*c, "onDisable", [](Component& x) { x.onDisable(); });
    }
    if (c->awoken_ && !c->destroyed_) runCallback(*c, "onDestroy", [](Component& x) { x.onDestroy(); });
    c->destroyed_ = true;
  }
  --callbackDepth_;
  if (GameObject* p = go.parent()) {
    auto& v = p->children_;
    v.erase(std::remove(v.begin(), v.end(), go.id_), v.end());
  }
}

void Scene::flushDestroyed() {
  if (callbackDepth_ > 0) return;  // a callback is running: the enclosing phase flushes when it ends
  ++callbackDepth_;                // prevents re-entrant flushes from onDestroy callbacks
  // Components removed individually.
  while (!removalOwners_.empty()) {
    std::vector<GameObjectId> owners;
    owners.swap(removalOwners_);
    for (const GameObjectId id : owners) {
      GameObject* go = get(id);
      if (go == nullptr) continue;
      for (std::size_t i = go->components_.size(); i-- > 0;) {
        Component* c = go->components_[i].get();
        if (!c->destroyed_) continue;
        if (c->enableCalled_) {
          c->enableCalled_ = false;
          runCallback(*c, "onDisable", [](Component& x) { x.onDisable(); });
        }
        if (c->awoken_) runCallback(*c, "onDestroy", [](Component& x) { x.onDestroy(); });
        c->awoken_ = false;
      }
      std::erase_if(go->components_, [](const std::unique_ptr<Component>& c) { return c->destroyed_; });
    }
  }
  // Whole objects (callbacks may schedule more, so loop until nothing is left).
  std::unordered_set<GameObject*> freed;
  while (!pendingDestroy_.empty()) {
    std::vector<GameObjectId> roots;
    roots.swap(pendingDestroy_);
    for (const GameObjectId id : roots) {
      GameObject* root = get(id);
      if (root == nullptr) continue;
      std::vector<GameObject*> subtree;
      collectSubtree(*root, subtree);
      for (GameObject* go : subtree) {
        go->destroying_ = true;
        destroyNow(*go);
        const std::uint32_t slot = go->id_.index - 1;
        freed.insert(go);
        slots_[slot].reset();  // frees the GameObject (and its components)
        ++generations_[slot];
        freeSlots_.push_back(slot);
      }
    }
  }
  if (!freed.empty()) {
    std::erase_if(list_, [&](GameObject* o) { return freed.count(o) != 0; });
  }
  --callbackDepth_;
}

void Scene::clear() {
  if (clearing_) return;
  clearing_ = true;
  for (GameObject* o : list_) {
    if (!o->destroying_ && !o->parent_.valid()) {
      markDestroying(*o);
      pendingDestroy_.push_back(o->id_);
    }
  }
  const int savedDepth = callbackDepth_;
  callbackDepth_ = 0;
  flushDestroyed();
  callbackDepth_ = savedDepth;
  // Anything left (objects created from onDestroy handlers during the flush are refused while
  // clearing_, so this only happens for objects marked earlier) is released now.
  if (!list_.empty()) {
    for (GameObject* o : list_) {
      if (o->destroying_ && !o->parent_.valid()) pendingDestroy_.push_back(o->id_);
    }
    callbackDepth_ = 0;
    flushDestroyed();
    callbackDepth_ = savedDepth;
  }
  clearing_ = false;
}

void Scene::runPhase(Phase phase, float dt) {
  std::vector<GameObjectId> ids;
  ids.reserve(list_.size());
  for (const GameObject* o : list_) ids.push_back(o->id_);
  ++callbackDepth_;
  for (const GameObjectId id : ids) {
    GameObject* go = get(id);
    if (go == nullptr || go->destroying_ || !go->wasActive_) continue;
    // Components added during this phase are not visited (n is fixed up front); removal is deferred
    // so indices stay valid.
    const std::size_t n = go->components_.size();
    for (std::size_t i = 0; i < n; ++i) {
      Component* c = go->components_[i].get();
      if (c->destroyed_ || !c->enabled_ || !c->enableCalled_ || go->destroying_ || !go->wasActive_) continue;
      if (!c->started_) {
        c->started_ = true;
        runCallback(*c, "onStart", [](Component& x) { x.onStart(); });
        if (c->destroyed_ || !c->enabled_ || !c->enableCalled_) continue;
      }
      switch (phase) {
        case Phase::Fixed: runCallback(*c, "onFixedUpdate", [dt](Component& x) { x.onFixedUpdate(dt); }); break;
        case Phase::Update: runCallback(*c, "onUpdate", [dt](Component& x) { x.onUpdate(dt); }); break;
        case Phase::Late: runCallback(*c, "onLateUpdate", [dt](Component& x) { x.onLateUpdate(dt); }); break;
      }
    }
  }
  --callbackDepth_;
  flushDestroyed();
}

void Scene::fixedUpdate(float dt) { runPhase(Phase::Fixed, dt); }
void Scene::update(float dt) { runPhase(Phase::Update, dt); }
void Scene::lateUpdate(float dt) { runPhase(Phase::Late, dt); }

}  // namespace mingol::scene
