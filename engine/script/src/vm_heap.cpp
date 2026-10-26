// SPDX-License-Identifier: GPL-2.0-only
// VM setup, object allocation and the mark-sweep collector.
#include "vm_impl.hpp"

namespace mingol::script {

void unloadPlugins(std::vector<void*>& handles);

VMImpl::VMImpl(VM& v, const Program& p, VMOptions o) : vm(v), prog(p), opt(std::move(o)) {
  stackCap = opt.maxStackValues;
  stack = std::make_unique<Value[]>(stackCap);
  nextGc = opt.gcThresholdBytes;
  emptyString = vObj(newStr(""));
  buildClasses();
  buildConstants();
  statics.resize(static_cast<std::size_t>(prog.staticCount));
  for (std::size_t i = 0; i < prog.staticFields.size(); ++i) statics[i] = defaultFor(prog.staticFields[i]->type);
  natives.assign(prog.funcs.size(), nullptr);
  for (std::size_t i = 0; i < prog.funcs.size(); ++i) {
    if (prog.funcs[i].isNative) nativeIds[prog.funcs[i].nativeName] = static_cast<int>(i);
  }
  auto id = [&](const char* n) { return classIdByName(n); };
  clsNullRef = id("mingol.NullReferenceError");
  clsIndex = id("mingol.IndexOutOfRangeError");
  clsDivZero = id("mingol.DivideByZeroError");
  clsCast = id("mingol.CastError");
  clsStackOverflow = id("mingol.StackOverflowError");
  clsOutOfMemory = id("mingol.OutOfMemoryError");
  clsArgument = id("mingol.ArgumentError");
  clsNative = id("mingol.NativeError");
  clsIntBox = id("mingol.IntBox");
  clsFloatBox = id("mingol.FloatBox");
  clsBoolBox = id("mingol.BoolBox");
  clsStringBox = id("mingol.StringBox");
  clsCoroutine = id("mingol.Coroutine");
  clsType = id("mingol.Type");
  clsFieldInfo = id("mingol.FieldInfo");
  clsMethodInfo = id("mingol.MethodInfo");
  bindPreludeNatives();
}

VMImpl::~VMImpl() {
  for (Obj* o = head; o != nullptr;) {
    Obj* n = o->next;
    destroy(o);
    o = n;
  }
  head = nullptr;
  unloadPlugins(pluginHandles);  // after every object (and its native-data finalizer) is gone
}

int VMImpl::classIdByName(const char* q) const {
  for (const auto& c : prog.classes) {
    if (c->qualifiedName == q) return c->id;
  }
  return -1;
}

Value VMImpl::defaultFor(ElemKind ek) const {
  switch (ek) {
    case ElemKind::Int: return makeInt(0);
    case ElemKind::Float: return makeFloat(0.0);
    case ElemKind::Bool: return makeBool(false);
    case ElemKind::String: return emptyString;
    default: return makeNull();
  }
}

Value VMImpl::defaultFor(const Type* t) const {
  switch (t->kind) {
    case TypeKind::Int: case TypeKind::Enum: return makeInt(0);
    case TypeKind::Float: return makeFloat(0.0);
    case TypeKind::Bool: return makeBool(false);
    case TypeKind::String: return emptyString;
    default: return makeNull();
  }
}

void VMImpl::buildClasses() {
  classes.resize(prog.classes.size());
  for (std::size_t i = 0; i < prog.classes.size(); ++i) {
    const ClassSym* c = prog.classes[i].get();
    ClassRt& rt = classes[i];
    rt.sym = c;
    for (const MethodSym* m : c->vtable) rt.vtable.push_back(m != nullptr ? m->funcId : -1);
    for (const auto& [key, m] : c->itable) rt.itable[key] = m != nullptr ? m->funcId : -1;
    if (c->kind == TypeDeclKind::Class) {
      rt.defaults.assign(static_cast<std::size_t>(c->instanceFieldCount), makeNull());
      for (const ClassSym* k = c; k != nullptr; k = k->base) {
        for (const auto& f : k->fields) {
          if (!f->isStatic && f->index >= 0) rt.defaults[static_cast<std::size_t>(f->index)] = defaultFor(f->type);
        }
      }
    }
  }
}

void VMImpl::buildConstants() {
  for (const Constant& c : prog.constants) {
    switch (c.kind) {
      case Constant::Kind::Int: consts.push_back(makeInt(c.i)); break;
      case Constant::Kind::Float: consts.push_back(makeFloat(c.f)); break;
      case Constant::Kind::String: consts.push_back(vObj(newStr(c.s))); break;
    }
  }
}

// ---- allocation --------------------------------------------------------------------------------------

template <class T>
T* VMImpl::track(T* o, std::size_t extra) {
  o->bytes = static_cast<std::uint32_t>(std::min<std::size_t>(sizeof(T) + extra, 0xFFFFFFFFu));
  o->next = head;
  head = o;
  bytes += sizeof(T) + extra;
  ++liveObjects;
  return o;
}

StringObj* VMImpl::newStr(std::string s) {
  auto* o = new StringObj();
  const std::size_t n = s.size();
  o->s = std::move(s);
  return track(o, n);
}

InstanceObj* VMImpl::newInstance(const ClassRt& c) {
  auto* o = new InstanceObj();
  o->cls = &c;
  o->fields = c.defaults;
  return track(o, c.defaults.size() * sizeof(Value));
}

ArrayObj* VMImpl::newArr(ElemKind ek, std::size_t n) {
  auto* o = new ArrayObj();
  o->ek = ek;
  o->items.assign(n, defaultFor(ek));
  return track(o, n * sizeof(Value));
}

ClosureObj* VMImpl::newClosure(int funcId, std::size_t ncap) {
  auto* o = new ClosureObj();
  o->funcId = funcId;
  o->captures.assign(ncap, makeNull());
  return track(o, ncap * sizeof(Value));
}

BoxObj* VMImpl::newBox(Value v) {
  auto* o = new BoxObj();
  o->v = v;
  return track(o, 0);
}

RefObj* VMImpl::newRef(RefObj::RK rk, Obj* target, std::int32_t index) {
  auto* o = new RefObj();
  o->rk = rk;
  o->target = target;
  o->index = index;
  return track(o, 0);
}

void VMImpl::destroy(Obj* o) {
  switch (o->kind) {
    case ObjKind::Instance: delete static_cast<InstanceObj*>(o); break;
    case ObjKind::String: delete static_cast<StringObj*>(o); break;
    case ObjKind::Array: delete static_cast<ArrayObj*>(o); break;
    case ObjKind::Closure: delete static_cast<ClosureObj*>(o); break;
    case ObjKind::Box: delete static_cast<BoxObj*>(o); break;
    case ObjKind::Ref: delete static_cast<RefObj*>(o); break;
  }
}

// ---- collector ---------------------------------------------------------------------------------------

void VMImpl::markObj(Obj* o) {
  if (o == nullptr || o->marked) return;
  o->marked = true;
  gray.push_back(o);
}

void VMImpl::markValue(const Value& v) {
  if (v.tag == MS_TAG_OBJECT) markObj(objOf(v));
}

void CoroutineData::trace(VMImpl& vm) {
  for (const Value& v : locals) vm.markValue(v);
  vm.markValue(yielded);
}

void VMImpl::traceObj(Obj* o) {
  switch (o->kind) {
    case ObjKind::Instance: {
      auto* i = static_cast<InstanceObj*>(o);
      for (const Value& v : i->fields) markValue(v);
      if (i->native) i->native->trace(*this);
      break;
    }
    case ObjKind::String: break;
    case ObjKind::Array: {
      auto* a = static_cast<ArrayObj*>(o);
      if (a->ek == ElemKind::String || a->ek == ElemKind::Ref) {
        for (const Value& v : a->items) markValue(v);
      }
      break;
    }
    case ObjKind::Closure: {
      auto* c = static_cast<ClosureObj*>(o);
      markValue(c->self);
      for (const Value& v : c->captures) markValue(v);
      break;
    }
    case ObjKind::Box: markValue(static_cast<BoxObj*>(o)->v); break;
    case ObjKind::Ref: markObj(static_cast<RefObj*>(o)->target); break;
  }
}

void VMImpl::collect() {
  if (inGc) return;
  inGc = true;
  markValue(emptyString);
  markValue(retVal);
  if (hasPending) markValue(pending);
  for (const Value& v : consts) markValue(v);
  for (const Value& v : statics) markValue(v);
  for (std::size_t i = 0; i < sp; ++i) markValue(stack[i]);
  for (const Frame& f : frames) {
    markObj(f.closure);
  }
  for (const auto& [id, v] : pins) markValue(v);
  for (auto& c : classes) markObj(c.typeObj);
  for (auto& [k, o] : typeObjs) markObj(o);
  for (auto& [k, o] : fieldObjs) markObj(o);
  for (auto& [k, o] : methodObjs) markObj(o);
  while (!gray.empty()) {
    Obj* o = gray.back();
    gray.pop_back();
    traceObj(o);
  }
  Obj** link = &head;
  std::size_t liveBytes = 0;
  std::size_t live = 0;
  while (*link != nullptr) {
    Obj* o = *link;
    if (o->marked) {
      o->marked = false;
      liveBytes += o->bytes;
      ++live;
      link = &o->next;
    } else {
      *link = o->next;
      destroy(o);
    }
  }
  bytes = liveBytes;
  liveObjects = live;
  ++collections;
  nextGc = std::max(opt.gcThresholdBytes, liveBytes * 2);
  inGc = false;
}

}  // namespace mingol::script
