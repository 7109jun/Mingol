// SPDX-License-Identifier: GPL-2.0-only
// VM internals shared by the vm_*.cpp files.
#pragma once

#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "mingol/core/assert.hpp"
#include "mingol/script/vm.hpp"

namespace mingol::script {

enum class ObjKind : std::uint8_t { Instance, String, Array, Closure, Box, Ref };

struct Obj {
  ObjKind kind;
  bool marked = false;
  std::uint32_t bytes = 0;  // accounted size
  Obj* next = nullptr;
  explicit Obj(ObjKind k) : kind(k) {}
};

struct NativeData {
  virtual ~NativeData() = default;
  virtual void trace(VMImpl&) {}
};

struct ClassRt;

struct StringObj : Obj {
  std::string s;
  StringObj() : Obj(ObjKind::String) {}
};
struct InstanceObj : Obj {
  const ClassRt* cls = nullptr;
  std::vector<Value> fields;
  std::unique_ptr<NativeData> native;
  InstanceObj() : Obj(ObjKind::Instance) {}
};
struct ArrayObj : Obj {
  ElemKind ek = ElemKind::Int;
  std::vector<Value> items;
  ArrayObj() : Obj(ObjKind::Array) {}
};
struct ClosureObj : Obj {
  int funcId = -1;
  bool bound = false;
  Value self{};
  std::vector<Value> captures;
  ClosureObj() : Obj(ObjKind::Closure) {}
};
struct BoxObj : Obj {
  Value v{};
  BoxObj() : Obj(ObjKind::Box) {}
};
struct RefObj : Obj {
  enum class RK : std::uint8_t { Box, Field, Elem, Static };
  RK rk = RK::Box;
  Obj* target = nullptr;
  std::int32_t index = 0;
  RefObj() : Obj(ObjKind::Ref) {}
};

struct ClassRt {
  const ClassSym* sym = nullptr;
  std::vector<int> vtable;          // func ids
  std::unordered_map<int, int> itable;  // interface method key -> func id
  std::vector<Value> defaults;      // instance field defaults
  InstanceObj* typeObj = nullptr;   // cached `Type` object (GC root)
};

struct CoroutineData : NativeData {
  int funcId = -1;
  std::vector<Value> locals;
  std::uint32_t pc = 0;
  bool done = false;
  bool running = false;
  int yieldKind = 0;
  Value yielded{};
  void trace(VMImpl& vm) override;
};

struct TypeData : NativeData { const Type* type = nullptr; };
struct FieldData : NativeData { const FieldSym* field = nullptr; };
struct MethodData : NativeData { const MethodSym* method = nullptr; };

struct Frame {
  const FuncProto* fn = nullptr;
  std::uint32_t pc = 0;
  std::uint32_t base = 0;
  std::uint32_t retSp = 0;
  ClosureObj* closure = nullptr;
  CoroutineData* co = nullptr;
};

inline Value vObj(Obj* o) noexcept { Value v{MS_TAG_OBJECT, 0, {0}}; v.u.p = o; return v; }
inline Obj* objOf(const Value& v) noexcept { return static_cast<Obj*>(v.u.p); }
inline bool isKind(const Value& v, ObjKind k) noexcept { return v.tag == MS_TAG_OBJECT && objOf(v)->kind == k; }
inline StringObj* strOf(const Value& v) noexcept { return static_cast<StringObj*>(v.u.p); }
inline InstanceObj* instOf(const Value& v) noexcept { return static_cast<InstanceObj*>(v.u.p); }
inline ArrayObj* arrOf(const Value& v) noexcept { return static_cast<ArrayObj*>(v.u.p); }
inline ClosureObj* cloOf(const Value& v) noexcept { return static_cast<ClosureObj*>(v.u.p); }
inline BoxObj* boxOf(const Value& v) noexcept { return static_cast<BoxObj*>(v.u.p); }
inline RefObj* refOf(const Value& v) noexcept { return static_cast<RefObj*>(v.u.p); }

enum class Status { Returned, Yielded, Threw };

struct VMImpl {
  VM& vm;
  const Program& prog;
  VMOptions opt;

  // stack / frames
  std::unique_ptr<Value[]> stack;
  std::size_t stackCap = 0;
  std::size_t sp = 0;  // used only between executions (natives, host calls)
  std::vector<Frame> frames;
  int nativeDepth = 0;

  // program-derived tables
  std::vector<ClassRt> classes;
  std::vector<Value> consts;
  std::vector<Value> statics;
  std::vector<MsNativeFn> natives;                 // by func id
  std::unordered_map<std::string, int> nativeIds;  // native name -> func id
  std::vector<std::string> pluginNames;

  // heap
  Obj* head = nullptr;
  std::size_t bytes = 0;
  std::size_t liveObjects = 0;
  std::size_t nextGc = 0;
  std::size_t collections = 0;
  bool inGc = false;
  std::vector<Obj*> gray;
  std::map<std::uint64_t, Value> pins;
  std::uint64_t nextPin = 1;
  Value emptyString{};
  std::unordered_map<const Type*, InstanceObj*> typeObjs;
  std::unordered_map<const FieldSym*, InstanceObj*> fieldObjs;
  std::unordered_map<const MethodSym*, InstanceObj*> methodObjs;

  // exception state
  bool hasPending = false;
  Value pending{};
  Value retVal{};
  std::vector<void*> pluginHandles;

  // well-known classes (ids)
  int clsNullRef = -1, clsIndex = -1, clsDivZero = -1, clsCast = -1, clsStackOverflow = -1, clsOutOfMemory = -1, clsArgument = -1, clsNative = -1;
  int clsIntBox = -1, clsFloatBox = -1, clsBoolBox = -1, clsStringBox = -1;
  int clsCoroutine = -1, clsType = -1, clsFieldInfo = -1, clsMethodInfo = -1;

  VMImpl(VM& v, const Program& p, VMOptions o);
  ~VMImpl();

  // ---- setup (vm_heap.cpp) ----
  void buildClasses();
  void buildConstants();
  void bindPreludeNatives();  // vm_natives.cpp

  // ---- heap (vm_heap.cpp) ----
  StringObj* newStr(std::string s);
  InstanceObj* newInstance(const ClassRt& c);
  ArrayObj* newArr(ElemKind ek, std::size_t n);
  ClosureObj* newClosure(int funcId, std::size_t ncap);
  BoxObj* newBox(Value v);
  RefObj* newRef(RefObj::RK rk, Obj* target, std::int32_t index);
  Value defaultFor(const Type* t) const;
  Value defaultFor(ElemKind ek) const;
  bool heapRoom(std::size_t extra) const { return bytes + extra <= opt.maxHeapBytes; }
  bool shouldCollect() const { return bytes >= nextGc; }
  void collect();
  void markValue(const Value& v);
  void markObj(Obj* o);
  void traceObj(Obj* o);
  void destroy(Obj* o);
  template <class T>
  T* track(T* o, std::size_t extra);

  // ---- errors (vm_exec.cpp) ----
  Value makeError(int classId, const std::string& message);
  void setPending(Value ex) { hasPending = true; pending = ex; }
  void raise(int classId, const std::string& message);          // sets pending
  void raiseNamed(const char* qualifiedClass, const std::string& message);
  std::string buildTrace(std::size_t maxLines = 24) const;
  void fillTrace(Value ex);
  bool unwind(std::size_t stopDepth);                           // true: handler found, execution continues
  ScriptException flatten(Value ex) const;

  // ---- execution (vm_exec.cpp) ----
  Status execute(std::size_t stopDepth);
  bool pushFrame(const FuncProto& fn, std::uint32_t base, std::uint32_t argc, std::uint32_t retSp, ClosureObj* closure);
  // Runs func with args already placed at stack[sp..sp+argc). On Returned: retVal; on Threw: pending.
  Status invoke(int funcId, std::uint32_t argc, ClosureObj* closure);
  Status callNative(const FuncProto& fn, int funcId, std::uint32_t argBase, std::uint32_t argc, Value& result);
  int resolveVirtual(const Value& receiver, int slot) const;
  Status resumeCo(InstanceObj* co);
  Value newCoroutine(int funcId, const Value* args, std::size_t argc);

  // ---- helpers ----
  std::string toStr(const Value& v, int mode, int classId) const;
  bool refEquals(const Value& a, const Value& b) const;
  Value typeObject(const Type* t);
  Value fieldObject(const FieldSym* f);
  Value methodObject(const MethodSym* m);
  const ClassRt* classRt(int id) const { return &classes[static_cast<std::size_t>(id)]; }
  int classIdByName(const char* qualified) const;
};

}  // namespace mingol::script
