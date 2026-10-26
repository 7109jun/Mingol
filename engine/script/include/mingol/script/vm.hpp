// SPDX-License-Identifier: GPL-2.0-only
// The Mingol Script virtual machine: executes a compiled Program.
//
// Threading: a VM is single-threaded; use one VM per thread and never share Values between VMs.
// Memory: precise mark-sweep garbage collector. Collection only happens at VM safepoints (calls, backward
// jumps, allocation sites) or on an explicit collectGarbage(), never inside a native function. A Value held by
// the host across VM calls must be pinned (VM::pin or Rooted).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/script/plugin_abi.h"
#include "mingol/script/program.hpp"

// Definition of the C ABI's opaque handle (see plugin_abi.h); mingol::script::VM derives from it.
struct MsVM {};

namespace mingol::script {

using Value = MsValue;

inline Value makeNull() noexcept { return Value{MS_TAG_NULL, 0, {0}}; }
inline Value makeInt(std::int64_t i) noexcept { Value v{MS_TAG_INT, 0, {0}}; v.u.i = i; return v; }
inline Value makeFloat(double f) noexcept { Value v{MS_TAG_FLOAT, 0, {0}}; v.u.f = f; return v; }
inline Value makeBool(bool b) noexcept { Value v{MS_TAG_BOOL, 0, {0}}; v.u.i = b ? 1 : 0; return v; }
inline bool isNull(const Value& v) noexcept { return v.tag == MS_TAG_NULL; }
inline bool isObject(const Value& v) noexcept { return v.tag == MS_TAG_OBJECT; }

// An uncaught script exception, flattened for the host.
struct ScriptException {
  std::string type;        // qualified class name, e.g. "mingol.NullReferenceError"
  std::string message;
  std::string stackTrace;  // "  at Class.method(sig) (file:line)" lines, innermost first
  std::string describe() const { return type + ": " + message + (stackTrace.empty() ? "" : "\n" + stackTrace); }
};

struct VMOptions {
  std::size_t maxStackValues = 1u << 18;       // operand/local stack, in 16-byte values
  std::size_t maxFrames = 8192;                // script call depth
  int maxNativeDepth = 64;                     // native -> script -> native re-entries
  std::size_t gcThresholdBytes = 8u << 20;     // first collection after this many allocated bytes
  std::size_t maxHeapBytes = 512u << 20;       // hard limit; larger allocations raise OutOfMemoryError
  std::function<void(int level, const std::string& message)> log;  // sink for api->log; default: ignore
};

struct HeapStats {
  std::size_t liveObjects = 0;
  std::size_t liveBytes = 0;
  std::size_t collections = 0;
};

struct VMImpl;

// The host function table handed to plugins; also usable by natives implemented inside the host process.
const MsHostApi* hostApi() noexcept;

class VM final : public MsVM {
 public:
  explicit VM(const Program& program, VMOptions options = {});
  ~VM();
  VM(const VM&) = delete;
  VM& operator=(const VM&) = delete;

  const Program& program() const noexcept;

  // ---- natives ----
  // Binds a native method by its native name ("mingol.Type.getName()", "game.Audio.play(string, float)").
  Result<void> bindNative(const std::string& name, MsNativeFn fn);
  // Native methods of the program that have no implementation yet.
  std::vector<std::string> unboundNatives() const;
  // Loads a plugin shared library and lets it register natives. See plugin_abi.h.
  Result<void> loadPlugin(const std::string& path);

  // ---- execution ----
  // Runs static initialisers. Call once before anything else. False: `ex` describes the exception.
  bool initialize(ScriptException& ex);
  // Calls a method. Instance methods dispatch virtually on `receiver`; for static methods and
  // constructors-by-hand `receiver` is ignored/required accordingly.
  bool call(const MethodSym& method, Value receiver, std::span<const Value> args, Value& result, ScriptException& ex);
  bool callClosure(Value closure, std::span<const Value> args, Value& result, ScriptException& ex);
  // Allocates an instance (fields at their defaults) and runs `ctor`.
  bool construct(const ClassSym& cls, const MethodSym& ctor, std::span<const Value> args, Value& result, ScriptException& ex);
  // Allocates an instance without running any constructor (used when migrating objects on hot reload).
  Value allocate(const ClassSym& cls);

  // ---- values ----
  Value newString(std::string_view text);
  bool isString(Value v) const;
  std::string_view stringValue(Value v) const;  // valid until the next collection
  const ClassSym* classOf(Value v) const;       // null for non-instances
  Value getField(Value object, const FieldSym& field) const;
  void setField(Value object, const FieldSym& field, Value value);
  Value getStatic(const FieldSym& field) const;
  void setStatic(const FieldSym& field, Value value);

  // ---- coroutines (objects returned by calling a `coroutine` method) ----
  bool isCoroutine(Value v) const;
  bool coroutineDone(Value co) const;
  // Runs the coroutine until its next yield or its end. False: it threw (and is now finished).
  bool resumeCoroutine(Value co, ScriptException& ex);
  // 0 none, 1 int, 2 float, 3 bool, 4 string, 5 object
  int coroutineYieldKind(Value co) const;
  Value coroutineYielded(Value co) const;

  // ---- memory ----
  std::uint64_t pin(Value v);
  void unpin(std::uint64_t id);
  Value pinned(std::uint64_t id) const;
  void collectGarbage();
  HeapStats heapStats() const;

  VMImpl& impl() noexcept { return *impl_; }
  const VMImpl& impl() const noexcept { return *impl_; }

 private:
  std::unique_ptr<VMImpl> impl_;
};

// Keeps a Value alive while in scope.
class Rooted {
 public:
  Rooted(VM& vm, Value v) : vm_(&vm), id_(vm.pin(v)) {}
  ~Rooted() { if (vm_ != nullptr) vm_->unpin(id_); }
  Rooted(const Rooted&) = delete;
  Rooted& operator=(const Rooted&) = delete;
  Rooted(Rooted&& o) noexcept : vm_(o.vm_), id_(o.id_) { o.vm_ = nullptr; }
  Value get() const { return vm_->pinned(id_); }

 private:
  VM* vm_;
  std::uint64_t id_;
};

}  // namespace mingol::script
