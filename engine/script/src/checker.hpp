// SPDX-License-Identifier: GPL-2.0-only
// The semantic checker: resolves names, checks types and annotates the syntax tree.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "compiler_internal.hpp"

namespace mingol::script {

class Checker {
 public:
  Checker(Compiler& c) : c_(c), p_(c.p_), d_(c.d_) {}
  void checkClass(ClassSym& cls);

 private:
  Compiler& c_;
  Program& p_;
  Diagnostics& d_;

  struct FnState {
    FuncInfo* info = nullptr;
    FnState* parent = nullptr;
    std::vector<std::map<std::string, VarSym*>> scopes;
    int loopDepth = 0;
    int switchDepth = 0;
    std::vector<const Type*> returnTypes;  // lambdas with an inferred return type
    bool sawReturnValue = false;
    bool isCtor = false;
    bool isFieldInit = false;
    int finallyLoop = -1, finallySwitch = -1;  // loop/switch depth at the innermost enclosing finally (-1: not in one)
    bool inFinally = false;
    std::vector<bool> loopHasBreak;        // per enclosing loop/switch: a `break` targets it
  };

  Expr* ctorChain_ = nullptr;  // the super(...)/this(...) call allowed in the constructor being checked
  ClassSym* cls_ = nullptr;
  UnitInfo* unit_ = nullptr;
  FnState* fn_ = nullptr;

  void error(SourceLoc l, std::string m) { d_.error(l, std::move(m)); }
  const Type* intT() const { return p_.types.intType(); }
  const Type* floatT() const { return p_.types.floatType(); }
  const Type* boolT() const { return p_.types.boolType(); }
  const Type* stringT() const { return p_.types.stringType(); }
  const Type* voidT() const { return p_.types.voidType(); }
  const Type* nullT() const { return p_.types.nullType(); }

  // ---- function bodies ----
  FuncInfo* newFunc(FuncInfo* parent);
  void checkMethod(MethodSym& m);
  void checkConstructor(MethodSym& m);
  void checkFieldInits(ClassSym& c, bool isStatic);
  void beginFn(FnState& st, FuncInfo* info, FnState* parent);
  void endFn();
  VarSym* declareVar(const std::string& name, const Type* type, SourceLoc loc, bool isParam = false);
  VarSym* declareThis();
  void pushScope() { fn_->scopes.emplace_back(); }
  void popScope() { fn_->scopes.pop_back(); }

  // ---- lookup ----
  VarSym* lookupVar(const std::string& name, SourceLoc loc);
  bool isAccessible(Access a, const ClassSym* owner) const;
  void requireAccess(Access a, const ClassSym* owner, SourceLoc loc, const std::string& what);
  void collectMethods(ClassSym* c, const std::string& name, std::vector<MethodSym*>& out);
  void collectOperators(ClassSym* c, Tok op, std::vector<MethodSym*>& out);

  // ---- overload resolution ----
  struct Arg {
    Expr* e = nullptr;
    const Type* type = nullptr;  // null while deferred (lambda / empty array literal)
    bool deferred = false;
  };
  std::vector<Arg> prepareArgs(std::vector<ExprPtr>& kids, std::size_t from);
  MethodSym* pickOverload(const std::vector<MethodSym*>& cands, std::vector<Arg>& args, SourceLoc loc, const std::string& what);
  bool applicable(const MethodSym& m, const std::vector<Arg>& args, int& cost);
  void finishArgs(const MethodSym& m, std::vector<Arg>& args);
  void finishClosureArgs(const Type* fnType, std::vector<Arg>& args, SourceLoc loc);
  void markAddressTaken(Expr& lvalue);
  bool isLValue(const Expr& e) const;
  bool isRefable(const Expr& e) const;

  // ---- statements ----
  bool stmt(Stmt& s);  // returns true when the statement can complete normally
  bool block(Stmt& s, bool newScope = true);
  bool varDecl(Stmt& s);
  bool forEach(Stmt& s);
  bool switchStmt(Stmt& s);
  bool tryStmt(Stmt& s);
  bool returnStmt(Stmt& s);
  const Type* condition(Expr& e);

  // ---- expressions ----
  const Type* expr(Expr& e, const Type* expected = nullptr);
  const Type* exprNoVoid(Expr& e, const Type* expected = nullptr);
  const Type* nameExpr(Expr& e);
  const Type* memberExpr(Expr& e, const Type* expected);
  const Type* callExpr(Expr& e);
  const Type* newExpr(Expr& e);
  const Type* unaryExpr(Expr& e);
  const Type* binaryExpr(Expr& e);
  const Type* assignExpr(Expr& e);
  const Type* indexExpr(Expr& e);
  const Type* castExpr(Expr& e);
  const Type* lambdaExpr(Expr& e, const Type* expected);
  const Type* arrayLitExpr(Expr& e, const Type* expected);
  const Type* ternaryExpr(Expr& e, const Type* expected);
  const Type* userBinary(Expr& e, Tok op, const Type* a, const Type* b, bool& found);
  const Type* unify(const Type* a, const Type* b);
  void requireAssignable(const Type* from, const Type* to, SourceLoc loc, const char* what);
  ClassSym* typeNameOf(Expr& e);  // resolves an expression that names a type, or nullptr (no diagnostics)
  bool constLabel(Expr& e, const Type* subject, std::int64_t& iv, std::string& sv);
  const Type* resolve(const TypeRef& t, bool allowVoid = false) { return c_.resolveType(t, *unit_, allowVoid); }
  const Type* memberOfType(Expr& e, const Type* objType, bool isStaticAccess, ClassSym* typeCls, const Type* expected);
  const Type* tryMethodValue(Expr& e, ClassSym* c, const std::string& name, bool viaType, const Type* expected);
  void superCtorCall(Expr& call, MethodSym& ctor);
};

}  // namespace mingol::script
