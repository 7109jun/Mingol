// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/scripting/script_component.hpp"

#include <cmath>
#include <stdexcept>

#include "mingol/core/log.hpp"

namespace mingol::scripting {

using script::ClassSym;
using script::FieldSym;
using script::MethodSym;
using script::Value;

namespace {
const char* const kCallbackNames[] = {"onAwake", "onEnable", "onStart", "onFixedUpdate", "onUpdate", "onLateUpdate", "onDisable", "onDestroy"};
const std::size_t kCallbackParams[] = {0, 0, 0, 1, 1, 1, 0, 0};

const MethodSym* findVirtual(const ClassSym* cls, const char* name, std::size_t params) {
  for (const ClassSym* c = cls; c != nullptr; c = c->base) {
    for (const auto& m : c->methods) {
      if (m->mkind == script::MethodKind::Method && !m->isStatic() && m->name == name && m->params.size() == params) return m.get();
    }
  }
  return nullptr;
}
}  // namespace

ScriptComponent::ScriptComponent(ScriptHost& host, std::string className) : host_(&host), className_(std::move(className)) {
  host_->registerComponent(this);
  const auto r = createInstance();
  if (!r.ok()) MINGOL_LOG_ERROR("script", "cannot create script '{}': {}", className_, r.error().message);
}

ScriptComponent::~ScriptComponent() {
  releaseInstance();
  host_->unregisterComponent(this);
}

void ScriptComponent::releaseInstance() {
  instance_.reset();
  cls_ = nullptr;
  handleSet_ = false;
  callbacks_.fill(nullptr);
}

script::Value ScriptComponent::instance() const { return instance_ != nullptr ? instance_->get() : script::makeNull(); }

Result<void> ScriptComponent::createInstance() {
  releaseInstance();
  const ClassSym* cls = host_->program().findClass(className_);
  if (cls == nullptr) return makeError(ErrorCode::NotFound, "script class '{}' does not exist", className_);
  const MethodSym* ctor = nullptr;
  for (const auto& m : cls->methods) {
    if (m->mkind == script::MethodKind::Constructor && m->params.empty() && m->mods.access == script::Access::Public) ctor = m.get();
  }
  if (ctor == nullptr) return makeError(ErrorCode::InvalidArgument, "script class '{}' has no public parameterless constructor", className_);
  script::ScriptException ex;
  Value obj;
  {
    ScriptHost::Scope scope(*host_, nullptr);
    if (!host_->vm().construct(*cls, *ctor, {}, obj, ex)) {
      return makeError(ErrorCode::Internal, "constructor of '{}' threw {}", className_, ex.describe());
    }
  }
  instance_ = std::make_unique<script::Rooted>(host_->vm(), obj);
  cls_ = cls;
  bindCallbacks();
  return {};
}

void ScriptComponent::bindCallbacks() {
  for (int i = 0; i < CbCount; ++i) {
    const MethodSym* m = findVirtual(cls_, kCallbackNames[i], kCallbackParams[i]);
    // The empty defaults declared by `Script` are skipped entirely.
    callbacks_[static_cast<std::size_t>(i)] = (m != nullptr && m->owner->name != "Script") ? m : nullptr;
  }
}

void ScriptComponent::invoke(Cb cb, const float* dt) {
  const MethodSym* m = callbacks_[static_cast<std::size_t>(cb)];
  if (m == nullptr || instance_ == nullptr) return;
  ScriptHost::Scope scope(*host_, &scene());
  Value arg = script::makeFloat(dt != nullptr ? static_cast<double>(*dt) : 0.0);
  Value result;
  script::ScriptException ex;
  const bool ok = host_->vm().call(*m, instance_->get(), dt != nullptr ? std::span<const Value>(&arg, 1) : std::span<const Value>{}, result, ex);
  if (!ok) throw std::runtime_error(className_ + "." + kCallbackNames[cb] + " threw " + ex.describe());
}

void ScriptComponent::bindGameObjectHandle() {
  // Hand the script its GameObject handle (field `gameObject` of Script).
  ScriptHost::Scope scope(*host_, &scene());
  const FieldSym* f = findField("gameObject");
  if (f == nullptr || instance_ == nullptr) return;
  const auto* api = script::hostApi();
  MsValue h = api->newObject(&host_->vm(), "GameObject");
  if (h.tag != MS_TAG_OBJECT) return;
  script::Rooted keep(host_->vm(), h);
  api->setField(&host_->vm(), keep.get(), "index", script::makeInt(gameObject().id().index));
  api->setField(&host_->vm(), keep.get(), "generation", script::makeInt(gameObject().id().generation));
  host_->vm().setField(instance_->get(), *f, keep.get());
  handleSet_ = true;
}

void ScriptComponent::onAwake() {
  if (instance_ == nullptr) return;
  bindGameObjectHandle();
  invoke(Awake, nullptr);
}
void ScriptComponent::onEnable() { invoke(Enable, nullptr); }
void ScriptComponent::onStart() { invoke(Start, nullptr); }
void ScriptComponent::onFixedUpdate(float dt) { invoke(Fixed, &dt); }
void ScriptComponent::onUpdate(float dt) { invoke(Update, &dt); }
void ScriptComponent::onLateUpdate(float dt) { invoke(Late, &dt); }
void ScriptComponent::onDisable() { invoke(Disable, nullptr); }
void ScriptComponent::onDestroy() { invoke(Destroy, nullptr); }

const FieldSym* ScriptComponent::findField(const std::string& name) const {
  return cls_ != nullptr ? cls_->findField(name) : nullptr;
}

#define MINGOL_FIELD_LOOKUP(name)                                                                  \
  if (instance_ == nullptr) return makeError(ErrorCode::NotFound, "the script component has no instance"); \
  const FieldSym* f = findField(name);                                                              \
  if (f == nullptr || f->isStatic) return makeError(ErrorCode::NotFound, "no instance field '{}' in '{}'", name, className_)

Result<void> ScriptComponent::setInt(const std::string& n, std::int64_t v) {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::Int) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not int", n, f->type->name());
  host_->vm().setField(instance_->get(), *f, script::makeInt(v));
  return {};
}
Result<void> ScriptComponent::setFloat(const std::string& n, double v) {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::Float) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not float", n, f->type->name());
  host_->vm().setField(instance_->get(), *f, script::makeFloat(v));
  return {};
}
Result<void> ScriptComponent::setBool(const std::string& n, bool v) {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::Bool) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not bool", n, f->type->name());
  host_->vm().setField(instance_->get(), *f, script::makeBool(v));
  return {};
}
Result<void> ScriptComponent::setString(const std::string& n, const std::string& v) {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::String) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not string", n, f->type->name());
  host_->vm().setField(instance_->get(), *f, host_->vm().newString(v));
  return {};
}
Result<std::int64_t> ScriptComponent::getInt(const std::string& n) const {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::Int) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not int", n, f->type->name());
  return static_cast<std::int64_t>(host_->vm().getField(instance_->get(), *f).u.i);
}
Result<double> ScriptComponent::getFloat(const std::string& n) const {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::Float) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not float", n, f->type->name());
  return host_->vm().getField(instance_->get(), *f).u.f;
}
Result<bool> ScriptComponent::getBool(const std::string& n) const {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::Bool) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not bool", n, f->type->name());
  return host_->vm().getField(instance_->get(), *f).u.i != 0;
}
Result<std::string> ScriptComponent::getString(const std::string& n) const {
  MINGOL_FIELD_LOOKUP(n);
  if (f->type->kind != script::TypeKind::String) return makeError(ErrorCode::InvalidArgument, "field '{}' is {}, not string", n, f->type->name());
  const Value v = host_->vm().getField(instance_->get(), *f);
  return v.tag == MS_TAG_OBJECT ? std::string(host_->vm().stringValue(v)) : std::string();
}

namespace {
bool dataType(const script::Type* t) {
  return t->kind == script::TypeKind::Int || t->kind == script::TypeKind::Float || t->kind == script::TypeKind::Bool ||
         t->kind == script::TypeKind::String || t->kind == script::TypeKind::Enum;
}
int elemKind(const script::Type* t) {
  switch (t->kind) {
    case script::TypeKind::Int: case script::TypeKind::Enum: return MS_ELEM_INT;
    case script::TypeKind::Float: return MS_ELEM_FLOAT;
    case script::TypeKind::Bool: return MS_ELEM_BOOL;
    default: return MS_ELEM_STRING;
  }
}
}  // namespace

Result<json::Value> ScriptComponent::serializeFields() const {
  if (instance_ == nullptr) return makeError(ErrorCode::NotFound, "the script component has no instance");
  script::VM& vm = host_->vm();
  const MsHostApi* api = script::hostApi();
  auto scalar = [&](const script::Type* t, const Value& v) -> Result<json::Value> {
    switch (t->kind) {
      case script::TypeKind::Int: case script::TypeKind::Enum: return json::Value(static_cast<std::int64_t>(v.u.i));
      case script::TypeKind::Bool: return json::Value(v.u.i != 0);
      case script::TypeKind::Float:
        if (!std::isfinite(v.u.f)) return makeError(ErrorCode::InvalidArgument, "a float field holds NaN or Infinity, which JSON cannot represent");
        return json::Value(v.u.f);
      default: return v.tag == MS_TAG_OBJECT ? json::Value(std::string(vm.stringValue(v))) : json::Value(nullptr);
    }
  };
  json::Object out;
  std::vector<const ClassSym*> chain;
  for (const ClassSym* k = cls_; k != nullptr; k = k->base) chain.push_back(k);
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {   // base class fields first
    for (const auto& f : (*it)->fields) {
      if (f->isStatic || f->name == "gameObject") continue;
      const script::Type* t = f->type;
      const Value v = vm.getField(instance_->get(), *f);
      if (dataType(t)) {
        auto j = scalar(t, v);
        if (!j.ok()) return makeError(ErrorCode::InvalidArgument, "field '{}': {}", f->name, j.error().message);
        out.set(f->name, std::move(j).value());
      } else if (t->kind == script::TypeKind::Array && dataType(t->elem)) {
        if (v.tag != MS_TAG_OBJECT) { out.set(f->name, json::Value(nullptr)); continue; }
        json::Array arr;
        const std::int64_t n = api->arrayLength(&vm, v);
        for (std::int64_t i = 0; i < n; ++i) {
          auto j = scalar(t->elem, api->arrayGet(&vm, v, i));
          if (!j.ok()) return makeError(ErrorCode::InvalidArgument, "field '{}': {}", f->name, j.error().message);
          arr.push_back(std::move(j).value());
        }
        out.set(f->name, json::Value(std::move(arr)));
      }
    }
  }
  return json::Value(std::move(out));
}

Result<void> ScriptComponent::deserializeFields(const json::Value& fields, std::vector<std::string>& warnings) {
  if (instance_ == nullptr) return makeError(ErrorCode::NotFound, "the script component has no instance");
  if (!fields.isObject()) return makeError(ErrorCode::Corrupt, "script fields must be a JSON object");
  script::VM& vm = host_->vm();
  const MsHostApi* api = script::hostApi();
  auto matches = [](const script::Type* t, const json::Value& j) {
    switch (t->kind) {
      case script::TypeKind::Int: case script::TypeKind::Enum: return j.isInt();
      case script::TypeKind::Float: return j.isNumber();
      case script::TypeKind::Bool: return j.isBool();
      default: return j.isString();
    }
  };
  auto toValue = [&](const script::Type* t, const json::Value& j) -> Value {
    switch (t->kind) {
      case script::TypeKind::Int: case script::TypeKind::Enum: return script::makeInt(j.asInt());
      case script::TypeKind::Float: return script::makeFloat(j.toDouble());
      case script::TypeKind::Bool: return script::makeBool(j.asBool());
      default: return vm.newString(j.asString());
    }
  };
  const json::Object& o = fields.asObject();
  for (std::size_t i = 0; i < o.size(); ++i) {
    const std::string& name = o.keyAt(i);
    const json::Value& j = o.valueAt(i);
    const FieldSym* f = findField(name);
    if (f == nullptr || f->isStatic || name == "gameObject") { warnings.push_back("script '" + className_ + "': unknown field '" + name + "' ignored"); continue; }
    const script::Type* t = f->type;
    if (dataType(t)) {
      if (!matches(t, j)) { warnings.push_back("script '" + className_ + "': field '" + name + "' has the wrong JSON type for " + t->name() + "; ignored"); continue; }
      vm.setField(instance_->get(), *f, toValue(t, j));
    } else if (t->kind == script::TypeKind::Array && dataType(t->elem)) {
      if (j.isNull()) { vm.setField(instance_->get(), *f, script::makeNull()); continue; }
      bool ok = j.isArray();
      if (ok) for (const auto& e : j.asArray()) ok = ok && matches(t->elem, e);
      if (!ok) { warnings.push_back("script '" + className_ + "': field '" + name + "' is not a valid " + t->name() + " array; ignored"); continue; }
      const auto& src = j.asArray();
      MsValue arr = api->newArray(&vm, elemKind(t->elem), static_cast<std::int64_t>(src.size()));
      script::Rooted keep(vm, arr);
      for (std::size_t k = 0; k < src.size(); ++k) api->arraySet(&vm, keep.get(), static_cast<std::int64_t>(k), toValue(t->elem, src[k]));
      vm.setField(instance_->get(), *f, keep.get());
    } else {
      warnings.push_back("script '" + className_ + "': field '" + name + "' (" + t->name() + ") cannot be restored; ignored");
    }
  }
  return {};
}

Result<void> ScriptComponent::call(const std::string& method) {
  if (instance_ == nullptr) return makeError(ErrorCode::NotFound, "the script component has no instance");
  const MethodSym* m = findVirtual(cls_, method.c_str(), 0);
  if (m == nullptr) return makeError(ErrorCode::NotFound, "no parameterless method '{}' in '{}'", method, className_);
  ScriptHost::Scope scope(*host_, &scene());
  Value result;
  script::ScriptException ex;
  if (!host_->vm().call(*m, instance_->get(), {}, result, ex)) return makeError(ErrorCode::Internal, "{} threw {}", method, ex.describe());
  return {};
}

}  // namespace mingol::scripting
