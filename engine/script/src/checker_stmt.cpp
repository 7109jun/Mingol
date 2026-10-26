// SPDX-License-Identifier: GPL-2.0-only
// Checker: statements and control-flow analysis.
#include <algorithm>
#include <set>

#include "checker.hpp"

namespace mingol::script {
namespace {
constexpr int R(Res r) { return static_cast<int>(r); }
}  // namespace

const Type* Checker::condition(Expr& e) {
  const Type* t = exprNoVoid(e);
  if (t != nullptr && t->kind != TypeKind::Bool) {
    error(e.loc, "condition must be bool, not '" + t->name() + "'");
  }
  return t;
}

bool Checker::block(Stmt& s, bool newScope) {
  if (newScope) pushScope();
  bool completes = true;
  for (auto& st : s.stmts) {
    if (!completes) {
      error(st->loc, "unreachable code");
      completes = true;  // report once, then keep analysing
    }
    completes = stmt(*st);
  }
  if (newScope) popScope();
  return completes;
}

bool Checker::varDecl(Stmt& s) {
  const Type* declared = s.declType ? resolve(*s.declType) : nullptr;
  Expr* init = s.exprs.empty() ? nullptr : s.exprs[0].get();
  if (declared == nullptr && init == nullptr) {
    error(s.loc, "'var' needs an initialiser to infer the type");
    declared = intT();
  }
  VarSym* v = nullptr;
  if (declared != nullptr) {
    v = declareVar(s.name, declared, s.loc);
    v->initializing = true;
    if (init == nullptr) v->reassigned = true;  // assigned later, after possible capture
  }
  if (init != nullptr) {
    const Type* t = exprNoVoid(*init, declared);
    if (declared == nullptr) {
      declared = t;
      if (t != nullptr && t->kind == TypeKind::Null) {
        error(init->loc, "cannot infer a type from 'null'; declare the variable's type");
        declared = nullptr;
      }
      if (declared == nullptr) declared = intT();
      v = declareVar(s.name, declared, s.loc);
    } else if (t != nullptr) {
      requireAssignable(t, declared, init->loc, "initialiser");
    }
  }
  v->initializing = false;
  s.sym = v;
  s.type = declared;
  return true;
}

bool Checker::forEach(Stmt& s) {
  Expr& it = *s.exprs[0];
  const Type* t = exprNoVoid(it);
  const Type* elem = nullptr;
  if (t != nullptr) {
    if (t->kind == TypeKind::Array) elem = t->elem;
    else error(it.loc, "can only iterate over an array, not '" + t->name() + "'");
  }
  pushScope();
  const Type* declared = s.declType ? resolve(*s.declType) : nullptr;
  if (declared == nullptr) declared = elem != nullptr ? elem : intT();
  else if (elem != nullptr && !c_.assignable(elem, declared)) error(s.loc, "cannot use element type '" + elem->name() + "' as '" + declared->name() + "'");
  s.sym = declareVar(s.name, declared, s.loc);
  s.type = elem != nullptr ? elem : declared;
  ++fn_->loopDepth;
  fn_->loopHasBreak.push_back(false);
  stmt(*s.stmts[0]);
  fn_->loopHasBreak.pop_back();
  --fn_->loopDepth;
  popScope();
  return true;
}

bool Checker::constLabel(Expr& e, const Type* subject, std::int64_t& iv, std::string& sv) {
  if (subject->kind == TypeKind::Enum) {
    ClassSym* ec = subject->cls;
    std::string name;
    if (e.kind == ExprKind::Name) name = e.text;
    else if (e.kind == ExprKind::Member && e.kids[0]->kind == ExprKind::Name && e.kids[0]->text == ec->name) name = e.text;
    else if (e.kind == ExprKind::Member) {
      const Type* t = expr(e);
      if (t == subject && e.aux == R(Res::EnumConst)) { iv = static_cast<EnumConstSym*>(e.sym)->value; return true; }
    }
    for (auto& c : ec->enumConsts) {
      if (c->name == name) {
        iv = c->value;
        e.sym = c.get();
        e.aux = R(Res::Constant);
        e.type = subject;
        e.ival = iv;
        return true;
      }
    }
    error(e.loc, "case label must be a constant of enum '" + ec->name + "'");
    return false;
  }
  if (subject->kind == TypeKind::Int) {
    if (e.kind == ExprKind::IntLit) iv = e.ival;
    else if (e.kind == ExprKind::Unary && e.uop == UnaryOp::Neg && e.kids[0]->kind == ExprKind::IntLit) iv = static_cast<std::int64_t>(0 - static_cast<std::uint64_t>(e.kids[0]->ival));
    else { error(e.loc, "case label must be an integer literal"); return false; }
    e.type = intT();
    e.aux = R(Res::Constant);
    e.ival = iv;
    return true;
  }
  if (subject->kind == TypeKind::String) {
    if (e.kind != ExprKind::StringLit) { error(e.loc, "case label must be a string literal"); return false; }
    sv = e.text;
    e.type = stringT();
    e.aux = R(Res::Constant);
    return true;
  }
  return false;
}

bool Checker::switchStmt(Stmt& s) {
  const Type* t = exprNoVoid(*s.exprs[0]);
  bool ok = t != nullptr;
  if (ok && t->kind != TypeKind::Int && t->kind != TypeKind::String && t->kind != TypeKind::Enum) {
    error(s.exprs[0]->loc, "can only switch on int, string or enum, not '" + t->name() + "'");
    ok = false;
  }
  std::set<std::int64_t> seenInt;
  std::set<std::string> seenStr;
  bool hasDefault = false;
  bool anyCompletes = false;
  ++fn_->switchDepth;
  fn_->loopHasBreak.push_back(false);
  for (auto& c : s.cases) {
    hasDefault = hasDefault || c.isDefault;
    for (auto& l : c.labels) {
      if (!ok) { expr(*l); continue; }
      std::int64_t iv = 0;
      std::string sv;
      if (!constLabel(*l, t, iv, sv)) continue;
      const bool dup = t->kind == TypeKind::String ? !seenStr.insert(sv).second : !seenInt.insert(iv).second;
      if (dup) error(l->loc, "duplicate case label");
    }
    if (c.body.empty() && !c.isDefault && &c != &s.cases.back())
      error(c.loc, "this case has no statements and does not fall through; list several labels as 'case 2, 3:'");
    pushScope();
    bool completes = true;
    for (auto& st : c.body) {
      if (!completes) { error(st->loc, "unreachable code"); completes = true; }
      completes = stmt(*st);
    }
    popScope();
    // No fall-through: a case that can complete simply leaves the switch.
    anyCompletes = anyCompletes || completes;
  }
  const bool hasBreak = fn_->loopHasBreak.back();
  fn_->loopHasBreak.pop_back();
  --fn_->switchDepth;
  return !hasDefault || anyCompletes || hasBreak || s.cases.empty();
}

bool Checker::tryStmt(Stmt& s) {
  bool completes = stmt(*s.stmts[0]);
  bool tryCompletes = completes;
  for (auto& c : s.catches) {
    const Type* t = resolve(*c.type);
    pushScope();
    if (t != nullptr) {
      if (!t->isClassLike() || t->kind != TypeKind::Class || !t->cls->isSubclassOf(p_.exceptionClass)) {
        error(c.loc, "can only catch classes derived from 'Exception', not '" + t->name() + "'");
        t = c_.exceptionType();
      }
    } else {
      t = c_.exceptionType();
    }
    VarSym* v = declareVar(c.name, t, c.loc);
    c.sym = v;
    const bool cc = stmt(*c.body);
    tryCompletes = tryCompletes || cc;
    popScope();
  }
  if (s.stmts.size() > 1) {
    const bool saveF = fn_->inFinally;
    const int sl = fn_->finallyLoop, ss = fn_->finallySwitch;
    fn_->inFinally = true;
    fn_->finallyLoop = fn_->loopDepth;
    fn_->finallySwitch = fn_->switchDepth;
    const bool fc = stmt(*s.stmts[1]);
    fn_->inFinally = saveF;
    fn_->finallyLoop = sl;
    fn_->finallySwitch = ss;
    return tryCompletes && fc;
  }
  return tryCompletes;
}

bool Checker::returnStmt(Stmt& s) {
  if (fn_->inFinally) error(s.loc, "cannot 'return' from a finally block");
  FuncInfo* info = fn_->info;
  if (fn_->isFieldInit) { error(s.loc, "'return' is not allowed here"); return false; }
  const bool hasValue = !s.exprs.empty();
  if (fn_->isCtor || info->isCoroutine) {
    if (hasValue) error(s.loc, fn_->isCtor ? "a constructor cannot return a value" : "a coroutine cannot return a value");
    return false;
  }
  if (info->retInferred) {
    if (hasValue) {
      const Type* t = exprNoVoid(*s.exprs[0]);
      if (t != nullptr) fn_->returnTypes.push_back(t);
      fn_->sawReturnValue = true;
    } else {
      if (fn_->sawReturnValue || !fn_->returnTypes.empty()) error(s.loc, "lambda returns a value elsewhere; this 'return' needs one");
    }
    return false;
  }
  const Type* want = info->ret;
  if (want == nullptr || want->kind == TypeKind::Void) {
    if (hasValue) {
      error(s.exprs[0]->loc, "cannot return a value from a void function");
      expr(*s.exprs[0]);
    }
  } else if (!hasValue) {
    error(s.loc, "this function must return a value of type '" + want->name() + "'");
  } else {
    const Type* t = exprNoVoid(*s.exprs[0], want);
    if (t != nullptr) requireAssignable(t, want, s.exprs[0]->loc, "return value");
  }
  return false;
}

bool Checker::stmt(Stmt& s) {
  switch (s.kind) {
    case StmtKind::Block: return block(s);
    case StmtKind::VarDecl: return varDecl(s);
    case StmtKind::ExprStmt: {
      Expr& e = *s.exprs[0];
      const bool statementLike = e.kind == ExprKind::Assign || e.kind == ExprKind::Call || e.kind == ExprKind::New ||
                                 (e.kind == ExprKind::Unary && (e.uop == UnaryOp::PreInc || e.uop == UnaryOp::PreDec || e.uop == UnaryOp::PostInc || e.uop == UnaryOp::PostDec));
      const Type* t = expr(e);
      if (e.aux == R(Res::TypeName)) error(e.loc, "a type is not a statement");
      else if (!statementLike) error(e.loc, "this expression has no effect; it is not a statement");
      else if (e.kind == ExprKind::Call && e.aux == R(Res::CoroutineCall)) error(e.loc, "the coroutine is created but never started; pass it to a scheduler or store it");
      (void)t;
      return true;
    }
    case StmtKind::If: {
      condition(*s.exprs[0]);
      pushScope();
      const bool a = stmt(*s.stmts[0]);
      popScope();
      bool b = true;
      if (s.stmts.size() > 1) {
        pushScope();
        b = stmt(*s.stmts[1]);
        popScope();
      }
      return a || b;
    }
    case StmtKind::While: {
      condition(*s.exprs[0]);
      ++fn_->loopDepth;
      fn_->loopHasBreak.push_back(false);
      pushScope();
      stmt(*s.stmts[0]);
      popScope();
      const bool hasBreak = fn_->loopHasBreak.back();
      fn_->loopHasBreak.pop_back();
      --fn_->loopDepth;
      const bool infinite = s.exprs[0]->kind == ExprKind::BoolLit && s.exprs[0]->ival == 1;
      return !infinite || hasBreak;
    }
    case StmtKind::DoWhile: {
      ++fn_->loopDepth;
      fn_->loopHasBreak.push_back(false);
      pushScope();
      stmt(*s.stmts[0]);
      popScope();
      const bool hasBreak = fn_->loopHasBreak.back();
      fn_->loopHasBreak.pop_back();
      --fn_->loopDepth;
      condition(*s.exprs[0]);
      const bool infinite = s.exprs[0]->kind == ExprKind::BoolLit && s.exprs[0]->ival == 1;
      return !infinite || hasBreak;
    }
    case StmtKind::For: {
      pushScope();
      if (s.stmts[0]) stmt(*s.stmts[0]);
      if (s.exprs[0]) condition(*s.exprs[0]);
      if (s.exprs[1]) {
        Expr& step = *s.exprs[1];
        expr(step);
        const bool ok = step.kind == ExprKind::Assign || step.kind == ExprKind::Call || (step.kind == ExprKind::Unary && step.uop != UnaryOp::Neg && step.uop != UnaryOp::Not && step.uop != UnaryOp::BitNot && step.uop != UnaryOp::Plus && step.uop != UnaryOp::AddrOf && step.uop != UnaryOp::Deref);
        if (!ok) error(step.loc, "the for-loop step must be an assignment, call or ++/--");
      }
      ++fn_->loopDepth;
      fn_->loopHasBreak.push_back(false);
      pushScope();
      stmt(*s.stmts[1]);
      popScope();
      const bool hasBreak = fn_->loopHasBreak.back();
      fn_->loopHasBreak.pop_back();
      --fn_->loopDepth;
      popScope();
      const bool infinite = !s.exprs[0] || (s.exprs[0]->kind == ExprKind::BoolLit && s.exprs[0]->ival == 1);
      return !infinite || hasBreak;
    }
    case StmtKind::ForEach: return forEach(s);
    case StmtKind::Switch: return switchStmt(s);
    case StmtKind::Break: {
      if (fn_->loopDepth + fn_->switchDepth == 0) { error(s.loc, "'break' outside of a loop or switch"); return false; }
      if (fn_->inFinally && fn_->loopDepth + fn_->switchDepth <= fn_->finallyLoop + fn_->finallySwitch) error(s.loc, "cannot 'break' out of a finally block");
      if (!fn_->loopHasBreak.empty()) fn_->loopHasBreak.back() = true;
      return false;
    }
    case StmtKind::Continue: {
      if (fn_->loopDepth == 0) { error(s.loc, "'continue' outside of a loop"); return false; }
      if (fn_->inFinally && fn_->loopDepth <= fn_->finallyLoop) error(s.loc, "cannot 'continue' out of a finally block");
      return false;
    }
    case StmtKind::Return: return returnStmt(s);
    case StmtKind::Throw: {
      const Type* t = exprNoVoid(*s.exprs[0]);
      if (t != nullptr && (t->kind != TypeKind::Class || !t->cls->isSubclassOf(p_.exceptionClass))) {
        error(s.exprs[0]->loc, "can only throw classes derived from 'Exception', not '" + t->name() + "'");
      }
      return false;
    }
    case StmtKind::Try: return tryStmt(s);
    case StmtKind::Yield: {
      if (!fn_->info->isCoroutine) error(s.loc, "'yield' is only allowed directly inside a coroutine method");
      if (fn_->inFinally) error(s.loc, "cannot 'yield' inside a finally block");
      if (!s.exprs.empty()) exprNoVoid(*s.exprs[0]);
      return true;
    }
  }
  return true;
}

}  // namespace mingol::script
