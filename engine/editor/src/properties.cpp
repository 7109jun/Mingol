// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/properties.hpp"

#include <cmath>

#include "mingol/editor/euler.hpp"
#include "mingol/runtime/audio_components.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/scripting/script_component.hpp"

namespace mingol::editor {

namespace {

Error badType(const std::string& name) { return Error{ErrorCode::InvalidArgument, "wrong value type for property '" + name + "'"}; }

template <class T>
const T* get(const PropValue& v) {
  return std::get_if<T>(&v);
}

Result<void> checkRange(const std::string& name, double v, double lo, double hi) {
  if (!std::isfinite(v)) return Error{ErrorCode::InvalidArgument, "property '" + name + "' must be finite"};
  if (lo < hi && (v < lo || v > hi))
    return Error{ErrorCode::OutOfRange, "property '" + name + "' out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]"};
  return {};
}

// ---- builders ------------------------------------------------------------------------------
Property boolProp(std::string name, bool* ref) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Bool;
  const std::string n = p.name;
  p.get = [ref] { return PropValue(*ref); };
  p.set = [ref, n](const PropValue& v) -> Result<void> {
    const bool* b = get<bool>(v);
    if (b == nullptr) return badType(n);
    *ref = *b;
    return {};
  };
  return p;
}

Property floatProp(std::string name, float* ref, double lo = 0, double hi = 0, double speed = 0.05) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Float;
  p.min = lo;
  p.max = hi;
  p.speed = speed;
  const std::string n = p.name;
  p.get = [ref] { return PropValue(static_cast<double>(*ref)); };
  p.set = [ref, n, lo, hi](const PropValue& v) -> Result<void> {
    const double* d = get<double>(v);
    if (d == nullptr) return badType(n);
    MINGOL_TRY(checkRange(n, *d, lo, hi));
    *ref = static_cast<float>(*d);
    return {};
  };
  return p;
}

// float edited in degrees but stored in radians
Property angleProp(std::string name, float* radiansRef, double lo, double hi) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Float;
  p.min = lo;
  p.max = hi;
  p.speed = 0.5;
  const std::string n = p.name;
  p.get = [radiansRef] { return PropValue(static_cast<double>(degrees(*radiansRef))); };
  p.set = [radiansRef, n, lo, hi](const PropValue& v) -> Result<void> {
    const double* d = get<double>(v);
    if (d == nullptr) return badType(n);
    MINGOL_TRY(checkRange(n, *d, lo, hi));
    *radiansRef = radians(static_cast<float>(*d));
    return {};
  };
  return p;
}

template <class T>
Property intProp(std::string name, T* ref, std::int64_t lo = 0, std::int64_t hi = 0) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Int;
  p.min = static_cast<double>(lo);
  p.max = static_cast<double>(hi);
  p.speed = 0.2;
  const std::string n = p.name;
  p.get = [ref] { return PropValue(static_cast<std::int64_t>(*ref)); };
  p.set = [ref, n, lo, hi](const PropValue& v) -> Result<void> {
    const auto* i = get<std::int64_t>(v);
    if (i == nullptr) return badType(n);
    MINGOL_TRY(checkRange(n, static_cast<double>(*i), static_cast<double>(lo), static_cast<double>(hi)));
    *ref = static_cast<T>(*i);
    return {};
  };
  return p;
}

template <class E>
Property enumProp(std::string name, E* ref, std::vector<std::string> names) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Enum;
  p.enumNames = std::move(names);
  const std::string n = p.name;
  const auto count = static_cast<std::int64_t>(p.enumNames.size());
  p.get = [ref] { return PropValue(static_cast<std::int64_t>(*ref)); };
  p.set = [ref, n, count](const PropValue& v) -> Result<void> {
    const auto* i = get<std::int64_t>(v);
    if (i == nullptr) return badType(n);
    if (*i < 0 || *i >= count) return Error{ErrorCode::OutOfRange, "enum index out of range for '" + n + "'"};
    *ref = static_cast<E>(*i);
    return {};
  };
  return p;
}

Property vec3Prop(std::string name, Vec3* ref, bool color = false, double speed = 0.05) {
  Property p;
  p.name = std::move(name);
  p.kind = color ? PropKind::Color3 : PropKind::Vec3;
  p.speed = speed;
  const std::string n = p.name;
  p.get = [ref] { return PropValue(*ref); };
  p.set = [ref, n](const PropValue& v) -> Result<void> {
    const Vec3* x = get<Vec3>(v);
    if (x == nullptr) return badType(n);
    if (!std::isfinite(x->x) || !std::isfinite(x->y) || !std::isfinite(x->z)) return Error{ErrorCode::InvalidArgument, "property '" + n + "' must be finite"};
    *ref = *x;
    return {};
  };
  return p;
}

Property assetProp(std::string name, std::vector<std::string> choices, std::function<std::string()> getName,
                   std::function<Result<void>(const std::string&)> setName) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Asset;
  p.assetChoices = std::move(choices);
  const std::string n = p.name;
  auto choicesCopy = p.assetChoices;
  p.get = [getName] { return PropValue(getName()); };
  p.set = [setName, n, choicesCopy](const PropValue& v) -> Result<void> {
    const std::string* s = get<std::string>(v);
    if (s == nullptr) return badType(n);
    if (!s->empty() && std::find(choicesCopy.begin(), choicesCopy.end(), *s) == choicesCopy.end())
      return Error{ErrorCode::NotFound, "unknown asset '" + *s + "' for property '" + n + "'"};
    return setName(*s);
  };
  return p;
}

Property label(std::string name, std::string text) {
  Property p;
  p.name = std::move(name);
  p.kind = PropKind::Label;
  p.get = [text] { return PropValue(text); };
  return p;
}

// ---- components ------------------------------------------------------------------------------
void describeCamera(scene::CameraComponent& c, std::vector<Property>& out) {
  out.push_back(angleProp("Field of View", &c.fovYRadians, 1.0, 179.0));
  out.push_back(floatProp("Near", &c.nearZ, 0.001, 1000.0, 0.01));
  out.push_back(floatProp("Far", &c.farZ, 0.01, 100000.0, 1.0));
  out.push_back(intProp("Priority", &c.priority, -1000, 1000));
}

void describeLight(scene::LightComponent& c, std::vector<Property>& out) {
  out.push_back(enumProp("Type", &c.type, {"Directional", "Point", "Spot"}));
  out.push_back(vec3Prop("Color", &c.color, true));
  out.push_back(floatProp("Intensity", &c.intensity, 0.0, 1.0e6, 0.05));
  out.push_back(floatProp("Range", &c.range, 0.01, 1.0e5, 0.1));
  out.push_back(angleProp("Inner Cone", &c.innerConeRadians, 0.0, 89.0));
  out.push_back(angleProp("Outer Cone", &c.outerConeRadians, 0.1, 89.9));
  out.push_back(boolProp("Cast Shadow", &c.castShadow));
}

void describeMeshRenderer(scene::MeshRenderer& c, const PropertyEnv& env, std::vector<Property>& out) {
  const sceneio::AssetTable* a = env.assets;
  out.push_back(assetProp(
      "Mesh", a ? a->meshNames() : std::vector<std::string>{},
      [&c, a] { return a ? a->nameOf(c.mesh) : std::string(); },
      [&c, a](const std::string& n) -> Result<void> {
        c.mesh = n.empty() || a == nullptr ? nullptr : a->mesh(n);
        return {};
      }));
  out.push_back(assetProp(
      "Material", a ? a->materialNames() : std::vector<std::string>{},
      [&c, a] { return a ? a->nameOf(c.material) : std::string(); },
      [&c, a](const std::string& n) -> Result<void> {
        c.material = n.empty() || a == nullptr ? nullptr : a->material(n);
        return {};
      }));
  out.push_back(boolProp("Cast Shadow", &c.castShadow));
}

void describeAnimator(scene::Animator& c, std::vector<Property>& out) {
  out.push_back(floatProp("Speed", &c.speed, -100.0, 100.0, 0.02));
  std::string clips;
  for (const auto& kv : c.clips()) clips += (clips.empty() ? "" : ", ") + kv.first;
  out.push_back(label("Clips", clips.empty() ? "(none)" : clips));
  out.push_back(label("Playing", c.playing() ? c.currentClip() : "(stopped)"));
}

void describeRigidbody(runtime::Rigidbody& c, std::vector<Property>& out) {
  out.push_back(enumProp("Body Type", &c.type, {"Static", "Dynamic", "Kinematic"}));
  out.push_back(floatProp("Mass", &c.mass, 0.0001, 1.0e7, 0.05));
  out.push_back(floatProp("Friction", &c.friction, 0.0, 100.0, 0.01));
  out.push_back(floatProp("Restitution", &c.restitution, 0.0, 10.0, 0.01));
  out.push_back(floatProp("Linear Damping", &c.linearDamping, 0.0, 1.0, 0.005));
  out.push_back(floatProp("Angular Damping", &c.angularDamping, 0.0, 1.0, 0.005));
  out.push_back(boolProp("Is Trigger", &c.isTrigger));
  out.push_back(boolProp("Allow Sleep", &c.allowSleep));
  out.push_back(intProp("Layer", &c.layer, 0, 0xFFFFFFFFLL));
  out.push_back(intProp("Mask", &c.mask, 0, 0xFFFFFFFFLL));
  out.push_back(boolProp("Lock Rotation X", &c.lockRotationX));
  out.push_back(boolProp("Lock Rotation Y", &c.lockRotationY));
  out.push_back(boolProp("Lock Rotation Z", &c.lockRotationZ));
  out.push_back(boolProp("Lock Position X", &c.lockPositionX));
  out.push_back(boolProp("Lock Position Y", &c.lockPositionY));
  out.push_back(boolProp("Lock Position Z", &c.lockPositionZ));
}

void describeCollider(runtime::Collider& c, std::vector<Property>& out) {
  out.push_back(enumProp("Shape", &c.shape.type, {"Box", "Sphere", "Capsule", "TriangleMesh"}));
  out.push_back(vec3Prop("Half Extents", &c.shape.halfExtents));
  out.push_back(floatProp("Radius", &c.shape.radius, 0.0001, 1.0e5, 0.01));
  out.push_back(floatProp("Height", &c.shape.height, 0.0, 1.0e5, 0.01));
  out.push_back(vec3Prop("Offset", &c.shape.offset));
}

void describeAudioSource(runtime::AudioSource& c, const PropertyEnv& env, std::vector<Property>& out) {
  const sceneio::AssetTable* a = env.assets;
  out.push_back(assetProp(
      "Clip", a ? a->audioClipNames() : std::vector<std::string>{},
      [&c, a] { return a ? a->nameOf(c.clip) : std::string(); },
      [&c, a](const std::string& n) -> Result<void> {
        c.clip = n.empty() || a == nullptr ? nullptr : a->audioClip(n);
        return {};
      }));
  out.push_back(floatProp("Volume", &c.volume, 0.0, 10.0, 0.01));
  out.push_back(floatProp("Pitch", &c.pitch, 0.01, 10.0, 0.01));
  out.push_back(floatProp("Pan", &c.pan, -1.0, 1.0, 0.01));
  out.push_back(boolProp("Loop", &c.loop));
  out.push_back(boolProp("Play On Enable", &c.playOnEnable));
  out.push_back(intProp("Priority", &c.priority, -128, 128));
  out.push_back(boolProp("Spatial", &c.spatial));
  out.push_back(floatProp("Min Distance", &c.minDistance, 0.0, 1.0e5, 0.1));
  out.push_back(floatProp("Max Distance", &c.maxDistance, 0.0, 1.0e6, 0.5));
  out.push_back(floatProp("Rolloff", &c.rolloff, 0.0, 100.0, 0.01));
}

void describeScript(scripting::ScriptComponent& c, std::vector<Property>& out) {
  out.push_back(label("Class", c.className()));
  auto fields = c.serializeFields();
  if (!fields.ok() || !fields.value().isObject()) {
    out.push_back(label("Fields", "(unavailable)"));
    return;
  }
  const json::Object& o = fields.value().asObject();
  for (std::size_t i = 0; i < o.size(); ++i) {
    const std::string name = o.keyAt(i);
    const json::Value& v = o.valueAt(i);
    Property p;
    p.name = name;
    scripting::ScriptComponent* sc = &c;
    if (v.isBool()) {
      p.kind = PropKind::Bool;
      p.get = [sc, name] { auto r = sc->getBool(name); return PropValue(r.ok() ? r.value() : false); };
      p.set = [sc, name](const PropValue& x) -> Result<void> {
        const bool* b = get<bool>(x);
        if (b == nullptr) return badType(name);
        return sc->setBool(name, *b);
      };
    } else if (v.isInt()) {
      p.kind = PropKind::Int;
      p.speed = 0.2;
      p.get = [sc, name] { auto r = sc->getInt(name); return PropValue(r.ok() ? r.value() : std::int64_t{0}); };
      p.set = [sc, name](const PropValue& x) -> Result<void> {
        const auto* iv = get<std::int64_t>(x);
        if (iv == nullptr) return badType(name);
        return sc->setInt(name, *iv);
      };
    } else if (v.isFloat()) {
      p.kind = PropKind::Float;
      p.get = [sc, name] { auto r = sc->getFloat(name); return PropValue(r.ok() ? r.value() : 0.0); };
      p.set = [sc, name](const PropValue& x) -> Result<void> {
        const double* d = get<double>(x);
        if (d == nullptr) return badType(name);
        MINGOL_TRY(checkRange(name, *d, 0, 0));
        return sc->setFloat(name, *d);
      };
    } else if (v.isString()) {
      p.kind = PropKind::String;
      p.get = [sc, name] { auto r = sc->getString(name); return PropValue(r.ok() ? r.value() : std::string()); };
      p.set = [sc, name](const PropValue& x) -> Result<void> {
        const std::string* s = get<std::string>(x);
        if (s == nullptr) return badType(name);
        return sc->setString(name, *s);
      };
    } else {
      auto text = json::write(v, {false, 16});
      p = label(name, text.ok() ? text.value() : std::string("(unprintable)"));
    }
    out.push_back(std::move(p));
  }
}

}  // namespace

std::vector<Property> describeTransform(scene::GameObject& go) {
  std::vector<Property> out;
  scene::GameObject* g = &go;
  {
    Property p;
    p.name = "Position";
    p.kind = PropKind::Vec3;
    p.get = [g] { return PropValue(g->transform().localPosition()); };
    p.set = [g](const PropValue& v) -> Result<void> {
      const Vec3* x = get<Vec3>(v);
      if (x == nullptr) return badType("Position");
      if (!std::isfinite(x->x) || !std::isfinite(x->y) || !std::isfinite(x->z)) return Error{ErrorCode::InvalidArgument, "position must be finite"};
      g->transform().setLocalPosition(*x);
      return {};
    };
    out.push_back(std::move(p));
  }
  {
    Property p;
    p.name = "Rotation";
    p.kind = PropKind::Vec3;
    p.speed = 0.5;
    p.get = [g] { return PropValue(toEulerDegrees(g->transform().localRotation())); };
    p.set = [g](const PropValue& v) -> Result<void> {
      const Vec3* x = get<Vec3>(v);
      if (x == nullptr) return badType("Rotation");
      if (!std::isfinite(x->x) || !std::isfinite(x->y) || !std::isfinite(x->z)) return Error{ErrorCode::InvalidArgument, "rotation must be finite"};
      g->transform().setLocalRotation(fromEulerDegrees(*x));
      return {};
    };
    out.push_back(std::move(p));
  }
  {
    Property p;
    p.name = "Scale";
    p.kind = PropKind::Vec3;
    p.get = [g] { return PropValue(g->transform().localScale()); };
    p.set = [g](const PropValue& v) -> Result<void> {
      const Vec3* x = get<Vec3>(v);
      if (x == nullptr) return badType("Scale");
      if (!std::isfinite(x->x) || !std::isfinite(x->y) || !std::isfinite(x->z)) return Error{ErrorCode::InvalidArgument, "scale must be finite"};
      g->transform().setLocalScale(*x);
      return {};
    };
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<Property> describeComponent(scene::Component& c, const PropertyEnv& env) {
  std::vector<Property> out;
  if (auto* x = dynamic_cast<scene::CameraComponent*>(&c)) describeCamera(*x, out);
  else if (auto* l = dynamic_cast<scene::LightComponent*>(&c)) describeLight(*l, out);
  else if (auto* m = dynamic_cast<scene::MeshRenderer*>(&c)) describeMeshRenderer(*m, env, out);
  else if (auto* a = dynamic_cast<scene::Animator*>(&c)) describeAnimator(*a, out);
  else if (auto* r = dynamic_cast<runtime::Rigidbody*>(&c)) describeRigidbody(*r, out);
  else if (auto* k = dynamic_cast<runtime::Collider*>(&c)) describeCollider(*k, out);
  else if (auto* s = dynamic_cast<runtime::AudioSource*>(&c)) describeAudioSource(*s, env, out);
  else if (dynamic_cast<runtime::AudioListener*>(&c) != nullptr) { /* no properties */ }
  else if (auto* sc = dynamic_cast<scripting::ScriptComponent*>(&c)) describeScript(*sc, out);
  else out.push_back(label("Type", c.typeName()));
  return out;
}

const std::vector<std::string>& addableComponentTypes() {
  static const std::vector<std::string> kTypes = {"Camera", "Light", "MeshRenderer", "Animator", "Rigidbody", "Collider", "AudioSource", "AudioListener"};
  return kTypes;
}

std::string toDisplayString(const PropValue& v) {
  struct V {
    std::string operator()(bool b) const { return b ? "true" : "false"; }
    std::string operator()(std::int64_t i) const { return std::to_string(i); }
    std::string operator()(double d) const { return std::to_string(d); }
    std::string operator()(const Vec3& x) const { return "(" + std::to_string(x.x) + ", " + std::to_string(x.y) + ", " + std::to_string(x.z) + ")"; }
    std::string operator()(const Vec4& x) const { return "(" + std::to_string(x.x) + ", " + std::to_string(x.y) + ", " + std::to_string(x.z) + ", " + std::to_string(x.w) + ")"; }
    std::string operator()(const std::string& s) const { return s; }
  };
  return std::visit(V{}, v);
}

}  // namespace mingol::editor
