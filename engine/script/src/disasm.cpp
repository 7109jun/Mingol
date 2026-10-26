// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#include "mingol/script/program.hpp"

namespace mingol::script {

namespace {
constexpr const char* kOpNames[] = {
  "Nop",
  "PushNull",
  "PushTrue",
  "PushFalse",
  "PushInt",
  "PushConst",
  "Pop",
  "Dup",
  "Dup2",
  "LoadLocal",
  "StoreLocal",
  "LoadBoxed",
  "StoreBoxed",
  "BoxLocal",
  "LoadCapture",
  "LoadCaptureBoxed",
  "StoreCaptureBoxed",
  "LoadField",
  "StoreField",
  "LoadStatic",
  "StoreStatic",
  "NewObject",
  "NewArray",
  "ArrayLit",
  "LoadIndex",
  "StoreIndex",
  "ArrayLen",
  "StrLen",
  "StrIndex",
  "StrConcat",
  "AddI",
  "SubI",
  "MulI",
  "DivI",
  "ModI",
  "NegI",
  "AddF",
  "SubF",
  "MulF",
  "DivF",
  "ModF",
  "NegF",
  "BitAnd",
  "BitOr",
  "BitXor",
  "BitNot",
  "Shl",
  "Shr",
  "Not",
  "EqI",
  "NeI",
  "LtI",
  "LeI",
  "GtI",
  "GeI",
  "EqF",
  "NeF",
  "LtF",
  "LeF",
  "GtF",
  "GeF",
  "EqRef",
  "NeRef",
  "EqStr",
  "NeStr",
  "LtStr",
  "LeStr",
  "GtStr",
  "GeStr",
  "I2F",
  "F2I",
  "ToStr",
  "EnumFromInt",
  "Jmp",
  "JmpIfFalse",
  "JmpIfTrue",
  "Call",
  "CallVirtual",
  "CallInterface",
  "CallClosure",
  "Return",
  "MakeClosure",
  "MakeBound",
  "MakeBoundVirtual",
  "IsType",
  "CastType",
  "Throw",
  "MakeRefBox",
  "MakeRefField",
  "MakeRefElem",
  "MakeRefStatic",
  "LoadRef",
  "StoreRef",
  "Yield",
  "StartCoroutine",
  "StartCoroutineVirtual",
  "TypeOfStatic",
  "TypeOfValue"
};
static_assert(sizeof(kOpNames) / sizeof(kOpNames[0]) == static_cast<std::size_t>(Op::TypeOfValue) + 1, "op name table out of date");
}  // namespace

const char* opName(Op op) noexcept {
  const auto i = static_cast<std::size_t>(op);
  return i < sizeof(kOpNames) / sizeof(kOpNames[0]) ? kOpNames[i] : "?";
}

std::string disassemble(const Program& p, int funcId) {
  const FuncProto& f = p.funcs.at(static_cast<std::size_t>(funcId));
  std::string out = f.name + " (params=" + std::to_string(f.numParams) + " locals=" + std::to_string(f.numLocals) +
                    " captures=" + std::to_string(f.numCaptures) + " maxStack=" + std::to_string(f.maxStack) + ")\n";
  for (std::size_t i = 0; i < f.code.size(); ++i) {
    const Instr& in = f.code[i];
    out += "  " + std::to_string(i) + ": " + opName(in.op) + " " + std::to_string(in.a) + " " + std::to_string(in.b) + "\n";
  }
  for (const Handler& h : f.handlers) {
    out += "  handler [" + std::to_string(h.startPc) + "," + std::to_string(h.endPc) + ") -> " + std::to_string(h.handlerPc) +
           " class=" + std::to_string(h.classId) + " slot=" + std::to_string(h.slot) + "\n";
  }
  return out;
}

}  // namespace mingol::script
