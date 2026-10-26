// SPDX-License-Identifier: GPL-2.0-only
// The compiled, immutable result of compiling a set of sources: bytecode plus metadata.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mingol/script/symbols.hpp"

namespace mingol::script {

enum class Op : std::uint8_t {
  Nop,
  // constants / stack
  PushNull, PushTrue, PushFalse, PushInt /*b*/, PushConst /*b*/, Pop, Dup, Dup2,
  // locals, captures
  LoadLocal /*a*/, StoreLocal /*a*/, LoadBoxed /*a*/, StoreBoxed /*a*/, BoxLocal /*a*/,
  LoadCapture /*a*/, LoadCaptureBoxed /*a*/, StoreCaptureBoxed /*a*/,
  // fields / statics
  LoadField /*a*/, StoreField /*a*/, LoadStatic /*b*/, StoreStatic /*b*/,
  // objects / arrays / strings
  NewObject /*b = class id*/, NewArray /*a = element kind*/, ArrayLit /*a = element kind, b = count*/,
  LoadIndex, StoreIndex, ArrayLen, StrLen, StrIndex, StrConcat,
  // arithmetic
  AddI, SubI, MulI, DivI, ModI, NegI, AddF, SubF, MulF, DivF, ModF, NegF,
  BitAnd, BitOr, BitXor, BitNot, Shl, Shr, Not,
  EqI, NeI, LtI, LeI, GtI, GeI, EqF, NeF, LtF, LeF, GtF, GeF,
  EqRef, NeRef, EqStr, NeStr, LtStr, LeStr, GtStr, GeStr,
  I2F, F2I, ToStr /*a = 0 int, 1 float, 2 bool, 3 enum (b = class id)*/, EnumFromInt /*b = class id*/,
  // control flow
  Jmp /*b = pc*/, JmpIfFalse /*b*/, JmpIfTrue /*b*/,
  Call /*b = func id, a = argc*/, CallVirtual /*b = vtable slot, a = argc incl. this*/,
  CallInterface /*b = key, a = argc incl. this*/, CallClosure /*a = argc*/, Return,
  MakeClosure /*b = func id, a = capture count*/, MakeBound /*b = func id; pops this*/, MakeBoundVirtual /*b = slot; pops this*/,
  // types
  IsType /*b = class id*/, CastType /*b = class id*/,
  Throw,
  // references (`ref` parameters and pointers)
  MakeRefBox /*pops a box*/, MakeRefField /*a*/, MakeRefElem, MakeRefStatic /*b*/, LoadRef, StoreRef,
  // coroutines
  Yield, StartCoroutine /*b = func id, a = argc*/, StartCoroutineVirtual /*b = vtable slot, a = argc incl. this*/,
  // runtime type objects
  TypeOfStatic /*b = index into Program::typeDescs*/, TypeOfValue,
};

const char* opName(Op op) noexcept;

// Element kinds for arrays (what the elements default to).
enum class ElemKind : std::uint8_t { Int = 0, Float = 1, Bool = 2, String = 3, Ref = 4 };

struct Instr {
  Op op = Op::Nop;
  std::uint8_t pad = 0;
  std::uint16_t a = 0;
  std::int32_t b = 0;
};
static_assert(sizeof(Instr) == 8);

struct Handler {
  std::uint32_t startPc = 0, endPc = 0;  // protected range [start, end)
  std::uint32_t handlerPc = 0;
  std::int32_t classId = -1;             // -1 catches everything
  std::uint16_t slot = 0;                // local that receives the exception
};

struct LineEntry {
  std::uint32_t pc;
  std::uint32_t line;
};

struct FuncProto {
  std::string name;       // e.g. "Player.update(float)" or "Player.update(float)$lambda0"
  int numParams = 0;      // including `this`
  int numLocals = 0;      // including params
  int numCaptures = 0;
  bool isCoroutine = false;
  bool isNative = false;
  bool isLambda = false;
  bool hasThis = false;
  std::string nativeName; // "Class.method" for native functions
  std::vector<Instr> code;
  std::vector<Handler> handlers;
  std::vector<LineEntry> lines;
  std::uint32_t file = 0;
  MethodSym* method = nullptr;
  int maxStack = 0;       // operand stack bound, computed by the code generator
};

struct Constant {
  enum class Kind : std::uint8_t { Int, Float, String } kind = Kind::Int;
  std::int64_t i = 0;
  double f = 0;
  std::string s;
};

class Program {
 public:
  Program() = default;
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  TypeTable types;
  std::vector<std::unique_ptr<CompilationUnit>> units;
  std::vector<std::string> files;
  std::vector<std::unique_ptr<ClassSym>> classes;
  std::vector<FuncProto> funcs;
  std::vector<Constant> constants;
  std::vector<std::unique_ptr<FuncInfo>> funcInfos;  // keeps variable symbols alive
  int staticCount = 0;
  std::vector<FieldSym*> staticFields;               // by static index
  std::vector<int> staticInitFuncs;                  // run in this order at load
  int interfaceKeyCount = 0;
  std::vector<const Type*> typeDescs;               // types referenced by typeof(...)

  // Well-known classes of the language prelude (never null once compiled with the prelude).
  ClassSym* objectClass = nullptr;
  ClassSym* exceptionClass = nullptr;
  ClassSym* coroutineClass = nullptr;
  ClassSym* typeClass = nullptr;

  ClassSym* findClass(const std::string& qualifiedName) const;
  const MethodSym* findMethod(const std::string& qualifiedClass, const std::string& name) const;
};


// Human-readable listing of a function's bytecode (tests and debugging).
std::string disassemble(const Program& p, int funcId);

}  // namespace mingol::script
