// SPDX-License-Identifier: GPL-2.0-only
// Public VM methods and the C host API handed to plugins.
#include "vm_impl.hpp"

namespace mingol::script {

namespace {

struct PluginNativeData final : NativeData {
  void* data = nullptr;
  void (*fin)(void*) = nullptr;
  ~PluginNativeData() override {
    if (fin != nullptr) fin(data);
  }
};

VMImpl& I(MsVM* vm) { return static_cast<VM*>(vm)->impl(); }

}  // namespace

VM::VM(const Program& program, VMOptions options) : impl_(std::make_unique<VMImpl>(*this, program, std::move(options))) {
  impl_->frames.reserve(impl_->opt.maxFrames);
}
VM::~VM() = default;

const Program& VM::program() const noexcept { return impl_->prog; }

Result<void> VM::bindNative(const std::string& name, MsNativeFn fn) {
  auto it = impl_->nativeIds.find(name);
  if (it == impl_->nativeIds.end()) return makeError(ErrorCode::NotFound, "no native method named '{}' in the loaded program", name);
  if (fn == nullptr) return makeError(ErrorCode::InvalidArgument, "native function for '{}' is null", name);
  impl_->natives[static_cast<std::size_t>(it->second)] = fn;
  return {};
}

std::vector<std::string> VM::unboundNatives() const {
  std::vector<std::string> out;
  for (const auto& [name, id] : impl_->nativeIds) {
    if (impl_->natives[static_cast<std::size_t>(id)] == nullptr) out.push_back(name);
  }
  return out;
}

// ---- execution ------------------------------------------------------------------------------------------

namespace {
bool finish(VMImpl& m, Status st, Value& result, ScriptException& ex) {
  if (st == Status::Threw) {
    ex = m.flatten(m.pending);
    m.hasPending = false;
    m.pending = makeNull();
    return false;
  }
  result = m.retVal;
  m.retVal = makeNull();
  return true;
}

}  // namespace

bool VM::initialize(ScriptException& ex) {
  VMImpl& m = *impl_;
  for (int funcId : m.prog.staticInitFuncs) {
    Value r;
    const Status st = m.invoke(funcId, 0, nullptr);
    if (!finish(m, st, r, ex)) return false;
  }
  return true;
}

bool VM::call(const MethodSym& method, Value receiver, std::span<const Value> args, Value& result, ScriptException& ex) {
  VMImpl& m = *impl_;
  if (method.funcId < 0) {
    ex = {"mingol.ArgumentError", "method '" + method.name + "' has no implementation", ""};
    return false;
  }
  int funcId = method.funcId;
  const bool isStatic = method.mods.isStatic;
  if (!isStatic) {
    if (!isObject(receiver)) {
      ex = {"mingol.NullReferenceError", "null receiver", ""};
      return false;
    }
    if (method.vtableSlot >= 0 && isKind(receiver, ObjKind::Instance)) funcId = m.resolveVirtual(receiver, method.vtableSlot);
  }
  if (args.size() != method.params.size()) {
    ex = {"mingol.ArgumentError", "wrong number of arguments for '" + method.signature() + "'", ""};
    return false;
  }
  std::vector<Value> all;
  if (!isStatic) all.push_back(receiver);
  all.insert(all.end(), args.begin(), args.end());
  if (method.mods.isCoroutine) {
    result = m.newCoroutine(funcId, all.data(), all.size());
    return true;
  }
  if (m.sp + all.size() + 1 >= m.stackCap) {
    ex = {"mingol.StackOverflowError", "stack overflow", ""};
    return false;
  }
  for (std::size_t i = 0; i < all.size(); ++i) m.stack[m.sp + i] = all[i];
  const Status st = m.invoke(funcId, static_cast<std::uint32_t>(all.size()), nullptr);
  return finish(m, st, result, ex);
}

bool VM::callClosure(Value closure, std::span<const Value> args, Value& result, ScriptException& ex) {
  VMImpl& m = *impl_;
  if (!isKind(closure, ObjKind::Closure)) {
    ex = {"mingol.NullReferenceError", "not a function value", ""};
    return false;
  }
  ClosureObj* c = cloOf(closure);
  std::size_t n = 0;
  if (m.sp + args.size() + 2 >= m.stackCap) {
    ex = {"mingol.StackOverflowError", "stack overflow", ""};
    return false;
  }
  if (c->bound) m.stack[m.sp + n++] = c->self;
  for (const Value& a : args) m.stack[m.sp + n++] = a;
  const Status st = m.invoke(c->funcId, static_cast<std::uint32_t>(n), c);
  return finish(m, st, result, ex);
}

Value VM::allocate(const ClassSym& cls) { return vObj(impl_->newInstance(impl_->classes[static_cast<std::size_t>(cls.id)])); }

bool VM::construct(const ClassSym& cls, const MethodSym& ctor, std::span<const Value> args, Value& result, ScriptException& ex) {
  VMImpl& m = *impl_;
  if (ctor.mkind != MethodKind::Constructor || ctor.owner != &cls || ctor.funcId < 0 || args.size() != ctor.params.size()) {
    ex = {"mingol.ArgumentError", "invalid constructor call", ""};
    return false;
  }
  if (cls.mods.isAbstract || cls.kind != TypeDeclKind::Class) {
    ex = {"mingol.ArgumentError", "cannot instantiate '" + cls.name + "'", ""};
    return false;
  }
  Value obj = allocate(cls);
  if (m.sp + args.size() + 2 >= m.stackCap) {
    ex = {"mingol.StackOverflowError", "stack overflow", ""};
    return false;
  }
  m.stack[m.sp] = obj;
  for (std::size_t i = 0; i < args.size(); ++i) m.stack[m.sp + 1 + i] = args[i];
  Value r;
  const Status st = m.invoke(ctor.funcId, static_cast<std::uint32_t>(args.size() + 1), nullptr);
  if (!finish(m, st, r, ex)) return false;
  result = obj;
  return true;
}

// ---- values ---------------------------------------------------------------------------------------------

Value VM::newString(std::string_view text) { return vObj(impl_->newStr(std::string(text))); }
bool VM::isString(Value v) const { return isKind(v, ObjKind::String); }
std::string_view VM::stringValue(Value v) const { return isKind(v, ObjKind::String) ? std::string_view(strOf(v)->s) : std::string_view(); }
const ClassSym* VM::classOf(Value v) const { return isKind(v, ObjKind::Instance) ? instOf(v)->cls->sym : nullptr; }

Value VM::getField(Value object, const FieldSym& field) const {
  MINGOL_VERIFY(isKind(object, ObjKind::Instance) && !field.isStatic);
  return instOf(object)->fields[static_cast<std::size_t>(field.index)];
}
void VM::setField(Value object, const FieldSym& field, Value value) {
  MINGOL_VERIFY(isKind(object, ObjKind::Instance) && !field.isStatic);
  instOf(object)->fields[static_cast<std::size_t>(field.index)] = value;
}
Value VM::getStatic(const FieldSym& field) const { return impl_->statics[static_cast<std::size_t>(field.index)]; }
void VM::setStatic(const FieldSym& field, Value value) { impl_->statics[static_cast<std::size_t>(field.index)] = value; }

// ---- coroutines -----------------------------------------------------------------------------------------

namespace {
CoroutineData* coData(const Value& v) {
  if (!isKind(v, ObjKind::Instance)) return nullptr;
  return dynamic_cast<CoroutineData*>(instOf(v)->native.get());
}
}  // namespace

bool VM::isCoroutine(Value v) const { return coData(v) != nullptr; }
bool VM::coroutineDone(Value co) const {
  CoroutineData* d = coData(co);
  return d == nullptr || d->done;
}
bool VM::resumeCoroutine(Value co, ScriptException& ex) {
  VMImpl& m = *impl_;
  if (coData(co) == nullptr) {
    ex = {"mingol.ArgumentError", "not a coroutine", ""};
    return false;
  }
  // keep the coroutine reachable while it runs
  m.stack[m.sp] = co;
  ++m.sp;
  const Status st = m.resumeCo(instOf(co));
  --m.sp;
  Value r;
  return finish(m, st == Status::Yielded ? Status::Returned : st, r, ex);
}
int VM::coroutineYieldKind(Value co) const {
  CoroutineData* d = coData(co);
  return d != nullptr ? d->yieldKind : 0;
}
Value VM::coroutineYielded(Value co) const {
  CoroutineData* d = coData(co);
  return d != nullptr ? d->yielded : makeNull();
}

// ---- memory ---------------------------------------------------------------------------------------------

std::uint64_t VM::pin(Value v) {
  const std::uint64_t id = impl_->nextPin++;
  impl_->pins[id] = v;
  return id;
}
void VM::unpin(std::uint64_t id) { impl_->pins.erase(id); }
Value VM::pinned(std::uint64_t id) const {
  auto it = impl_->pins.find(id);
  return it == impl_->pins.end() ? makeNull() : it->second;
}
void VM::collectGarbage() { impl_->collect(); }
HeapStats VM::heapStats() const { return {impl_->liveObjects, impl_->bytes, impl_->collections}; }

// ---- C host API -----------------------------------------------------------------------------------------

namespace {

FieldSym* findFieldByName(InstanceObj* o, const char* name) {
  for (const ClassSym* c = o->cls->sym; c != nullptr; c = c->base) {
    for (const auto& f : c->fields) {
      if (!f->isStatic && f->name == name) return f.get();
    }
  }
  return nullptr;
}

MsValue apiNewString(MsVM* vm, const char* s, size_t n) { return vObj(I(vm).newStr(std::string(s != nullptr ? s : "", s != nullptr ? n : 0))); }
const char* apiStringData(MsVM*, MsValue v, size_t* len) {
  if (!isKind(v, ObjKind::String)) {
    if (len != nullptr) *len = 0;
    return "";
  }
  if (len != nullptr) *len = strOf(v)->s.size();
  return strOf(v)->s.c_str();
}
void apiThrow(MsVM* vm, const char* cls, const char* msg) { I(vm).raiseNamed(cls != nullptr ? cls : "", msg != nullptr ? msg : ""); }
void apiSetNativeData(MsVM* vm, MsValue obj, void* data, void (*fin)(void*)) {
  if (!isKind(obj, ObjKind::Instance)) { I(vm).raise(I(vm).clsArgument, "setNativeData needs an object"); return; }
  auto d = std::make_unique<PluginNativeData>();
  d->data = data;
  d->fin = fin;
  instOf(obj)->native = std::move(d);
}
void* apiGetNativeData(MsVM*, MsValue obj) {
  if (!isKind(obj, ObjKind::Instance)) return nullptr;
  auto* d = dynamic_cast<PluginNativeData*>(instOf(obj)->native.get());
  return d != nullptr ? d->data : nullptr;
}
MsValue apiNewObject(MsVM* vm, const char* cls) {
  VMImpl& m = I(vm);
  const int id = m.classIdByName(cls != nullptr ? cls : "");
  if (id < 0 || m.classes[static_cast<std::size_t>(id)].sym->kind != TypeDeclKind::Class) {
    m.raise(m.clsArgument, std::string("unknown class '") + (cls != nullptr ? cls : "") + "'");
    return makeNull();
  }
  return vObj(m.newInstance(m.classes[static_cast<std::size_t>(id)]));
}
MsValue apiGetField(MsVM* vm, MsValue obj, const char* name) {
  VMImpl& m = I(vm);
  if (!isKind(obj, ObjKind::Instance)) { m.raise(m.clsNullRef, "null reference"); return makeNull(); }
  FieldSym* f = findFieldByName(instOf(obj), name);
  if (f == nullptr) { m.raise(m.clsArgument, std::string("no field '") + name + "'"); return makeNull(); }
  return instOf(obj)->fields[static_cast<std::size_t>(f->index)];
}
void apiSetField(MsVM* vm, MsValue obj, const char* name, MsValue v) {
  VMImpl& m = I(vm);
  if (!isKind(obj, ObjKind::Instance)) { m.raise(m.clsNullRef, "null reference"); return; }
  FieldSym* f = findFieldByName(instOf(obj), name);
  if (f == nullptr) { m.raise(m.clsArgument, std::string("no field '") + name + "'"); return; }
  instOf(obj)->fields[static_cast<std::size_t>(f->index)] = v;
}
MsValue apiNewArray(MsVM* vm, int32_t kind, int64_t n) {
  VMImpl& m = I(vm);
  if (kind < 0 || kind > 4 || n < 0 || n > (1 << 27) || !m.heapRoom(static_cast<std::size_t>(n) * sizeof(Value))) {
    m.raise(m.clsArgument, "invalid array request");
    return makeNull();
  }
  return vObj(m.newArr(static_cast<ElemKind>(kind), static_cast<std::size_t>(n)));
}
int64_t apiArrayLength(MsVM*, MsValue a) { return isKind(a, ObjKind::Array) ? static_cast<int64_t>(arrOf(a)->items.size()) : -1; }
MsValue apiArrayGet(MsVM* vm, MsValue a, int64_t i) {
  if (!isKind(a, ObjKind::Array) || i < 0 || i >= static_cast<int64_t>(arrOf(a)->items.size())) { I(vm).raise(I(vm).clsIndex, "array index out of range"); return makeNull(); }
  return arrOf(a)->items[static_cast<std::size_t>(i)];
}
void apiArraySet(MsVM* vm, MsValue a, int64_t i, MsValue v) {
  if (!isKind(a, ObjKind::Array) || i < 0 || i >= static_cast<int64_t>(arrOf(a)->items.size())) { I(vm).raise(I(vm).clsIndex, "array index out of range"); return; }
  arrOf(a)->items[static_cast<std::size_t>(i)] = v;
}
uint64_t apiPin(MsVM* vm, MsValue v) { return static_cast<VM*>(vm)->pin(v); }
void apiUnpin(MsVM* vm, uint64_t id) { static_cast<VM*>(vm)->unpin(id); }
MsValue apiPinned(MsVM* vm, uint64_t id) { return static_cast<VM*>(vm)->pinned(id); }
int32_t apiCallClosure(MsVM* vm, MsValue closure, const MsValue* args, int32_t argc, MsValue* result) {
  VMImpl& m = I(vm);
  if (!isKind(closure, ObjKind::Closure) || argc < 0) { m.raise(m.clsArgument, "not a function value"); return 1; }
  ClosureObj* c = cloOf(closure);
  std::size_t n = 0;
  if (m.sp + static_cast<std::size_t>(argc) + 2 >= m.stackCap) { m.raise(m.clsStackOverflow, "stack overflow"); return 1; }
  if (c->bound) m.stack[m.sp + n++] = c->self;
  for (int32_t i = 0; i < argc; ++i) m.stack[m.sp + n++] = args[i];
  const Status st = m.invoke(c->funcId, static_cast<std::uint32_t>(n), c);
  if (st == Status::Threw) return 1;
  if (result != nullptr) *result = m.retVal;
  return 0;
}
void apiLog(MsVM* vm, int32_t level, const char* msg) {
  VMImpl& m = I(vm);
  if (m.opt.log) m.opt.log(level, msg != nullptr ? msg : "");
}

const MsHostApi kHostApi = {
    MS_ABI_VERSION, sizeof(MsHostApi), apiNewString, apiStringData, apiThrow, apiSetNativeData, apiGetNativeData, apiNewObject,
    apiGetField,    apiSetField,       apiNewArray,   apiArrayLength, apiArrayGet, apiArraySet,  apiPin,          apiUnpin,
    apiPinned,      apiCallClosure,    apiLog,
};

}  // namespace

const MsHostApi* hostApi() noexcept { return &kHostApi; }

}  // namespace mingol::script
