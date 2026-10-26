// SPDX-License-Identifier: GPL-2.0-only
// The engine API visible to scripts: its source (compiled with every program) and the C++ natives behind it.
#include "engine_api.hpp"

#include <cmath>
#include <string>

#include "mingol/core/log.hpp"
#include "mingol/scene/scene.hpp"
#include "mingol/scripting/script_host.hpp"

namespace mingol::scripting {

const char* engineApiSource() noexcept {
  return R"MINS(
public native class Log {
  public static native void info(string message);
  public static native void warn(string message);
  public static native void error(string message);
}

public class Vec3 {
  public float x;
  public float y;
  public float z;
  public Vec3() { }
  public Vec3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
  public static Vec3 operator +(Vec3 a, Vec3 b) { return new Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
  public static Vec3 operator -(Vec3 a, Vec3 b) { return new Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
  public static Vec3 operator *(Vec3 a, float s) { return new Vec3(a.x * s, a.y * s, a.z * s); }
}

// Handle to a scene object. It stays valid as a value after the object is destroyed (exists() turns false and
// other calls throw NullReferenceError).
public native class GameObject {
  public int index;
  public int generation;
  public native bool exists();
  public native string getName();
  public native void setName(string name);
  public native bool isActive();
  public native void setActive(bool active);
  public native Vec3 getPosition();
  public native void setPosition(Vec3 position);
  public native Vec3 getWorldPosition();
  public native Vec3 getScale();
  public native void setScale(Vec3 scale);
  public native void rotate(Vec3 axis, float degrees);
  public native void setRotation(Vec3 axis, float degrees);
  public native GameObject getParent();
  public native bool same(GameObject other);
  public native void destroy();
  public static native GameObject find(string name);
  public static native GameObject create(string name);
}

// Base class of every script component. Override the callbacks you need.
public class Script {
  public GameObject gameObject;
  public void onAwake() { }
  public void onEnable() { }
  public void onStart() { }
  public void onFixedUpdate(float dt) { }
  public void onUpdate(float dt) { }
  public void onLateUpdate(float dt) { }
  public void onDisable() { }
  public void onDestroy() { }
}
)MINS";
}

namespace {

using script::hostApi;

const MsHostApi* api() { return hostApi(); }
MsValue vInt(std::int64_t i) { MsValue v{}; v.tag = MS_TAG_INT; v.u.i = i; return v; }
MsValue vFloat(double f) { MsValue v{}; v.tag = MS_TAG_FLOAT; v.u.f = f; return v; }
MsValue vBool(bool b) { MsValue v{}; v.tag = MS_TAG_BOOL; v.u.i = b ? 1 : 0; return v; }
MsValue vNull() { return MsValue{}; }

std::string str(MsVM* vm, MsValue s) {
  if (s.tag != MS_TAG_OBJECT) return {};
  std::size_t len = 0;
  const char* d = api()->stringData(vm, s, &len);
  return std::string(d != nullptr ? d : "", len);
}

void logInfo(MsVM* vm, MsValue* a, int32_t, MsValue*) { MINGOL_LOG_INFO("script", "{}", str(vm, a[0])); }
void logWarn(MsVM* vm, MsValue* a, int32_t, MsValue*) { MINGOL_LOG_WARN("script", "{}", str(vm, a[0])); }
void logError(MsVM* vm, MsValue* a, int32_t, MsValue*) { MINGOL_LOG_ERROR("script", "{}", str(vm, a[0])); }

scene::Scene* sceneOrThrow(MsVM* vm) {
  ScriptHost* h = ScriptHost::current();
  scene::Scene* s = h != nullptr ? h->currentScene() : nullptr;
  if (s == nullptr) api()->throwError(vm, "mingol.ArgumentError", "no scene is active for this call");
  return s;
}

MsValue makeHandle(MsVM* vm, const scene::GameObject* go) {
  if (go == nullptr) return vNull();
  MsValue o = api()->newObject(vm, "GameObject");
  if (o.tag != MS_TAG_OBJECT) return vNull();
  api()->setField(vm, o, "index", vInt(go->id().index));
  api()->setField(vm, o, "generation", vInt(go->id().generation));
  return o;
}

scene::GameObjectId idOf(MsVM* vm, MsValue h) {
  scene::GameObjectId id;
  id.index = static_cast<std::uint32_t>(api()->getField(vm, h, "index").u.i);
  id.generation = static_cast<std::uint32_t>(api()->getField(vm, h, "generation").u.i);
  return id;
}

// Resolves `this`; on failure raises a script error and returns null.
scene::GameObject* self(MsVM* vm, MsValue* a) {
  scene::Scene* s = sceneOrThrow(vm);
  if (s == nullptr) return nullptr;
  scene::GameObject* go = s->get(idOf(vm, a[0]));
  if (go == nullptr || go->markedForDestroy()) {
    api()->throwError(vm, "mingol.NullReferenceError", "the GameObject was destroyed");
    return nullptr;
  }
  return go;
}

MsValue makeVec3(MsVM* vm, Vec3 v) {
  MsValue o = api()->newObject(vm, "Vec3");
  if (o.tag != MS_TAG_OBJECT) return vNull();
  api()->setField(vm, o, "x", vFloat(v.x));
  api()->setField(vm, o, "y", vFloat(v.y));
  api()->setField(vm, o, "z", vFloat(v.z));
  return o;
}

bool readVec3(MsVM* vm, MsValue o, Vec3& out) {
  if (o.tag != MS_TAG_OBJECT) { api()->throwError(vm, "mingol.NullReferenceError", "Vec3 argument is null"); return false; }
  out = Vec3(static_cast<float>(api()->getField(vm, o, "x").u.f), static_cast<float>(api()->getField(vm, o, "y").u.f),
             static_cast<float>(api()->getField(vm, o, "z").u.f));
  return true;
}

constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;

void goExists(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  scene::Scene* s = sceneOrThrow(vm);
  if (s == nullptr) return;
  const scene::GameObject* go = s->get(idOf(vm, a[0]));
  *r = vBool(go != nullptr && !go->markedForDestroy());
}
void goGetName(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (auto* go = self(vm, a)) *r = api()->newString(vm, go->name().c_str(), go->name().size());
}
void goSetName(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  if (auto* go = self(vm, a)) go->setName(str(vm, a[1]));
}
void goIsActive(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (auto* go = self(vm, a)) *r = vBool(go->activeSelf());
}
void goSetActive(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  if (auto* go = self(vm, a)) go->setActive(a[1].u.i != 0);
}
void goGetPosition(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (auto* go = self(vm, a)) *r = makeVec3(vm, go->transform().localPosition());
}
void goSetPosition(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  auto* go = self(vm, a);
  Vec3 p;
  if (go != nullptr && readVec3(vm, a[1], p)) go->transform().setLocalPosition(p);
}
void goGetWorldPosition(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (auto* go = self(vm, a)) *r = makeVec3(vm, go->transform().worldPosition());
}
void goGetScale(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (auto* go = self(vm, a)) *r = makeVec3(vm, go->transform().localScale());
}
void goSetScale(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  auto* go = self(vm, a);
  Vec3 p;
  if (go != nullptr && readVec3(vm, a[1], p)) go->transform().setLocalScale(p);
}
bool axisAngle(MsVM* vm, MsValue* a, Quat& q) {
  Vec3 axis;
  if (!readVec3(vm, a[1], axis)) return false;
  const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
  if (!(len > 1e-8F)) { api()->throwError(vm, "mingol.ArgumentError", "rotation axis must not be zero"); return false; }
  q = Quat::fromAxisAngle(Vec3(axis.x / len, axis.y / len, axis.z / len), static_cast<float>(a[2].u.f) * kDegToRad);
  return true;
}
void goRotate(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  auto* go = self(vm, a);
  Quat q;
  if (go != nullptr && axisAngle(vm, a, q)) go->transform().setLocalRotation(go->transform().localRotation() * q);
}
void goSetRotation(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  auto* go = self(vm, a);
  Quat q;
  if (go != nullptr && axisAngle(vm, a, q)) go->transform().setLocalRotation(q);
}
void goGetParent(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (auto* go = self(vm, a)) *r = makeHandle(vm, go->parent());
}
void goSame(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (a[1].tag != MS_TAG_OBJECT) { *r = vBool(false); return; }
  *r = vBool(idOf(vm, a[0]) == idOf(vm, a[1]));
}
void goDestroy(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  if (auto* go = self(vm, a)) go->scene().destroy(go);
}
void goFind(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  scene::Scene* s = sceneOrThrow(vm);
  if (s == nullptr) return;
  scene::GameObject* go = s->find(str(vm, a[0]));
  *r = (go != nullptr && !go->markedForDestroy()) ? makeHandle(vm, go) : vNull();
}
void goCreate(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  scene::Scene* s = sceneOrThrow(vm);
  if (s == nullptr) return;
  *r = makeHandle(vm, s->createObject(str(vm, a[0])));
}

struct Binding { const char* name; MsNativeFn fn; };
const Binding kBindings[] = {
    {"Log.info(string)", logInfo},
    {"Log.warn(string)", logWarn},
    {"Log.error(string)", logError},
    {"GameObject.exists()", goExists},
    {"GameObject.getName()", goGetName},
    {"GameObject.setName(string)", goSetName},
    {"GameObject.isActive()", goIsActive},
    {"GameObject.setActive(bool)", goSetActive},
    {"GameObject.getPosition()", goGetPosition},
    {"GameObject.setPosition(Vec3)", goSetPosition},
    {"GameObject.getWorldPosition()", goGetWorldPosition},
    {"GameObject.getScale()", goGetScale},
    {"GameObject.setScale(Vec3)", goSetScale},
    {"GameObject.rotate(Vec3, float)", goRotate},
    {"GameObject.setRotation(Vec3, float)", goSetRotation},
    {"GameObject.getParent()", goGetParent},
    {"GameObject.same(GameObject)", goSame},
    {"GameObject.destroy()", goDestroy},
    {"GameObject.find(string)", goFind},
    {"GameObject.create(string)", goCreate},
};

}  // namespace

Result<void> bindEngineNatives(script::VM& vm) {
  for (const Binding& b : kBindings) MINGOL_TRY(vm.bindNative(b.name, b.fn));
  return {};
}

}  // namespace mingol::scripting
