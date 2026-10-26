// SPDX-License-Identifier: GPL-2.0-only
// (De)serialisation of the engine's built-in components.
#include "json_util.hpp"
#include "mingol/runtime/audio_components.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/scripting/script_component.hpp"
#include "mingol/sceneio/scene_io.hpp"

namespace mingol::sceneio {

namespace {

using json::Value;
namespace j = ju;

const AssetTable* needAssets(const SceneIoContext& ctx, const char* what, std::string& err) {
  if (ctx.assets == nullptr) err = std::string(what) + " references an asset but no AssetTable was provided";
  return ctx.assets;
}

// Reference helpers: null pointer -> JSON null; unregistered pointer -> error.
template <class T>
Result<Value> refOf(const SceneIoContext& ctx, const std::shared_ptr<const T>& p, const char* what) {
  if (p == nullptr) return Value(nullptr);
  std::string err;
  const AssetTable* a = needAssets(ctx, what, err);
  if (a == nullptr) return Error(ErrorCode::InvalidArgument, err);
  const std::string name = a->nameOf(p);
  if (name.empty()) return makeError(ErrorCode::NotFound, "{} is not registered in the AssetTable (give it a name with AssetTable::add*)", what);
  return Value(name);
}

template <class T, class Lookup>
Result<std::shared_ptr<const T>> resolve(const SceneIoContext& ctx, const std::string& name, const char* what, Lookup lookup) {
  if (name.empty()) return std::shared_ptr<const T>();
  std::string err;
  const AssetTable* a = needAssets(ctx, what, err);
  if (a == nullptr) return Error(ErrorCode::InvalidArgument, err);
  auto p = lookup(*a, name);
  if (p == nullptr) return makeError(ErrorCode::NotFound, ".minseen: {} '{}' is not in the AssetTable", what, name);
  return p;
}

// ---- Camera ----
Result<Value> saveCamera(const scene::Component& c, const SceneIoContext&) {
  const auto& cam = static_cast<const scene::CameraComponent&>(c);
  json::Object o;
  o.set("fovY", j::num(cam.fovYRadians));
  o.set("near", j::num(cam.nearZ));
  o.set("far", j::num(cam.farZ));
  o.set("priority", cam.priority);
  return Value(std::move(o));
}
Result<void> loadCamera(scene::GameObject& go, const Value& v, const SceneIoContext&, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "Camera data"));
  MINGOL_TRY_ASSIGN(float fov, j::getFloat(*o, "fovY", 1.0471976F, "Camera"));
  MINGOL_TRY_ASSIGN(float nz, j::getFloat(*o, "near", 0.1F, "Camera"));
  MINGOL_TRY_ASSIGN(float fz, j::getFloat(*o, "far", 200.0F, "Camera"));
  MINGOL_TRY_ASSIGN(std::int64_t prio, j::getInt(*o, "priority", 0, "Camera"));
  if (!(fov > 0.0F && fov < 3.1415F)) return j::corrupt("Camera.fovY must be in (0, pi)");
  if (!(nz > 0.0F && fz > nz)) return j::corrupt("Camera needs 0 < near < far");
  if (prio < -1000000 || prio > 1000000) return j::corrupt("Camera.priority is out of range");
  auto* c = go.addComponent<scene::CameraComponent>();
  c->fovYRadians = fov; c->nearZ = nz; c->farZ = fz; c->priority = static_cast<int>(prio);
  return {};
}

// ---- Light ----
const char* lightName(renderer::LightType t) {
  switch (t) { case renderer::LightType::Directional: return "directional"; case renderer::LightType::Point: return "point"; default: return "spot"; }
}
Result<Value> saveLight(const scene::Component& c, const SceneIoContext&) {
  const auto& l = static_cast<const scene::LightComponent&>(c);
  json::Object o;
  o.set("type", lightName(l.type));
  o.set("color", j::vec3(l.color));
  o.set("intensity", j::num(l.intensity));
  o.set("range", j::num(l.range));
  o.set("innerCone", j::num(l.innerConeRadians));
  o.set("outerCone", j::num(l.outerConeRadians));
  o.set("castShadow", l.castShadow);
  return Value(std::move(o));
}
Result<void> loadLight(scene::GameObject& go, const Value& v, const SceneIoContext&, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "Light data"));
  MINGOL_TRY_ASSIGN(std::string type, j::getString(*o, "type", "directional", "Light"));
  renderer::LightType lt;
  if (type == "directional") lt = renderer::LightType::Directional;
  else if (type == "point") lt = renderer::LightType::Point;
  else if (type == "spot") lt = renderer::LightType::Spot;
  else return j::corrupt("Light.type must be directional, point or spot");
  MINGOL_TRY_ASSIGN(Vec3 color, j::getVec3(*o, "color", Vec3(1, 1, 1), false, "Light"));
  MINGOL_TRY_ASSIGN(float intensity, j::getFloat(*o, "intensity", 1.0F, "Light"));
  MINGOL_TRY_ASSIGN(float range, j::getFloat(*o, "range", 10.0F, "Light"));
  MINGOL_TRY_ASSIGN(float inner, j::getFloat(*o, "innerCone", 0.4F, "Light"));
  MINGOL_TRY_ASSIGN(float outer, j::getFloat(*o, "outerCone", 0.6F, "Light"));
  MINGOL_TRY_ASSIGN(bool shadow, j::getBool(*o, "castShadow", false, "Light"));
  if (intensity < 0 || range <= 0) return j::corrupt("Light.intensity must be >= 0 and range > 0");
  if (inner < 0 || outer < inner || outer > 3.1415F) return j::corrupt("Light cone angles must satisfy 0 <= inner <= outer <= pi");
  auto* l = go.addComponent<scene::LightComponent>();
  l->type = lt; l->color = color; l->intensity = intensity; l->range = range; l->innerConeRadians = inner; l->outerConeRadians = outer; l->castShadow = shadow;
  return {};
}

// ---- MeshRenderer ----
Result<Value> saveMeshRenderer(const scene::Component& c, const SceneIoContext& ctx) {
  const auto& m = static_cast<const scene::MeshRenderer&>(c);
  json::Object o;
  MINGOL_TRY_ASSIGN(Value mesh, refOf(ctx, m.mesh, "a MeshRenderer mesh"));
  MINGOL_TRY_ASSIGN(Value mat, refOf(ctx, m.material, "a MeshRenderer material"));
  o.set("mesh", std::move(mesh));
  o.set("material", std::move(mat));
  o.set("castShadow", m.castShadow);
  return Value(std::move(o));
}
Result<void> loadMeshRenderer(scene::GameObject& go, const Value& v, const SceneIoContext& ctx, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "MeshRenderer data"));
  MINGOL_TRY_ASSIGN(std::string meshName, j::getOptRef(*o, "mesh", "MeshRenderer"));
  MINGOL_TRY_ASSIGN(std::string matName, j::getOptRef(*o, "material", "MeshRenderer"));
  MINGOL_TRY_ASSIGN(bool shadow, j::getBool(*o, "castShadow", true, "MeshRenderer"));
  MINGOL_TRY_ASSIGN(auto mesh, resolve<renderer::Mesh>(ctx, meshName, "mesh", [](const AssetTable& a, const std::string& n) { return a.mesh(n); }));
  MINGOL_TRY_ASSIGN(auto mat, resolve<renderer::Material>(ctx, matName, "material", [](const AssetTable& a, const std::string& n) { return a.material(n); }));
  auto* r = go.addComponent<scene::MeshRenderer>();
  r->mesh = std::move(mesh); r->material = std::move(mat); r->castShadow = shadow;
  return {};
}

// ---- Animator ----
Result<Value> saveAnimator(const scene::Component& c, const SceneIoContext& ctx) {
  const auto& a = static_cast<const scene::Animator&>(c);
  json::Object o;
  MINGOL_TRY_ASSIGN(Value sk, refOf(ctx, a.skeleton(), "an Animator skeleton"));
  o.set("skeleton", std::move(sk));
  json::Array clips;
  for (const auto& [name, clip] : a.clips()) {
    MINGOL_TRY_ASSIGN(Value r, refOf(ctx, clip, "an Animator clip"));
    clips.push_back(std::move(r));
  }
  o.set("clips", Value(std::move(clips)));
  o.set("speed", j::num(a.speed));
  if (a.playing()) {
    json::Object p;
    p.set("clip", a.currentClip());
    p.set("loop", a.looping());
    p.set("time", j::num(a.currentTime()));
    o.set("playing", Value(std::move(p)));
  }
  return Value(std::move(o));
}
Result<void> loadAnimator(scene::GameObject& go, const Value& v, const SceneIoContext& ctx, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "Animator data"));
  MINGOL_TRY_ASSIGN(std::string skName, j::getOptRef(*o, "skeleton", "Animator"));
  MINGOL_TRY_ASSIGN(float speed, j::getFloat(*o, "speed", 1.0F, "Animator"));
  MINGOL_TRY_ASSIGN(auto sk, resolve<renderer::Skeleton>(ctx, skName, "skeleton", [](const AssetTable& a, const std::string& n) { return a.skeleton(n); }));
  std::vector<std::shared_ptr<const renderer::AnimationClip>> clips;
  if (const Value* cv = o->find("clips")) {
    if (!cv->isArray()) return j::corrupt("Animator.clips must be an array");
    for (const auto& e : cv->asArray()) {
      if (!e.isString()) return j::corrupt("Animator.clips entries must be strings");
      MINGOL_TRY_ASSIGN(auto clip, resolve<renderer::AnimationClip>(ctx, e.asString(), "animation clip", [](const AssetTable& a, const std::string& n) { return a.animationClip(n); }));
      clips.push_back(std::move(clip));
    }
  }
  std::string playClip; bool loop = true; float time = 0;
  if (const Value* pv = o->find("playing")) {
    MINGOL_TRY_ASSIGN(const json::Object* p, j::object(*pv, "Animator.playing"));
    MINGOL_TRY_ASSIGN(playClip, j::getString(*p, "clip", "", "Animator.playing"));
    MINGOL_TRY_ASSIGN(loop, j::getBool(*p, "loop", true, "Animator.playing"));
    MINGOL_TRY_ASSIGN(time, j::getFloat(*p, "time", 0.0F, "Animator.playing"));
    if (time < 0) return j::corrupt("Animator.playing.time must be >= 0");
  }
  auto* a = go.addComponent<scene::Animator>();
  auto fail = [&](const Error& e) -> Result<void> { go.removeComponent(a); return Error(e.code, ".minseen: Animator: " + e.message); };
  a->speed = 1.0F;   // `time` is clip time; restore the speed after seeking
  if (sk != nullptr) { auto r = a->setSkeleton(sk); if (!r.ok()) return fail(r.error()); }
  for (const auto& clip : clips) { if (clip == nullptr) continue; auto r = a->addClip(clip); if (!r.ok()) return fail(r.error()); }
  if (!playClip.empty()) {
    auto r = a->play(playClip, loop);
    if (!r.ok()) return fail(r.error());
    a->advance(time);
  }
  a->speed = speed;
  return {};
}

// ---- Rigidbody / Collider ----
const char* bodyTypeName(physics::BodyType t) {
  switch (t) { case physics::BodyType::Static: return "static"; case physics::BodyType::Dynamic: return "dynamic"; default: return "kinematic"; }
}
Result<Value> saveRigidbody(const scene::Component& c, const SceneIoContext&) {
  const auto& r = static_cast<const runtime::Rigidbody&>(c);
  json::Object o;
  o.set("type", bodyTypeName(r.type));
  o.set("mass", j::num(r.mass));
  o.set("friction", j::num(r.friction));
  o.set("restitution", j::num(r.restitution));
  o.set("linearDamping", j::num(r.linearDamping));
  o.set("angularDamping", j::num(r.angularDamping));
  o.set("isTrigger", r.isTrigger);
  o.set("allowSleep", r.allowSleep);
  o.set("layer", static_cast<std::int64_t>(r.layer));
  o.set("mask", static_cast<std::int64_t>(r.mask));
  o.set("lockRotation", json::Value(json::Array{r.lockRotationX, r.lockRotationY, r.lockRotationZ}));
  o.set("lockPosition", json::Value(json::Array{r.lockPositionX, r.lockPositionY, r.lockPositionZ}));
  return Value(std::move(o));
}
Result<std::array<bool, 3>> getBool3(const json::Object& o, const char* key, const std::string& where) {
  std::array<bool, 3> out{false, false, false};
  const Value* v = o.find(key);
  if (v == nullptr) return out;
  if (!v->isArray() || v->asArray().size() != 3) return j::corrupt(where + "." + key + " must be an array of 3 booleans");
  for (std::size_t i = 0; i < 3; ++i) {
    if (!v->asArray()[i].isBool()) return j::corrupt(where + "." + key + " must be an array of 3 booleans");
    out[i] = v->asArray()[i].asBool();
  }
  return out;
}
Result<void> loadRigidbody(scene::GameObject& go, const Value& v, const SceneIoContext&, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "Rigidbody data"));
  MINGOL_TRY_ASSIGN(std::string type, j::getString(*o, "type", "dynamic", "Rigidbody"));
  physics::BodyType bt;
  if (type == "static") bt = physics::BodyType::Static;
  else if (type == "dynamic") bt = physics::BodyType::Dynamic;
  else if (type == "kinematic") bt = physics::BodyType::Kinematic;
  else return j::corrupt("Rigidbody.type must be static, dynamic or kinematic");
  MINGOL_TRY_ASSIGN(float mass, j::getFloat(*o, "mass", 1.0F, "Rigidbody"));
  MINGOL_TRY_ASSIGN(float fr, j::getFloat(*o, "friction", 0.5F, "Rigidbody"));
  MINGOL_TRY_ASSIGN(float rest, j::getFloat(*o, "restitution", 0.0F, "Rigidbody"));
  MINGOL_TRY_ASSIGN(float ld, j::getFloat(*o, "linearDamping", 0.0F, "Rigidbody"));
  MINGOL_TRY_ASSIGN(float ad, j::getFloat(*o, "angularDamping", 0.05F, "Rigidbody"));
  MINGOL_TRY_ASSIGN(bool trig, j::getBool(*o, "isTrigger", false, "Rigidbody"));
  MINGOL_TRY_ASSIGN(bool sleep, j::getBool(*o, "allowSleep", true, "Rigidbody"));
  MINGOL_TRY_ASSIGN(std::int64_t layer, j::getInt(*o, "layer", 1, "Rigidbody"));
  MINGOL_TRY_ASSIGN(std::int64_t mask, j::getInt(*o, "mask", 0xFFFFFFFFLL, "Rigidbody"));
  MINGOL_TRY_ASSIGN(auto lr, getBool3(*o, "lockRotation", "Rigidbody"));
  MINGOL_TRY_ASSIGN(auto lp, getBool3(*o, "lockPosition", "Rigidbody"));
  if (bt == physics::BodyType::Dynamic && !(mass > 0)) return j::corrupt("a dynamic Rigidbody needs mass > 0");
  if (fr < 0 || rest < 0 || ld < 0 || ld > 1 || ad < 0 || ad > 1) return j::corrupt("Rigidbody friction/restitution/damping out of range");
  if (layer < 0 || layer > 0xFFFFFFFFLL || mask < 0 || mask > 0xFFFFFFFFLL) return j::corrupt("Rigidbody layer/mask must fit in 32 bits");
  auto* r = go.addComponent<runtime::Rigidbody>();
  r->type = bt; r->mass = mass; r->friction = fr; r->restitution = rest; r->linearDamping = ld; r->angularDamping = ad;
  r->isTrigger = trig; r->allowSleep = sleep; r->layer = static_cast<std::uint32_t>(layer); r->mask = static_cast<std::uint32_t>(mask);
  r->lockRotationX = lr[0]; r->lockRotationY = lr[1]; r->lockRotationZ = lr[2];
  r->lockPositionX = lp[0]; r->lockPositionY = lp[1]; r->lockPositionZ = lp[2];
  return {};
}

Result<Value> saveCollider(const scene::Component& c, const SceneIoContext& ctx) {
  const auto& col = static_cast<const runtime::Collider&>(c);
  const physics::ShapeDesc& s = col.shape;
  json::Object o;
  switch (s.type) {
    case physics::ShapeType::Box: o.set("shape", "box"); o.set("halfExtents", j::vec3(s.halfExtents)); break;
    case physics::ShapeType::Sphere: o.set("shape", "sphere"); o.set("radius", j::num(s.radius)); break;
    case physics::ShapeType::Capsule: o.set("shape", "capsule"); o.set("radius", j::num(s.radius)); o.set("height", j::num(s.height)); break;
    case physics::ShapeType::TriangleMesh: {
      o.set("shape", "mesh");
      MINGOL_TRY_ASSIGN(Value ref, refOf(ctx, s.mesh, "a Collider mesh"));
      o.set("mesh", std::move(ref));
      break;
    }
  }
  o.set("offset", j::vec3(s.offset));
  o.set("rotation", j::quat(s.rotation));
  return Value(std::move(o));
}
Result<void> loadCollider(scene::GameObject& go, const Value& v, const SceneIoContext& ctx, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "Collider data"));
  MINGOL_TRY_ASSIGN(std::string shape, j::getString(*o, "shape", "", "Collider"));
  physics::ShapeDesc s;
  if (shape == "box") {
    s.type = physics::ShapeType::Box;
    MINGOL_TRY_ASSIGN(s.halfExtents, j::getVec3(*o, "halfExtents", Vec3(0.5F, 0.5F, 0.5F), false, "Collider"));
    if (!(s.halfExtents.x > 0 && s.halfExtents.y > 0 && s.halfExtents.z > 0)) return j::corrupt("Collider.halfExtents must be > 0");
  } else if (shape == "sphere") {
    s.type = physics::ShapeType::Sphere;
    MINGOL_TRY_ASSIGN(s.radius, j::getFloat(*o, "radius", 0.5F, "Collider"));
    if (!(s.radius > 0)) return j::corrupt("Collider.radius must be > 0");
  } else if (shape == "capsule") {
    s.type = physics::ShapeType::Capsule;
    MINGOL_TRY_ASSIGN(s.radius, j::getFloat(*o, "radius", 0.5F, "Collider"));
    MINGOL_TRY_ASSIGN(s.height, j::getFloat(*o, "height", 1.0F, "Collider"));
    if (!(s.radius > 0) || s.height < 0) return j::corrupt("Collider capsule needs radius > 0 and height >= 0");
  } else if (shape == "mesh") {
    s.type = physics::ShapeType::TriangleMesh;
    MINGOL_TRY_ASSIGN(std::string name, j::getOptRef(*o, "mesh", "Collider"));
    if (name.empty()) return j::corrupt("a mesh Collider needs 'mesh'");
    MINGOL_TRY_ASSIGN(s.mesh, resolve<physics::MeshShapeData>(ctx, name, "collision mesh", [](const AssetTable& a, const std::string& n) { return a.collisionMesh(n); }));
  } else {
    return j::corrupt("Collider.shape must be box, sphere, capsule or mesh");
  }
  MINGOL_TRY_ASSIGN(s.offset, j::getVec3(*o, "offset", Vec3(0, 0, 0), false, "Collider"));
  MINGOL_TRY_ASSIGN(s.rotation, j::getQuat(*o, "rotation", Quat{}, false, "Collider"));
  s.rotation = normalize(s.rotation);
  go.addComponent<runtime::Collider>()->shape = std::move(s);
  return {};
}

// ---- Audio ----
Result<Value> saveAudioSource(const scene::Component& c, const SceneIoContext& ctx) {
  const auto& a = static_cast<const runtime::AudioSource&>(c);
  json::Object o;
  MINGOL_TRY_ASSIGN(Value clip, refOf(ctx, a.clip, "an AudioSource clip"));
  o.set("clip", std::move(clip));
  o.set("volume", j::num(a.volume));
  o.set("pitch", j::num(a.pitch));
  o.set("pan", j::num(a.pan));
  o.set("loop", a.loop);
  o.set("playOnEnable", a.playOnEnable);
  o.set("priority", a.priority);
  o.set("spatial", a.spatial);
  o.set("minDistance", j::num(a.minDistance));
  o.set("maxDistance", j::num(a.maxDistance));
  o.set("rolloff", j::num(a.rolloff));
  return Value(std::move(o));
}
Result<void> loadAudioSource(scene::GameObject& go, const Value& v, const SceneIoContext& ctx, LoadReport&) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "AudioSource data"));
  MINGOL_TRY_ASSIGN(std::string clipName, j::getOptRef(*o, "clip", "AudioSource"));
  MINGOL_TRY_ASSIGN(auto clip, resolve<assets::AudioClip>(ctx, clipName, "audio clip", [](const AssetTable& a, const std::string& n) { return a.audioClip(n); }));
  MINGOL_TRY_ASSIGN(float vol, j::getFloat(*o, "volume", 1.0F, "AudioSource"));
  MINGOL_TRY_ASSIGN(float pitch, j::getFloat(*o, "pitch", 1.0F, "AudioSource"));
  MINGOL_TRY_ASSIGN(float pan, j::getFloat(*o, "pan", 0.0F, "AudioSource"));
  MINGOL_TRY_ASSIGN(bool loop, j::getBool(*o, "loop", false, "AudioSource"));
  MINGOL_TRY_ASSIGN(bool poe, j::getBool(*o, "playOnEnable", true, "AudioSource"));
  MINGOL_TRY_ASSIGN(std::int64_t prio, j::getInt(*o, "priority", 0, "AudioSource"));
  MINGOL_TRY_ASSIGN(bool spatial, j::getBool(*o, "spatial", false, "AudioSource"));
  MINGOL_TRY_ASSIGN(float minD, j::getFloat(*o, "minDistance", 1.0F, "AudioSource"));
  MINGOL_TRY_ASSIGN(float maxD, j::getFloat(*o, "maxDistance", 50.0F, "AudioSource"));
  MINGOL_TRY_ASSIGN(float roll, j::getFloat(*o, "rolloff", 1.0F, "AudioSource"));
  if (vol < 0 || !(pitch > 0) || pan < -1 || pan > 1 || !(minD > 0) || maxD < minD || roll < 0) return j::corrupt("AudioSource has out-of-range settings");
  if (prio < -1000000 || prio > 1000000) return j::corrupt("AudioSource.priority is out of range");
  auto* a = go.addComponent<runtime::AudioSource>();
  a->clip = std::move(clip); a->volume = vol; a->pitch = pitch; a->pan = pan; a->loop = loop; a->playOnEnable = poe;
  a->priority = static_cast<int>(prio); a->spatial = spatial; a->minDistance = minD; a->maxDistance = maxD; a->rolloff = roll;
  return {};
}
Result<Value> saveAudioListener(const scene::Component&, const SceneIoContext&) { return Value(json::Object{}); }
Result<void> loadAudioListener(scene::GameObject& go, const Value& v, const SceneIoContext&, LoadReport&) {
  if (!v.isObject()) return j::corrupt("AudioListener data must be an object");
  go.addComponent<runtime::AudioListener>();
  return {};
}

// ---- Script ----
Result<Value> saveScript(const scene::Component& c, const SceneIoContext&) {
  const auto& s = static_cast<const scripting::ScriptComponent&>(c);
  if (!s.hasInstance()) return makeError(ErrorCode::InvalidArgument, "script '{}' has no instance (it was disabled by a failed reload); cannot save it", s.className());
  json::Object o;
  o.set("class", s.className());
  MINGOL_TRY_ASSIGN(Value fields, s.serializeFields());
  o.set("fields", std::move(fields));
  return Value(std::move(o));
}
Result<void> loadScript(scene::GameObject& go, const Value& v, const SceneIoContext& ctx, LoadReport& report) {
  MINGOL_TRY_ASSIGN(const json::Object* o, j::object(v, "Script data"));
  MINGOL_TRY_ASSIGN(std::string cls, j::getString(*o, "class", "", "Script"));
  if (cls.empty()) return j::corrupt("Script needs 'class'");
  if (ctx.scripts == nullptr) return makeError(ErrorCode::InvalidArgument, ".minseen: the scene has Script components but no ScriptHost was provided");
  const Value* fields = o->find("fields");
  if (fields != nullptr && !fields->isObject()) return j::corrupt("Script.fields must be an object");
  auto r = ctx.scripts->addScript(go, cls);
  if (!r.ok()) return Error(r.error().code, ".minseen: " + r.error().message);
  scripting::ScriptComponent* sc = r.value();
  if (fields != nullptr) {
    const auto d = sc->deserializeFields(*fields, report.warnings);
    if (!d.ok()) { go.removeComponent(sc); return d.error(); }
  }
  return {};
}

}  // namespace

ComponentRegistry ComponentRegistry::withBuiltins() {
  ComponentRegistry r;
  auto add = [&](const char* name, SaveFn s, LoadFn l) { (void)r.registerType(name, std::move(s), std::move(l)); };
  add("Camera", saveCamera, loadCamera);
  add("Light", saveLight, loadLight);
  add("MeshRenderer", saveMeshRenderer, loadMeshRenderer);
  add("Animator", saveAnimator, loadAnimator);
  add("Rigidbody", saveRigidbody, loadRigidbody);
  add("Collider", saveCollider, loadCollider);
  add("AudioSource", saveAudioSource, loadAudioSource);
  add("AudioListener", saveAudioListener, loadAudioListener);
  add("Script", saveScript, loadScript);
  return r;
}

}  // namespace mingol::sceneio
