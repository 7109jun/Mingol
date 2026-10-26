// SPDX-License-Identifier: GPL-2.0-only
// Checker: function setup, name lookup, overload resolution, and the compile driver.
#include <algorithm>

#include "checker.hpp"

namespace mingol::script {

// ---- driver -------------------------------------------------------------------------------------

bool Compiler::compile(std::vector<std::unique_ptr<CompilationUnit>> units) {
  units_.reserve(units.size());
  for (auto& u : units) {
    UnitInfo ui;
    ui.unit = u.get();
    for (std::size_t i = 0; i < u->moduleName.size(); ++i) ui.module += (i ? "." : "") + u->moduleName[i];
    units_.push_back(std::move(ui));
  }
  for (UnitInfo& ui : units_) unitByPtr_[ui.unit] = &ui;
  p_.units = std::move(units);

  collectTypes();
  bindWellKnown();
  if (p_.objectClass == nullptr || p_.exceptionClass == nullptr || p_.coroutineClass == nullptr || p_.typeClass == nullptr) {
    error({}, "the language prelude (module 'mingol') is missing required classes");
    return false;
  }
  checkModules();
  if (d_.hasErrors()) return false;
  resolveAllHeaders();
  if (d_.hasErrors()) return false;
  assignFuncIds();
  checkAllBodies();
  return !d_.hasErrors();
}

void Compiler::checkAllBodies() {
  Checker ch(*this);
  for (auto& c : p_.classes) ch.checkClass(*c);
}

// ---- functions ----------------------------------------------------------------------------------

FuncInfo* Checker::newFunc(FuncInfo* parent) {
  p_.funcInfos.push_back(std::make_unique<FuncInfo>());
  FuncInfo* f = p_.funcInfos.back().get();
  f->parent = parent;
  return f;
}

void Checker::beginFn(FnState& st, FuncInfo* info, FnState* parent) {
  st.info = info;
  st.parent = parent;
  st.scopes.emplace_back();
  fn_ = &st;
}

void Checker::endFn() { fn_ = fn_->parent; }

VarSym* Checker::declareVar(const std::string& name, const Type* type, SourceLoc loc, bool isParam) {
  for (const auto& sc : fn_->scopes) {
    if (sc.count(name) != 0) {
      error(loc, "'" + name + "' is already declared in this function");
      break;
    }
  }
  auto v = std::make_unique<VarSym>();
  v->name = name;
  v->type = type;
  v->loc = loc;
  v->isParam = isParam;
  v->owner = fn_->info;
  VarSym* raw = v.get();
  fn_->info->vars.push_back(std::move(v));
  fn_->scopes.back()[name] = raw;
  return raw;
}

VarSym* Checker::declareThis() {
  auto v = std::make_unique<VarSym>();
  v->name = "this";
  v->type = cls_->thisType;
  v->isParam = true;
  v->owner = fn_->info;
  VarSym* raw = v.get();
  fn_->info->vars.push_back(std::move(v));
  fn_->scopes.back()["this"] = raw;
  return raw;
}

VarSym* Checker::lookupVar(const std::string& name, SourceLoc) {
  for (FnState* f = fn_; f != nullptr; f = f->parent) {
    for (auto it = f->scopes.rbegin(); it != f->scopes.rend(); ++it) {
      auto jt = it->find(name);
      if (jt == it->end()) continue;
      VarSym* v = jt->second;
      if (f != fn_) {
        for (FnState* g = fn_; g != f; g = g->parent) {
          auto& caps = g->info->captures;
          if (std::find(caps.begin(), caps.end(), v) == caps.end()) caps.push_back(v);
        }
        v->captured = true;
        if (v->initializing) v->reassigned = true;
      }
      return v;
    }
  }
  return nullptr;
}

void Checker::checkClass(ClassSym& cls) {
  if (cls.kind != TypeDeclKind::Class) return;
  cls_ = &cls;
  unit_ = c_.unitByPtr_[cls.unit];

  checkFieldInits(cls, true);
  checkFieldInits(cls, false);

  for (auto& m : cls.methods) {
    if (m->mkind == MethodKind::Constructor) {
      checkConstructor(*m);
    } else if (m->decl != nullptr && m->decl->body) {
      checkMethod(*m);
    }
  }
}

void Checker::checkFieldInits(ClassSym& c, bool isStatic) {
  bool any = false;
  for (auto& f : c.fields) {
    if (f->isStatic == isStatic && f->decl != nullptr && f->decl->init) any = true;
  }
  if (!any) return;
  FuncInfo* info = newFunc(nullptr);
  info->cls = &c;
  info->isStatic = isStatic;
  info->ret = voidT();
  (isStatic ? c.staticInit : c.fieldInit) = info;
  FnState st;
  beginFn(st, info, nullptr);
  st.isFieldInit = true;
  if (!isStatic) declareThis();
  for (auto& f : c.fields) {
    if (f->isStatic != isStatic || f->decl == nullptr || !f->decl->init) continue;
    Expr& init = *f->decl->init;
    const Type* t = expr(init, f->type);
    if (t != nullptr) requireAssignable(t, f->type, init.loc, "field initialiser");
  }
  endFn();
}

void Checker::checkMethod(MethodSym& m) {
  FuncInfo* info = newFunc(nullptr);
  info->method = &m;
  info->cls = cls_;
  info->isStatic = m.mods.isStatic;
  info->isCoroutine = m.mods.isCoroutine;
  info->ret = m.ret;
  info->funcId = m.funcId;
  p_.funcs[static_cast<std::size_t>(m.funcId)].method = &m;
  m.decl->sym = &m;
  m.info = info;
  FnState st;
  beginFn(st, info, nullptr);
  if (!m.mods.isStatic && m.mkind != MethodKind::Operator) declareThis();
  for (const ParamInfo& pi : m.params) {
    VarSym* v = declareVar(pi.name, pi.type, m.loc, true);
    v->isRefParam = pi.isRef;
  }
  if (m.mods.isCoroutine && m.ret->kind != TypeKind::Void) error(m.loc, "a coroutine must return void");
  const bool completes = stmt(*m.decl->body);
  if (completes && m.ret->kind != TypeKind::Void && !m.mods.isCoroutine) error(m.decl->body->endLoc, "method '" + m.name + "' must return a value on every path");
  endFn();
}

void Checker::checkConstructor(MethodSym& m) {
  FuncInfo* info = newFunc(nullptr);
  info->method = &m;
  info->cls = cls_;
  info->ret = voidT();
  info->funcId = m.funcId;
  m.info = info;
  FnState st;
  beginFn(st, info, nullptr);
  st.isCtor = true;
  declareThis();

  if (m.decl == nullptr) {
    // Synthesised default constructor: needs an accessible no-argument base constructor.
    if (cls_->base != nullptr) {
      bool ok = false;
      for (auto& bm : cls_->base->methods) {
        if (bm->mkind == MethodKind::Constructor && bm->params.empty() && isAccessible(bm->mods.access, cls_->base)) ok = true;
      }
      if (!ok) error(cls_->loc, "class '" + cls_->name + "' needs a constructor: base class '" + cls_->base->name + "' has no accessible no-argument constructor");
    }
    endFn();
    return;
  }

  p_.funcs[static_cast<std::size_t>(m.funcId)].method = &m;
  for (const ParamInfo& pi : m.params) {
    VarSym* v = declareVar(pi.name, pi.type, m.loc, true);
    v->isRefParam = pi.isRef;
  }
  Stmt& body = *m.decl->body;
  bool chained = false;
  std::size_t first = 0;
  if (!body.stmts.empty() && body.stmts[0]->kind == StmtKind::ExprStmt) {
    Expr& e = *body.stmts[0]->exprs[0];
    if (e.kind == ExprKind::Call && (e.kids[0]->kind == ExprKind::Super || e.kids[0]->kind == ExprKind::This)) {
      chained = true;
      first = 0;
      ctorChain_ = &e;
    }
  }
  if (!chained && cls_->base != nullptr) {
    bool ok = false;
    for (auto& bm : cls_->base->methods) {
      if (bm->mkind == MethodKind::Constructor && bm->params.empty() && isAccessible(bm->mods.access, cls_->base)) ok = true;
    }
    if (!ok) error(m.loc, "constructor must start with super(...): base class '" + cls_->base->name + "' has no accessible no-argument constructor");
  }
  (void)first;
  stmt(body);
  ctorChain_ = nullptr;
  endFn();
}

// ---- access & lookup ------------------------------------------------------------------------------

bool Checker::isAccessible(Access a, const ClassSym* owner) const {
  switch (a) {
    case Access::Public: return true;
    case Access::Private: return cls_ == owner;
    case Access::Protected: return cls_ != nullptr && (cls_->isSubclassOf(owner));
  }
  return false;
}

void Checker::requireAccess(Access a, const ClassSym* owner, SourceLoc loc, const std::string& what) {
  if (!isAccessible(a, owner)) {
    error(loc, what + " is " + (a == Access::Private ? "private" : "protected") + " in '" + owner->name + "'");
  }
}

void Checker::collectMethods(ClassSym* c, const std::string& name, std::vector<MethodSym*>& out) {
  auto sameSig = [](const MethodSym& a, const MethodSym& b) {
    if (a.params.size() != b.params.size()) return false;
    for (std::size_t i = 0; i < a.params.size(); ++i) {
      if (a.params[i].type != b.params[i].type || a.params[i].isRef != b.params[i].isRef) return false;
    }
    return true;
  };
  auto add = [&](ClassSym* k) {
    for (auto& m : k->methods) {
      if (m->mkind != MethodKind::Method || m->name != name) continue;
      bool hidden = false;
      for (MethodSym* o : out) hidden = hidden || sameSig(*o, *m);
      if (!hidden) out.push_back(m.get());
    }
  };
  for (ClassSym* k = c; k != nullptr; k = k->base) add(k);
  if (c->kind == TypeDeclKind::Interface) {
    for (ClassSym* i : c->allInterfaces) add(i);
  }
}

void Checker::collectOperators(ClassSym* c, Tok op, std::vector<MethodSym*>& out) {
  for (ClassSym* k = c; k != nullptr; k = k->base) {
    for (auto& m : k->methods) {
      if (m->mkind == MethodKind::Operator && m->opToken == op && std::find(out.begin(), out.end(), m.get()) == out.end()) out.push_back(m.get());
    }
  }
}

// ---- overload resolution --------------------------------------------------------------------------

std::vector<Checker::Arg> Checker::prepareArgs(std::vector<ExprPtr>& kids, std::size_t from) {
  std::vector<Arg> args;
  for (std::size_t i = from; i < kids.size(); ++i) {
    Arg a;
    a.e = kids[i].get();
    if (a.e->kind == ExprKind::Lambda || (a.e->kind == ExprKind::ArrayLit && a.e->kids.empty())) {
      a.deferred = true;
    } else {
      a.type = exprNoVoid(*a.e);
    }
    args.push_back(a);
  }
  return args;
}

bool Checker::applicable(const MethodSym& m, const std::vector<Arg>& args, int& cost) {
  if (m.params.size() != args.size()) return false;
  cost = 0;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const ParamInfo& p = m.params[i];
    const Arg& a = args[i];
    if (a.deferred) {
      if (a.e->kind == ExprKind::Lambda) {
        if (p.type->kind != TypeKind::Func || p.type->params.size() != a.e->lambda->params.size()) return false;
      } else if (p.type->kind != TypeKind::Array) {
        return false;
      }
      continue;
    }
    if (a.type == nullptr) return false;
    if (p.isRef) {
      if (!isRefable(*a.e) || a.type != p.type) return false;
      continue;
    }
    if (a.type == p.type) continue;
    if (!c_.assignable(a.type, p.type)) return false;
    cost += (p.type->isClassLike() && p.type->cls == p_.objectClass) ? 2 : 1;
  }
  return true;
}

MethodSym* Checker::pickOverload(const std::vector<MethodSym*>& cands, std::vector<Arg>& args, SourceLoc loc, const std::string& what) {
  MethodSym* best = nullptr;
  int bestCost = 0;
  bool tie = false;
  for (MethodSym* m : cands) {
    int cost = 0;
    if (!applicable(*m, args, cost)) continue;
    if (best == nullptr || cost < bestCost) {
      best = m;
      bestCost = cost;
      tie = false;
    } else if (cost == bestCost) {
      tie = true;
    }
  }
  if (best == nullptr) {
    // An argument without a type already produced its own diagnostic; do not cascade.
    for (const Arg& a : args) if (a.type == nullptr && !a.deferred) return nullptr;
    std::string given;
    for (std::size_t i = 0; i < args.size(); ++i) given += (i ? ", " : "") + (args[i].deferred ? std::string("<lambda>") : args[i].type->name());
    std::string msg = "no overload of " + what + " accepts (" + given + ")";
    if (!cands.empty()) {
      msg += "; candidates:";
      for (MethodSym* m : cands) msg += " " + m->signature();
    }
    error(loc, msg);
    return nullptr;
  }
  if (tie) {
    error(loc, "call to " + what + " is ambiguous");
    return nullptr;
  }
  return best;
}

void Checker::finishArgs(const MethodSym& m, std::vector<Arg>& args) {
  for (std::size_t i = 0; i < args.size() && i < m.params.size(); ++i) {
    const ParamInfo& p = m.params[i];
    Arg& a = args[i];
    if (a.deferred) {
      a.type = expr(*a.e, p.type);
      if (a.type != nullptr) requireAssignable(a.type, p.type, a.e->loc, "argument");
    }
    if (p.isRef) markAddressTaken(*a.e);
  }
}

void Checker::finishClosureArgs(const Type* fnType, std::vector<Arg>& args, SourceLoc loc) {
  if (fnType->params.size() != args.size()) {
    error(loc, "function value of type '" + fnType->name() + "' takes " + std::to_string(fnType->params.size()) + " argument(s), " + std::to_string(args.size()) + " given");
    return;
  }
  for (std::size_t i = 0; i < args.size(); ++i) {
    const FuncParam& p = fnType->params[i];
    Arg& a = args[i];
    if (a.deferred) a.type = expr(*a.e, p.type);
    if (a.type == nullptr) continue;
    if (p.isRef) {
      if (!isRefable(*a.e) || a.type != p.type) error(a.e->loc, "argument " + std::to_string(i + 1) + " must be a variable of type '" + p.type->name() + "' (passed by reference)");
      else markAddressTaken(*a.e);
    } else {
      requireAssignable(a.type, p.type, a.e->loc, "argument");
    }
  }
}

void Checker::markAddressTaken(Expr& e) {
  if (e.kind == ExprKind::Name && e.sym != nullptr && e.sym->kind == SymKind::Var) {
    auto* v = static_cast<VarSym*>(e.sym);
    if (!v->isRefParam) v->addressTaken = true;
  }
}

bool Checker::isRefable(const Expr& e) const {
  if (!isLValue(e)) return false;
  if (e.kind == ExprKind::Index && e.aux == static_cast<int>(Res::OperatorIndex)) return false;
  if (e.sym != nullptr && e.sym->kind == SymKind::Field && static_cast<const FieldSym*>(e.sym)->isFinal) return false;
  return true;
}

bool Checker::isLValue(const Expr& e) const {
  switch (e.kind) {
    case ExprKind::Name: return e.aux == static_cast<int>(Res::Local) || e.aux == static_cast<int>(Res::Field) || e.aux == static_cast<int>(Res::StaticField);
    case ExprKind::Member: return e.aux == static_cast<int>(Res::Field) || e.aux == static_cast<int>(Res::StaticField);
    case ExprKind::Index: return e.aux == static_cast<int>(Res::ArrayIndex) || (e.aux == static_cast<int>(Res::OperatorIndex) && e.extra != nullptr);
    case ExprKind::Unary: return e.uop == UnaryOp::Deref;
    default: return false;
  }
}

void Checker::requireAssignable(const Type* from, const Type* to, SourceLoc loc, const char* what) {
  if (from == nullptr || to == nullptr) return;
  if (from->kind == TypeKind::Void) {
    error(loc, std::string(what) + " has no value (void)");
    return;
  }
  if (!c_.assignable(from, to)) {
    std::string hint;
    if (from->kind == TypeKind::Float && to->kind == TypeKind::Int) hint = " (use 'as int' to convert)";
    error(loc, std::string("cannot use '") + from->name() + "' as '" + to->name() + "' in " + what + hint);
  }
}

}  // namespace mingol::script
