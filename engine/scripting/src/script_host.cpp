// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/scripting/script_host.hpp"

#include <algorithm>

#include "engine_api.hpp"
#include "mingol/core/log.hpp"
#include "mingol/scripting/script_component.hpp"

namespace mingol::scripting {

using script::ClassSym;
using script::FieldSym;
using script::TypeKind;
using script::Value;

namespace {
thread_local ScriptHost* tlsHost = nullptr;

const ClassSym* scriptBase(const script::Program& p) { return p.findClass("Script"); }

// One captured field value (primitives, strings, and arrays of them).
struct Scalar {
  int tag = MS_TAG_NULL;
  std::int64_t i = 0;
  double f = 0;
  std::string s;
};
struct Snap {
  std::string name;
  std::string typeName;
  bool isArray = false;
  bool isNull = false;
  std::vector<Scalar> items;  // exactly one for scalars
};

bool scalarKind(const script::Type* t) {
  return t->kind == TypeKind::Int || t->kind == TypeKind::Float || t->kind == TypeKind::Bool || t->kind == TypeKind::String ||
         t->kind == TypeKind::Enum;
}
int elemKindOf(const script::Type* t) {
  switch (t->kind) {
    case TypeKind::Int: case TypeKind::Enum: return MS_ELEM_INT;
    case TypeKind::Float: return MS_ELEM_FLOAT;
    case TypeKind::Bool: return MS_ELEM_BOOL;
    default: return MS_ELEM_STRING;
  }
}

Scalar readScalar(script::VM& vm, const Value& v) {
  Scalar s;
  s.tag = v.tag;
  if (v.tag == MS_TAG_INT || v.tag == MS_TAG_BOOL) s.i = v.u.i;
  else if (v.tag == MS_TAG_FLOAT) s.f = v.u.f;
  else if (v.tag == MS_TAG_OBJECT) s.s = std::string(vm.stringValue(v));
  return s;
}
Value writeScalar(script::VM& vm, const Scalar& s) {
  switch (s.tag) {
    case MS_TAG_INT: return script::makeInt(s.i);
    case MS_TAG_BOOL: return script::makeBool(s.i != 0);
    case MS_TAG_FLOAT: return script::makeFloat(s.f);
    case MS_TAG_OBJECT: return vm.newString(s.s);
    default: return script::makeNull();
  }
}
}  // namespace

ScriptHost* ScriptHost::current() noexcept { return tlsHost; }



ScriptHost::Scope::Scope(ScriptHost& host, scene::Scene* scene) noexcept : prevHost_(tlsHost), prevScene_(host.scene_), host_(host) {
  tlsHost = &host;
  // A null scene keeps the previous one (a nested call without its own scene context).
  if (scene != nullptr) host.scene_ = scene;
}
ScriptHost::Scope::~Scope() {
  tlsHost = prevHost_;
  host_.scene_ = prevScene_;
}

Result<ScriptHost::Built> ScriptHost::build(const std::vector<script::SourceInput>& sources) const {
  std::vector<script::SourceInput> all;
  all.push_back({"<engine-api>", engineApiSource()});
  all.insert(all.end(), sources.begin(), sources.end());
  script::Diagnostics diags;
  Built b;
  b.program = script::compile(all, diags);
  if (b.program == nullptr) {
    std::string msg = "script compilation failed:\n";
    for (const auto& e : diags.errors()) msg += "  " + e.format() + "\n";
    return Error(ErrorCode::InvalidArgument, std::move(msg));
  }
  if (scriptBase(*b.program) == nullptr) return makeError(ErrorCode::Internal, "engine API did not define class Script");
  b.vm = std::make_unique<script::VM>(*b.program, desc_.vm);
  MINGOL_TRY(bindEngineNatives(*b.vm));
  for (const std::string& p : desc_.plugins) MINGOL_TRY(b.vm->loadPlugin(p));
  const auto unbound = b.vm->unboundNatives();
  for (const std::string& n : unbound) {
    if (n.rfind("Log.", 0) == 0 || n.rfind("GameObject.", 0) == 0) return makeError(ErrorCode::Internal, "engine native '{}' is not bound", n);
  }
  script::ScriptException ex;
  Scope scope(*const_cast<ScriptHost*>(this), nullptr);  // static initialisers may log
  if (!b.vm->initialize(ex)) return makeError(ErrorCode::InvalidArgument, "script static initialisation failed: {}", ex.describe());
  return b;
}

Result<std::unique_ptr<ScriptHost>> ScriptHost::create(const std::vector<script::SourceInput>& sources, HostDesc desc) {
  std::unique_ptr<ScriptHost> h(new ScriptHost());
  h->desc_ = std::move(desc);
  MINGOL_TRY_ASSIGN(Built b, h->build(sources));
  h->program_ = std::move(b.program);
  h->vm_ = std::move(b.vm);
  return h;
}

ScriptHost::~ScriptHost() {
  // Components normally die before the host; if not, make them inert instead of leaving dangling pointers.
  for (ScriptComponent* c : components_) c->releaseInstance();
  components_.clear();
  vm_.reset();
  program_.reset();
}

void ScriptHost::registerComponent(ScriptComponent* c) { components_.push_back(c); }
void ScriptHost::unregisterComponent(ScriptComponent* c) { components_.erase(std::remove(components_.begin(), components_.end(), c), components_.end()); }

bool ScriptHost::isScriptClass(const std::string& name) const {
  const ClassSym* cls = program_->findClass(name);
  const ClassSym* base = scriptBase(*program_);
  if (cls == nullptr || base == nullptr || cls == base || !cls->isSubclassOf(base)) return false;
  if (cls->mods.isAbstract || cls->mods.isNative || cls->kind != script::TypeDeclKind::Class) return false;
  for (const auto& m : cls->methods) {
    if (m->mkind == script::MethodKind::Constructor && m->params.empty() && m->mods.access == script::Access::Public) return true;
  }
  return false;
}

std::vector<std::string> ScriptHost::scriptClasses() const {
  std::vector<std::string> out;
  for (const auto& c : program_->classes) {
    if (isScriptClass(c->qualifiedName)) out.push_back(c->qualifiedName);
  }
  return out;
}

Result<ScriptComponent*> ScriptHost::addScript(scene::GameObject& object, const std::string& className) {
  if (!isScriptClass(className)) {
    return makeError(ErrorCode::InvalidArgument, "'{}' is not a concrete class deriving from Script with a public parameterless constructor", className);
  }
  ScriptComponent* c = object.addComponent<ScriptComponent>(*this, className);
  if (!c->hasInstance()) {
    object.removeComponent(c);
    return makeError(ErrorCode::Internal, "could not create an instance of '{}' (see the log)", className);
  }
  return c;
}

Result<ReloadReport> ScriptHost::reload(const std::vector<script::SourceInput>& sources) {
  MINGOL_TRY_ASSIGN(Built next, build(sources));
  ReloadReport report;

  // 1. Capture state from the old VM.
  struct Captured {
    ScriptComponent* comp;
    bool hadHandle;
    std::vector<Snap> fields;
  };
  std::vector<Captured> captured;
  captured.reserve(components_.size());
  for (ScriptComponent* c : components_) {
    Captured cap{c, c->handleSet_, {}};
    if (c->instance_ != nullptr) {
      for (const ClassSym* k = c->cls_; k != nullptr; k = k->base) {
        for (const auto& f : k->fields) {
          if (f->isStatic) continue;
          const script::Type* t = f->type;
          const bool arr = t->kind == TypeKind::Array && scalarKind(t->elem);
          if (!scalarKind(t) && !arr) {
            if (f->name != "gameObject") ++report.fieldsSkipped;
            continue;
          }
          Snap s;
          s.name = f->name;
          s.typeName = t->name();
          s.isArray = arr;
          const Value v = vm_->getField(c->instance_->get(), *f);
          if (arr) {
            if (v.tag != MS_TAG_OBJECT) {
              s.isNull = true;
            } else {
              const MsHostApi* api = script::hostApi();
              const std::int64_t n = api->arrayLength(vm_.get(), v);
              for (std::int64_t i = 0; i < n; ++i) s.items.push_back(readScalar(*vm_, api->arrayGet(vm_.get(), v, i)));
            }
          } else {
            s.isNull = v.tag == MS_TAG_NULL;
            s.items.push_back(readScalar(*vm_, v));
          }
          cap.fields.push_back(std::move(s));
        }
      }
    }
    captured.push_back(std::move(cap));
  }

  // 2. Release everything that points into the old VM, then swap (the old VM dies before the old program).
  for (ScriptComponent* c : components_) c->releaseInstance();
  vm_ = std::move(next.vm);
  program_ = std::move(next.program);

  // 3. Recreate and restore in the new VM.
  for (Captured& cap : captured) {
    ScriptComponent* c = cap.comp;
    const auto r = c->createInstance();
    if (!r.ok()) {
      ++report.componentsDisabled;
      report.messages.push_back("script '" + c->className() + "' on '" + c->gameObject().name() + "' disabled: " + r.error().message);
      c->setEnabled(false);
      continue;
    }
    if (!isScriptClass(c->className())) {
      ++report.componentsDisabled;
      report.messages.push_back("script '" + c->className() + "' is no longer a valid script class; disabled");
      c->releaseInstance();
      c->setEnabled(false);
      continue;
    }
    Scope scope(*this, &c->scene());
    if (cap.hadHandle) c->bindGameObjectHandle();
    for (const Snap& s : cap.fields) {
      const FieldSym* f = c->findField(s.name);
      if (f == nullptr || f->isStatic || f->type->name() != s.typeName) {
        ++report.fieldsSkipped;
        continue;
      }
      if (s.isArray) {
        if (s.isNull) {
          vm_->setField(c->instance_->get(), *f, script::makeNull());
        } else {
          const MsHostApi* api = script::hostApi();
          MsValue arr = api->newArray(vm_.get(), elemKindOf(f->type->elem), static_cast<std::int64_t>(s.items.size()));
          script::Rooted keep(*vm_, arr);
          for (std::size_t i = 0; i < s.items.size(); ++i) api->arraySet(vm_.get(), keep.get(), static_cast<std::int64_t>(i), writeScalar(*vm_, s.items[i]));
          vm_->setField(c->instance_->get(), *f, keep.get());
        }
      } else {
        vm_->setField(c->instance_->get(), *f, writeScalar(*vm_, s.items[0]));
      }
      ++report.fieldsRestored;
    }
    ++report.componentsMigrated;
  }
  return report;
}

}  // namespace mingol::scripting
