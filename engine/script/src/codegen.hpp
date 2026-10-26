// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <map>
#include <string>
#include <vector>

#include "compiler_internal.hpp"
#include "mingol/core/assert.hpp"

namespace mingol::script {

class CodeGen {
 public:
  explicit CodeGen(Compiler& c) : c_(c), p_(c.p_) {}
  void run();

 private:
  Compiler& c_;
  Program& p_;

  struct Range {
    std::uint32_t s, e;
  };
  struct TryCtx {
    Stmt* fin = nullptr;
    std::vector<Range> catchRanges, finRanges;
    std::uint32_t catchStart = 0, finStart = 0;
    bool catchOpen = false, finOpen = false;
    bool catchWasOpen = false, finWasOpen = false;
  };
  struct LoopCtx {
    std::vector<std::size_t> breaks, continues;
    int tryDepth = 0;
    bool isSwitch = false;
  };
  struct Fn {
    FuncInfo* info = nullptr;
    int funcId = -1;
    std::vector<Instr> code;
    std::vector<Handler> handlers;
    std::vector<LineEntry> lines;
    int nextSlot = 0, maxSlot = 0;
    int depth = 0, maxDepth = 0;
    std::vector<LoopCtx> loops;
    std::vector<TryCtx> tries;
    std::uint32_t lastLine = 0;
    int lambdaCount = 0;
    std::string name;
  };
  Fn* g_ = nullptr;

  std::map<std::string, int> strConsts_;
  std::map<std::int64_t, int> intConsts_;
  std::map<std::uint64_t, int> floatConsts_;

  // ---- emission ----
  std::size_t emit(Op op, int a = 0, int b = 0);
  std::size_t here() const { return g_->code.size(); }
  void patch(std::size_t at, std::size_t target) { g_->code[at].b = static_cast<std::int32_t>(target); }
  std::size_t jump(Op op) { return emit(op, 0, -1); }
  void line(SourceLoc l);
  int allocSlot();
  int constInt(std::int64_t v);
  int constFloat(double v);
  int constString(const std::string& s);
  int typeDesc(const Type* t);
  void pushInt(std::int64_t v);
  void pushDefault(const Type* t);
  void coerce(const Type* from, const Type* to);
  ElemKind elemKind(const Type* t) const;
  void toStr(const Type* t);

  // ---- functions ----
  void genMethod(MethodSym& m);
  void genConstructor(MethodSym& m);
  void genFieldInit(ClassSym& c, bool isStatic);
  int genLambdaBody(FuncInfo& li, Expr& lambda, const std::string& name);
  void beginFn(Fn& f, FuncInfo* info, int funcId, std::string name);
  void endFn(Fn& f, int numParams);
  void assignParamSlots(FuncInfo& info);
  void boxParams(FuncInfo& info);
  void callFieldInit(ClassSym& c);
  VarSym* findThis() const;
  std::int64_t labelValue(const Expr& l) const;
  void loadThis() { loadVar(findThis()); }

  // ---- variables ----
  int captureIndex(VarSym* v) const;
  void loadRaw(VarSym* v);
  void loadVar(VarSym* v);
  void storeVar(VarSym* v);
  void declareLocal(VarSym* v);  // assigns a slot
  void initLocalFromStack(VarSym* v);

  // ---- statements ----
  void stmt(Stmt& s);
  void block(Stmt& s);
  void genVarDecl(Stmt& s);
  void genIf(Stmt& s);
  void genWhile(Stmt& s);
  void genDoWhile(Stmt& s);
  void genFor(Stmt& s);
  void genForEach(Stmt& s);
  void genSwitch(Stmt& s);
  void genTry(Stmt& s);
  void genReturn(Stmt& s);
  void genBreakContinue(bool isBreak);
  void exitThrough(int targetDepth);
  void reopenSegments(int targetDepth);
  void closeSegments(TryCtx& t);
  void reopen(TryCtx& t);

  // ---- expressions ----
  void expr(Expr& e);
  void exprConv(Expr& e, const Type* to);
  void discard(Expr& e);
  void genName(Expr& e);
  void genMember(Expr& e);
  void genCall(Expr& e);
  void genNew(Expr& e);
  void genUnary(Expr& e);
  void genBinary(Expr& e);
  void genAssign(Expr& e, bool wantValue);
  void genIncDec(Expr& e, bool wantValue);
  void genIndex(Expr& e);
  void genCast(Expr& e);
  void genLambda(Expr& e);
  void genArgs(const std::vector<ParamInfo>& params, Expr& call, std::size_t from);
  void genRef(Expr& e);
  void genReceiver(Expr& callee);
  void binaryOp(Tok op, const Type* common);
  void genUserCall(MethodSym& m);

  // lvalues
  enum class LK { Var, VarRef, Field, Static, Elem, Deref, OpIndex };
  LK lvKind(Expr& t) const;
  void lvPrefix(Expr& t, LK k);
  void lvDup(LK k);
  void lvLoad(Expr& t, LK k);
  void lvStore(Expr& t, LK k);
};

}  // namespace mingol::script
