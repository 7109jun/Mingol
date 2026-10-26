// SPDX-License-Identifier: GPL-2.0-only
// Natives of the language prelude: Coroutine, Type, FieldInfo, MethodInfo.
#include "vm_impl.hpp"

namespace mingol::script {

namespace {

VMImpl& I(MsVM* vm) { return static_cast<VM*>(vm)->impl(); }

Value str(VMImpl& m, std::string s) { return vObj(m.newStr(std::move(s))); }

// Receiver helpers: natives of these classes only ever run on instances created by the VM.
template <class T>
T* dataOf(const Value& v) {
  return isKind(v, ObjKind::Instance) ? dynamic_cast<T*>(instOf(v)->native.get()) : nullptr;
}

const Type* typeOf(VMImpl& m, const Value& v) {
  auto* d = dataOf<TypeData>(v);
  if (d == nullptr) m.raise(m.clsArgument, "invalid Type object");
  return d != nullptr ? d->type : nullptr;
}

// ---- Coroutine ----------------------------------------------------------------------------------------

CoroutineData* co(VMImpl& m, const Value& v) {
  auto* d = dataOf<CoroutineData>(v);
  if (d == nullptr) m.raise(m.clsArgument, "invalid Coroutine object");
  return d;
}

void coIsDone(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (auto* d = co(m, a[0])) *r = makeBool(d->done);
}
void coResume(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  VMImpl& m = I(vm);
  if (co(m, a[0]) == nullptr) return;
  m.resumeCo(instOf(a[0]));  // a thrown exception is left pending
}
void coKind(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (auto* d = co(m, a[0])) *r = makeInt(d->yieldKind);
}
void coInt(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  auto* d = co(m, a[0]);
  if (d == nullptr) return;
  if (d->yieldKind != 1) { m.raise(m.clsArgument, "the coroutine did not yield an int"); return; }
  *r = makeInt(d->yielded.u.i);
}
void coFloat(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  auto* d = co(m, a[0]);
  if (d == nullptr) return;
  if (d->yieldKind == 1) *r = makeFloat(static_cast<double>(d->yielded.u.i));
  else if (d->yieldKind == 2) *r = makeFloat(d->yielded.u.f);
  else m.raise(m.clsArgument, "the coroutine did not yield a float");
}
void coBool(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  auto* d = co(m, a[0]);
  if (d == nullptr) return;
  if (d->yieldKind != 3) { m.raise(m.clsArgument, "the coroutine did not yield a bool"); return; }
  *r = makeBool(d->yielded.u.i != 0);
}
void coString(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  auto* d = co(m, a[0]);
  if (d == nullptr) return;
  if (d->yieldKind != 4) { m.raise(m.clsArgument, "the coroutine did not yield a string"); return; }
  *r = d->yielded;
}
void coObject(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  auto* d = co(m, a[0]);
  if (d == nullptr) return;
  if (d->yieldKind == 0) { *r = makeNull(); return; }
  if (d->yieldKind != 5) { m.raise(m.clsArgument, "the coroutine did not yield an object"); return; }
  *r = d->yielded;
}

// ---- Type ---------------------------------------------------------------------------------------------

void tyName(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (const Type* t = typeOf(m, a[0])) *r = str(m, t->name());
}
void tyBase(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  *r = (t->kind == TypeKind::Class && t->cls->base != nullptr) ? m.typeObject(t->cls->base->thisType) : makeNull();
}
void tyIsClass(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const Type* t = typeOf(I(vm), a[0])) *r = makeBool(t->kind == TypeKind::Class);
}
void tyIsInterface(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const Type* t = typeOf(I(vm), a[0])) *r = makeBool(t->kind == TypeKind::Interface);
}
void tyIsEnum(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const Type* t = typeOf(I(vm), a[0])) *r = makeBool(t->kind == TypeKind::Enum);
}
void tyIsAbstract(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const Type* t = typeOf(I(vm), a[0])) *r = makeBool(t->kind == TypeKind::Class && t->cls->mods.isAbstract);
}
void tyAssignable(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  if (a[1].tag != MS_TAG_OBJECT) { m.raise(m.clsNullRef, "null reference"); return; }
  const Type* o = typeOf(m, a[1]);
  if (o == nullptr) return;
  if (t->isClassLike() && o->isClassLike()) *r = makeBool(o->cls->isAssignableTo(t->cls));
  else *r = makeBool(t == o);
}

std::vector<const FieldSym*> allFields(const ClassSym* c) {
  std::vector<const ClassSym*> chain;
  for (const ClassSym* k = c; k != nullptr; k = k->base) chain.push_back(k);
  std::vector<const FieldSym*> out;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (const auto& f : (*it)->fields) out.push_back(f.get());
  }
  return out;
}

std::vector<const MethodSym*> allMethods(const ClassSym* c) {
  std::vector<const MethodSym*> out;
  std::vector<std::string> seen;
  for (const ClassSym* k = c; k != nullptr; k = k->base) {
    for (const auto& mm : k->methods) {
      if (mm->mkind != MethodKind::Method) continue;
      const std::string sig = mm->signature();
      if (std::find(seen.begin(), seen.end(), sig) != seen.end()) continue;
      seen.push_back(sig);
      out.push_back(mm.get());
    }
  }
  return out;
}

void tyFields(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  std::vector<const FieldSym*> fs;
  if (t->kind == TypeKind::Class) fs = allFields(t->cls);
  ArrayObj* arr = m.newArr(ElemKind::Ref, fs.size());
  for (std::size_t i = 0; i < fs.size(); ++i) arr->items[i] = m.fieldObject(fs[i]);
  *r = vObj(arr);
}
void tyMethods(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  std::vector<const MethodSym*> ms;
  if (t->isClassLike()) ms = allMethods(t->cls);
  ArrayObj* arr = m.newArr(ElemKind::Ref, ms.size());
  for (std::size_t i = 0; i < ms.size(); ++i) arr->items[i] = m.methodObject(ms[i]);
  *r = vObj(arr);
}
void tyField(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  const std::string& n = strOf(a[1])->s;
  *r = makeNull();
  if (t->kind != TypeKind::Class) return;
  for (const FieldSym* f : allFields(t->cls)) {
    if (f->name == n) *r = m.fieldObject(f);
  }
}
void tyMethod(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  const std::string& n = strOf(a[1])->s;
  *r = makeNull();
  if (!t->isClassLike()) return;
  for (const MethodSym* mm : allMethods(t->cls)) {
    if (mm->name == n) { *r = m.methodObject(mm); return; }
  }
}
void tyNewInstance(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const Type* t = typeOf(m, a[0]);
  if (t == nullptr) return;
  if (t->kind != TypeKind::Class || t->cls->mods.isAbstract || t->cls->mods.isNative) { m.raise(m.clsArgument, "cannot instantiate '" + t->name() + "'"); return; }
  const MethodSym* ctor = nullptr;
  for (const auto& mm : t->cls->methods) {
    if (mm->mkind == MethodKind::Constructor && mm->params.empty() && mm->mods.access == Access::Public) ctor = mm.get();
  }
  if (ctor == nullptr) { m.raise(m.clsArgument, "'" + t->name() + "' has no public parameterless constructor"); return; }
  InstanceObj* o = m.newInstance(m.classes[static_cast<std::size_t>(t->cls->id)]);
  m.stack[m.sp] = vObj(o);
  if (m.invoke(ctor->funcId, 1, nullptr) == Status::Threw) return;
  *r = vObj(o);
}
void tyForName(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const int id = m.classIdByName(strOf(a[0])->s.c_str());
  *r = id < 0 ? makeNull() : m.typeObject(m.prog.classes[static_cast<std::size_t>(id)]->thisType);
}

// ---- FieldInfo ----------------------------------------------------------------------------------------

const FieldSym* fieldOf(VMImpl& m, const Value& v) {
  auto* d = dataOf<FieldData>(v);
  if (d == nullptr) m.raise(m.clsArgument, "invalid FieldInfo object");
  return d != nullptr ? d->field : nullptr;
}

// Locates the storage of `f` for `target`; returns null (with an exception pending) on failure.
Value* slotOf(VMImpl& m, const FieldSym* f, const Value& target) {
  if (f->isStatic) return &m.statics[static_cast<std::size_t>(f->index)];
  if (target.tag != MS_TAG_OBJECT) { m.raise(m.clsNullRef, "an instance is required for field '" + f->name + "'"); return nullptr; }
  InstanceObj* o = instOf(target);
  if (!o->cls->sym->isAssignableTo(f->owner)) { m.raise(m.clsArgument, "object is not an instance of '" + f->owner->name + "'"); return nullptr; }
  return &o->fields[static_cast<std::size_t>(f->index)];
}

void fiName(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (const FieldSym* f = fieldOf(m, a[0])) *r = str(m, f->name);
}
void fiTypeName(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (const FieldSym* f = fieldOf(m, a[0])) *r = str(m, f->type->name());
}
void fiIsStatic(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const FieldSym* f = fieldOf(I(vm), a[0])) *r = makeBool(f->isStatic);
}

bool needKind(VMImpl& m, const FieldSym* f, bool ok, const char* what) {
  if (!ok) m.raise(m.clsArgument, "field '" + f->name + "' is '" + f->type->name() + "', not " + what);
  return ok;
}

void fiGetInt(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::Int || f->type->kind == TypeKind::Enum, "an int")) return;
  if (Value* s = slotOf(m, f, a[1])) *r = makeInt(s->u.i);
}
void fiGetFloat(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::Float, "a float")) return;
  if (Value* s = slotOf(m, f, a[1])) *r = makeFloat(s->u.f);
}
void fiGetBool(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::Bool, "a bool")) return;
  if (Value* s = slotOf(m, f, a[1])) *r = makeBool(s->u.i != 0);
}
void fiGetString(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::String, "a string")) return;
  if (Value* s = slotOf(m, f, a[1])) *r = *s;
}
void fiGetObject(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->isClassLike(), "an object")) return;
  if (Value* s = slotOf(m, f, a[1])) *r = *s;
}
void fiSetInt(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::Int || f->type->kind == TypeKind::Enum, "an int")) return;
  if (Value* s = slotOf(m, f, a[1])) *s = makeInt(a[2].u.i);
}
void fiSetFloat(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::Float, "a float")) return;
  if (Value* s = slotOf(m, f, a[1])) *s = makeFloat(a[2].u.f);
}
void fiSetBool(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::Bool, "a bool")) return;
  if (Value* s = slotOf(m, f, a[1])) *s = makeBool(a[2].u.i != 0);
}
void fiSetString(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->kind == TypeKind::String, "a string")) return;
  if (Value* s = slotOf(m, f, a[1])) *s = a[2];
}
void fiSetObject(MsVM* vm, MsValue* a, int32_t, MsValue*) {
  VMImpl& m = I(vm);
  const FieldSym* f = fieldOf(m, a[0]);
  if (f == nullptr || !needKind(m, f, f->type->isClassLike(), "an object")) return;
  if (a[2].tag == MS_TAG_OBJECT && !instOf(a[2])->cls->sym->isAssignableTo(f->type->cls)) {
    m.raise(m.clsCast, "cannot store " + instOf(a[2])->cls->sym->qualifiedName + " in a field of type " + f->type->name());
    return;
  }
  if (Value* s = slotOf(m, f, a[1])) *s = a[2];
}

// ---- MethodInfo ---------------------------------------------------------------------------------------

const MethodSym* methodOf(VMImpl& m, const Value& v) {
  auto* d = dataOf<MethodData>(v);
  if (d == nullptr) m.raise(m.clsArgument, "invalid MethodInfo object");
  return d != nullptr ? d->method : nullptr;
}

void miName(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (const MethodSym* mm = methodOf(m, a[0])) *r = str(m, mm->name);
}
void miRet(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  if (const MethodSym* mm = methodOf(m, a[0])) *r = str(m, mm->ret->name());
}
void miCount(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const MethodSym* mm = methodOf(I(vm), a[0])) *r = makeInt(static_cast<std::int64_t>(mm->params.size()));
}
void miStatic(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  if (const MethodSym* mm = methodOf(I(vm), a[0])) *r = makeBool(mm->mods.isStatic);
}

bool unbox(VMImpl& m, const Value& v, int boxClass, Value& out) {
  if (v.tag != MS_TAG_OBJECT || instOf(v)->cls != m.classRt(boxClass)) return false;
  out = instOf(v)->fields[0];
  return true;
}

void miInvoke(MsVM* vm, MsValue* a, int32_t, MsValue* r) {
  VMImpl& m = I(vm);
  const MethodSym* mm = methodOf(m, a[0]);
  if (mm == nullptr) return;
  if (mm->funcId < 0) { m.raise(m.clsArgument, "method '" + mm->name + "' has no implementation"); return; }
  const Value target = a[1];
  const std::size_t want = mm->params.size();
  const std::size_t have = a[2].tag == MS_TAG_OBJECT ? arrOf(a[2])->items.size() : 0;
  if (have != want) { m.raise(m.clsArgument, "'" + mm->signature() + "' needs " + std::to_string(want) + " argument(s), got " + std::to_string(have)); return; }
  int funcId = mm->funcId;
  if (!mm->mods.isStatic) {
    if (target.tag != MS_TAG_OBJECT) { m.raise(m.clsNullRef, "an instance is required to call '" + mm->name + "'"); return; }
    if (!instOf(target)->cls->sym->isAssignableTo(mm->owner)) { m.raise(m.clsArgument, "object is not an instance of '" + mm->owner->name + "'"); return; }
    if (mm->vtableSlot >= 0) funcId = m.resolveVirtual(target, mm->vtableSlot);
    else if (mm->owner->kind == TypeDeclKind::Interface) {
      auto it = instOf(target)->cls->itable.find(mm->interfaceKey);
      if (it == instOf(target)->cls->itable.end()) { m.raise(m.clsCast, "object does not implement '" + mm->signature() + "'"); return; }
      funcId = it->second;
    }
  }
  std::vector<Value> argv;
  if (!mm->mods.isStatic) argv.push_back(target);
  for (std::size_t i = 0; i < want; ++i) {
    const ParamInfo& p = mm->params[i];
    const Value& boxed = arrOf(a[2])->items[i];
    Value v;
    if (p.isRef) { m.raise(m.clsArgument, "'ref' parameters cannot be passed through reflection"); return; }
    switch (p.type->kind) {
      case TypeKind::Int: case TypeKind::Enum:
        if (!unbox(m, boxed, m.clsIntBox, v)) { m.raise(m.clsArgument, "argument " + std::to_string(i + 1) + " must be an IntBox"); return; }
        break;
      case TypeKind::Float:
        if (unbox(m, boxed, m.clsFloatBox, v)) break;
        if (unbox(m, boxed, m.clsIntBox, v)) { v = makeFloat(static_cast<double>(v.u.i)); break; }
        m.raise(m.clsArgument, "argument " + std::to_string(i + 1) + " must be a FloatBox");
        return;
      case TypeKind::Bool:
        if (!unbox(m, boxed, m.clsBoolBox, v)) { m.raise(m.clsArgument, "argument " + std::to_string(i + 1) + " must be a BoolBox"); return; }
        break;
      case TypeKind::String:
        if (!unbox(m, boxed, m.clsStringBox, v)) { m.raise(m.clsArgument, "argument " + std::to_string(i + 1) + " must be a StringBox"); return; }
        break;
      case TypeKind::Class: case TypeKind::Interface:
        if (boxed.tag == MS_TAG_OBJECT && !instOf(boxed)->cls->sym->isAssignableTo(p.type->cls)) { m.raise(m.clsCast, "argument " + std::to_string(i + 1) + " is not a " + p.type->name()); return; }
        v = boxed;
        break;
      default:
        m.raise(m.clsArgument, "parameter type '" + p.type->name() + "' is not supported by reflection");
        return;
    }
    argv.push_back(v);
  }
  if (mm->mods.isCoroutine) { *r = m.newCoroutine(funcId, argv.data(), argv.size()); return; }
  for (std::size_t i = 0; i < argv.size(); ++i) m.stack[m.sp + i] = argv[i];
  if (m.invoke(funcId, static_cast<std::uint32_t>(argv.size()), nullptr) == Status::Threw) return;
  const Value ret = m.retVal;
  m.retVal = makeNull();
  auto box = [&](int cls, Value v) {
    InstanceObj* b = m.newInstance(m.classes[static_cast<std::size_t>(cls)]);
    b->fields[0] = v;
    return vObj(b);
  };
  switch (mm->ret->kind) {
    case TypeKind::Void: *r = makeNull(); break;
    case TypeKind::Int: case TypeKind::Enum: *r = box(m.clsIntBox, ret); break;
    case TypeKind::Float: *r = box(m.clsFloatBox, ret); break;
    case TypeKind::Bool: *r = box(m.clsBoolBox, ret); break;
    case TypeKind::String: *r = box(m.clsStringBox, ret); break;
    case TypeKind::Class: case TypeKind::Interface: *r = ret; break;
    default: m.raise(m.clsArgument, "return type '" + mm->ret->name() + "' is not supported by reflection"); break;
  }
}

}  // namespace

Value VMImpl::fieldObject(const FieldSym* f) {
  auto it = fieldObjs.find(f);
  if (it != fieldObjs.end()) return vObj(it->second);
  InstanceObj* o = newInstance(classes[static_cast<std::size_t>(clsFieldInfo)]);
  auto d = std::make_unique<FieldData>();
  d->field = f;
  o->native = std::move(d);
  fieldObjs[f] = o;
  return vObj(o);
}

Value VMImpl::methodObject(const MethodSym* mm) {
  auto it = methodObjs.find(mm);
  if (it != methodObjs.end()) return vObj(it->second);
  InstanceObj* o = newInstance(classes[static_cast<std::size_t>(clsMethodInfo)]);
  auto d = std::make_unique<MethodData>();
  d->method = mm;
  o->native = std::move(d);
  methodObjs[mm] = o;
  return vObj(o);
}

Value VMImpl::newCoroutine(int funcId, const Value* args, std::size_t argc) {
  const FuncProto& fn = prog.funcs[static_cast<std::size_t>(funcId)];
  InstanceObj* c = newInstance(classes[static_cast<std::size_t>(clsCoroutine)]);
  auto cd = std::make_unique<CoroutineData>();
  cd->funcId = funcId;
  cd->locals.assign(static_cast<std::size_t>(fn.numLocals), makeNull());
  for (std::size_t i = 0; i < argc; ++i) cd->locals[i] = args[i];
  c->native = std::move(cd);
  return vObj(c);
}

void VMImpl::bindPreludeNatives() {
  struct Entry { const char* name; MsNativeFn fn; };
  static const Entry kTable[] = {
      {"mingol.Coroutine.isDone()", coIsDone},
      {"mingol.Coroutine.resume()", coResume},
      {"mingol.Coroutine.yieldKind()", coKind},
      {"mingol.Coroutine.yieldedInt()", coInt},
      {"mingol.Coroutine.yieldedFloat()", coFloat},
      {"mingol.Coroutine.yieldedBool()", coBool},
      {"mingol.Coroutine.yieldedString()", coString},
      {"mingol.Coroutine.yieldedObject()", coObject},
      {"mingol.Type.getName()", tyName},
      {"mingol.Type.getBase()", tyBase},
      {"mingol.Type.isClass()", tyIsClass},
      {"mingol.Type.isInterface()", tyIsInterface},
      {"mingol.Type.isEnum()", tyIsEnum},
      {"mingol.Type.isAbstract()", tyIsAbstract},
      {"mingol.Type.isAssignableFrom(mingol.Type)", tyAssignable},
      {"mingol.Type.getFields()", tyFields},
      {"mingol.Type.getMethods()", tyMethods},
      {"mingol.Type.getField(string)", tyField},
      {"mingol.Type.getMethod(string)", tyMethod},
      {"mingol.Type.newInstance()", tyNewInstance},
      {"mingol.Type.forName(string)", tyForName},
      {"mingol.FieldInfo.getName()", fiName},
      {"mingol.FieldInfo.getTypeName()", fiTypeName},
      {"mingol.FieldInfo.isStatic()", fiIsStatic},
      {"mingol.FieldInfo.getInt(mingol.Object)", fiGetInt},
      {"mingol.FieldInfo.getFloat(mingol.Object)", fiGetFloat},
      {"mingol.FieldInfo.getBool(mingol.Object)", fiGetBool},
      {"mingol.FieldInfo.getString(mingol.Object)", fiGetString},
      {"mingol.FieldInfo.getObject(mingol.Object)", fiGetObject},
      {"mingol.FieldInfo.setInt(mingol.Object, int)", fiSetInt},
      {"mingol.FieldInfo.setFloat(mingol.Object, float)", fiSetFloat},
      {"mingol.FieldInfo.setBool(mingol.Object, bool)", fiSetBool},
      {"mingol.FieldInfo.setString(mingol.Object, string)", fiSetString},
      {"mingol.FieldInfo.setObject(mingol.Object, mingol.Object)", fiSetObject},
      {"mingol.MethodInfo.getName()", miName},
      {"mingol.MethodInfo.getReturnTypeName()", miRet},
      {"mingol.MethodInfo.getParameterCount()", miCount},
      {"mingol.MethodInfo.isStatic()", miStatic},
      {"mingol.MethodInfo.invoke(mingol.Object, mingol.Object[])", miInvoke},
  };
  for (const Entry& e : kTable) {
    auto it = nativeIds.find(e.name);
    MINGOL_VERIFY_MSG(it != nativeIds.end(), e.name);
    natives[static_cast<std::size_t>(it->second)] = e.fn;
  }
}

}  // namespace mingol::script
