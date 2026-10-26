// SPDX-License-Identifier: GPL-2.0-only
// Checker: expressions.
#include <algorithm>
#include <cmath>

#include "checker.hpp"

namespace mingol::script {
namespace {

constexpr int R(Res r) { return static_cast<int>(r); }

Tok baseOp(Tok compound) {
  switch (compound) {
    case Tok::PlusAssign: return Tok::Plus;
    case Tok::MinusAssign: return Tok::Minus;
    case Tok::StarAssign: return Tok::Star;
    case Tok::SlashAssign: return Tok::Slash;
    case Tok::PercentAssign: return Tok::Percent;
    case Tok::AmpAssign: return Tok::Amp;
    case Tok::PipeAssign: return Tok::Pipe;
    case Tok::CaretAssign: return Tok::Caret;
    case Tok::ShlAssign: return Tok::Shl;
    case Tok::ShrAssign: return Tok::Shr;
    default: return Tok::End;
  }
}

const char* opText(Tok t) {
  const char* n = tokName(t);
  return n;
}

}  // namespace

// ---- helpers ------------------------------------------------------------------------------------------

const Type* Checker::exprNoVoid(Expr& e, const Type* expected) {
  const Type* t = expr(e, expected);
  if (e.aux == R(Res::TypeName)) {
    error(e.loc, "'" + e.sym->name + "' is a type, not a value");
    return nullptr;
  }
  if (t != nullptr && t->kind == TypeKind::Void) {
    error(e.loc, "this expression has no value (it returns void)");
    return nullptr;
  }
  return t;
}

const Type* Checker::unify(const Type* a, const Type* b) {
  if (a == nullptr || b == nullptr) return nullptr;
  if (a == b) return a;
  if (a->kind == TypeKind::Null && b->isRefType()) return b;
  if (b->kind == TypeKind::Null && a->isRefType()) return a;
  if (a->isNumeric() && b->isNumeric()) return floatT();
  if (c_.assignable(a, b)) return b;
  if (c_.assignable(b, a)) return a;
  return nullptr;
}

ClassSym* Checker::typeNameOf(Expr& e) {
  auto nameIsValue = [&](const std::string& n) {
    for (FnState* f = fn_; f != nullptr; f = f->parent) {
      for (const auto& sc : f->scopes) {
        if (sc.count(n) != 0) return true;
      }
    }
    return cls_ != nullptr && cls_->findField(n) != nullptr;
  };
  if (e.kind == ExprKind::Name) {
    if (nameIsValue(e.text)) return nullptr;
    return c_.lookupSimple(e.text, *unit_, e.loc, false);
  }
  if (e.kind == ExprKind::Member) {
    std::vector<std::string> path;
    const Expr* cur = &e;
    while (cur->kind == ExprKind::Member) {
      path.push_back(cur->text);
      cur = cur->kids[0].get();
    }
    if (cur->kind != ExprKind::Name) return nullptr;
    path.push_back(cur->text);
    std::reverse(path.begin(), path.end());
    if (nameIsValue(path[0])) return nullptr;
    return c_.lookupClass(path, *unit_, e.loc, false);
  }
  return nullptr;
}

// ---- dispatcher -----------------------------------------------------------------------------------------

const Type* Checker::expr(Expr& e, const Type* expected) {
  const Type* t = nullptr;
  switch (e.kind) {
    case ExprKind::IntLit: t = intT(); break;
    case ExprKind::FloatLit: t = floatT(); break;
    case ExprKind::StringLit: t = stringT(); break;
    case ExprKind::BoolLit: t = boolT(); break;
    case ExprKind::NullLit: t = nullT(); break;
    case ExprKind::This: {
      VarSym* v = lookupVar("this", e.loc);
      if (v == nullptr) {
        error(e.loc, "'this' is not available in a static context");
        break;
      }
      e.sym = v;
      e.aux = R(Res::Local);
      t = v->type;
      break;
    }
    case ExprKind::Super: error(e.loc, "'super' can only be used as super.member or super(...)"); break;
    case ExprKind::Name: t = nameExpr(e); break;
    case ExprKind::Member: t = memberExpr(e, expected); break;
    case ExprKind::Index: t = indexExpr(e); break;
    case ExprKind::Call: t = callExpr(e); break;
    case ExprKind::New: t = newExpr(e); break;
    case ExprKind::NewArray: {
      const Type* elem = resolve(*e.typeRef);
      const Type* n = exprNoVoid(*e.kids[0]);
      if (n != nullptr && n->kind != TypeKind::Int) error(e.kids[0]->loc, "array size must be an int");
      t = p_.types.arrayOf(elem);
      break;
    }
    case ExprKind::ArrayLit: t = arrayLitExpr(e, expected); break;
    case ExprKind::Unary: t = unaryExpr(e); break;
    case ExprKind::Binary: t = binaryExpr(e); break;
    case ExprKind::Assign: t = assignExpr(e); break;
    case ExprKind::Ternary: t = ternaryExpr(e, expected); break;
    case ExprKind::Cast: t = castExpr(e); break;
    case ExprKind::Is: {
      const Type* s = exprNoVoid(*e.kids[0]);
      const Type* target = resolve(*e.typeRef);
      if (s != nullptr && target != nullptr) {
        if (!s->isClassLike() || !target->isClassLike()) {
          error(e.loc, "'is' needs class or interface types (got '" + s->name() + "' is '" + target->name() + "')");
        } else {
          const bool related = s->cls->isAssignableTo(target->cls) || target->cls->isAssignableTo(s->cls);
          const bool viaInterface = (s->kind == TypeKind::Interface || target->kind == TypeKind::Interface) && !(s->cls->mods.isFinal || target->cls->mods.isFinal);
          if (!related && !viaInterface) error(e.loc, "'" + s->name() + "' can never be a '" + target->name() + "'");
        }
        e.sym = target->isClassLike() ? target->cls : nullptr;
      }
      t = boolT();
      break;
    }
    case ExprKind::Typeof: {
      if (e.typeRef) {
        const Type* ty = resolve(*e.typeRef);
        e.extra = const_cast<Type*>(ty);
        e.aux = 0;
      } else {
        const Type* ty = expr(*e.kids[0]);
        if (e.kids[0]->aux == R(Res::TypeName)) {
          // typeof(Name): the operand names a type
          ClassSym* tc = static_cast<ClassSym*>(e.kids[0]->sym);
          ty = tc->thisType;
          e.kids.clear();
          e.aux = 0;
        } else {
          if (ty != nullptr && ty->kind == TypeKind::Void) { error(e.loc, "this expression has no value (it returns void)"); ty = nullptr; }
          e.aux = (ty != nullptr && ty->isClassLike()) ? 1 : 0;  // 1: runtime class of the value
        }
        e.extra = const_cast<Type*>(ty);
      }
      t = p_.typeClass->thisType;
      break;
    }
    case ExprKind::Lambda: t = lambdaExpr(e, expected); break;
  }
  e.type = t;
  return t;
}

// ---- names and members -----------------------------------------------------------------------------------

const Type* Checker::tryMethodValue(Expr& e, ClassSym* c, const std::string& name, bool viaType, const Type* expected) {
  std::vector<MethodSym*> ms;
  collectMethods(c, name, ms);
  if (ms.empty()) return nullptr;
  MethodSym* pick = nullptr;
  if (expected != nullptr && expected->kind == TypeKind::Func) {
    for (MethodSym* m : ms) {
      if (m->params.size() != expected->params.size() || m->ret != expected->ret) continue;
      bool same = true;
      for (std::size_t i = 0; i < m->params.size(); ++i) same = same && m->params[i].type == expected->params[i].type && m->params[i].isRef == expected->params[i].isRef;
      if (same) { pick = m; break; }
    }
  } else if (ms.size() == 1) {
    pick = ms[0];
  }
  if (pick == nullptr) {
    error(e.loc, "cannot use method '" + name + "' as a value here; its type is ambiguous or does not match");
    return nullptr;
  }
  if (pick->mods.isCoroutine) {
    error(e.loc, "a coroutine cannot be used as a function value");
    return nullptr;
  }
  requireAccess(pick->mods.access, pick->owner, e.loc, "method '" + name + "'");
  if (pick->owner->kind == TypeDeclKind::Interface) {
    error(e.loc, "cannot take a method value of an interface method");
    return nullptr;
  }
  if (!pick->mods.isStatic && viaType) {
    error(e.loc, "'" + name + "' is an instance method; use it on an object");
    return nullptr;
  }
  std::vector<FuncParam> ps;
  for (const ParamInfo& pi : pick->params) ps.push_back({pi.type, pi.isRef});
  e.sym = pick;
  e.aux = R(pick->mods.isStatic ? Res::StaticMethodValue : Res::MethodValue);
  return p_.types.func(std::move(ps), pick->ret);
}

const Type* Checker::nameExpr(Expr& e) {
  const std::string& n = e.text;
  if (VarSym* v = lookupVar(n, e.loc)) {
    e.sym = v;
    e.aux = R(Res::Local);
    return v->type;
  }
  if (cls_ != nullptr) {
    if (FieldSym* f = cls_->findField(n)) {
      requireAccess(f->access, f->owner, e.loc, "field '" + n + "'");
      e.sym = f;
      if (f->isStatic) {
        e.aux = R(Res::StaticField);
      } else {
        if (lookupVar("this", e.loc) == nullptr) {
          error(e.loc, "instance field '" + n + "' cannot be used from a static context");
          return nullptr;
        }
        e.aux = R(Res::Field);
      }
      return f->type;
    }
    std::vector<MethodSym*> ms;
    collectMethods(cls_, n, ms);
    if (!ms.empty()) {
      const Type* t = tryMethodValue(e, cls_, n, false, nullptr);
      if (t != nullptr && e.aux == R(Res::MethodValue) && lookupVar("this", e.loc) == nullptr) {
        error(e.loc, "instance method '" + n + "' cannot be used from a static context");
        return nullptr;
      }
      return t;
    }
  }
  if (ClassSym* k = c_.lookupSimple(n, *unit_, e.loc, false)) {
    e.sym = k;
    e.aux = R(Res::TypeName);
    return nullptr;
  }
  error(e.loc, "unknown name '" + n + "'");
  return nullptr;
}

const Type* Checker::memberExpr(Expr& e, const Type* expected) {
  Expr& obj = *e.kids[0];
  const std::string& name = e.text;

  if (obj.kind == ExprKind::Super) {
    if (cls_->base == nullptr) { error(e.loc, "this class has no base class"); return nullptr; }
    FieldSym* f = cls_->base->findField(name);
    if (f == nullptr || f->isStatic) { error(e.loc, "base class has no instance field '" + name + "'"); return nullptr; }
    requireAccess(f->access, f->owner, e.loc, "field '" + name + "'");
    if (lookupVar("this", e.loc) == nullptr) { error(e.loc, "'super' is not available in a static context"); return nullptr; }
    e.sym = f;
    e.aux = R(Res::Field);
    return f->type;
  }

  if (ClassSym* tc = typeNameOf(obj)) {
    obj.sym = tc;
    obj.aux = R(Res::TypeName);
    obj.type = nullptr;
    if (tc->kind == TypeDeclKind::Enum) {
      for (auto& ec : tc->enumConsts) {
        if (ec->name == name) {
          e.sym = ec.get();
          e.aux = R(Res::EnumConst);
          return tc->thisType;
        }
      }
      error(e.loc, "enum '" + tc->name + "' has no constant '" + name + "'");
      return nullptr;
    }
    if (FieldSym* f = tc->findField(name)) {
      if (!f->isStatic) { error(e.loc, "'" + name + "' is an instance field; use it on an object"); return nullptr; }
      requireAccess(f->access, f->owner, e.loc, "field '" + name + "'");
      e.sym = f;
      e.aux = R(Res::StaticField);
      return f->type;
    }
    if (const Type* mt = tryMethodValue(e, tc, name, true, expected)) return mt;
    if (d_.hasErrors() && e.sym == nullptr) return nullptr;
    error(e.loc, "'" + tc->name + "' has no static member '" + name + "'");
    return nullptr;
  }

  const Type* ot = exprNoVoid(obj);
  if (ot == nullptr) return nullptr;
  return memberOfType(e, ot, false, nullptr, expected);
}

const Type* Checker::memberOfType(Expr& e, const Type* ot, bool, ClassSym*, const Type* expected) {
  const std::string& name = e.text;
  if (ot->kind == TypeKind::Array && name == "length") {
    e.aux = R(Res::ArrayLength);
    return intT();
  }
  if (ot->kind == TypeKind::String && name == "length") {
    e.aux = R(Res::StringLength);
    return intT();
  }
  if (ot->kind == TypeKind::Class || ot->kind == TypeKind::Interface) {
    ClassSym* c = ot->cls;
    if (FieldSym* f = c->findField(name)) {
      if (f->isStatic) {
        error(e.loc, "'" + name + "' is static; access it as " + f->owner->name + "." + name);
        return nullptr;
      }
      requireAccess(f->access, f->owner, e.loc, "field '" + name + "'");
      e.sym = f;
      e.aux = R(Res::Field);
      return f->type;
    }
    if (const Type* mt = tryMethodValue(e, c, name, false, expected)) return mt;
    error(e.loc, "'" + ot->name() + "' has no member '" + name + "'");
    return nullptr;
  }
  error(e.loc, "type '" + ot->name() + "' has no member '" + name + "'");
  return nullptr;
}

// ---- calls ---------------------------------------------------------------------------------------------------

void Checker::superCtorCall(Expr& call, MethodSym& ctor) {
  call.sym = &ctor;
}

const Type* Checker::callExpr(Expr& e) {
  Expr& callee = *e.kids[0];

  // super(...) / this(...)
  if (callee.kind == ExprKind::Super || callee.kind == ExprKind::This) {
    if (&e != ctorChain_) {
      error(e.loc, std::string("'") + (callee.kind == ExprKind::Super ? "super" : "this") + "(...)' must be the first statement of a constructor");
      for (std::size_t i = 1; i < e.kids.size(); ++i) expr(*e.kids[i]);
      return nullptr;
    }
    ClassSym* target = callee.kind == ExprKind::Super ? cls_->base : cls_;
    if (target == nullptr) { error(e.loc, "this class has no base class"); return nullptr; }
    std::vector<MethodSym*> ctors;
    for (auto& m : target->methods) {
      if (m->mkind == MethodKind::Constructor) ctors.push_back(m.get());
    }
    auto args = prepareArgs(e.kids, 1);
    MethodSym* pick = pickOverload(ctors, args, e.loc, "constructor of '" + target->name + "'");
    if (pick == nullptr) return nullptr;
    if (callee.kind == ExprKind::Super) requireAccess(pick->mods.access, target, e.loc, "constructor");
    if (callee.kind == ExprKind::This && pick == fn_->info->method) error(e.loc, "a constructor cannot call itself");
    finishArgs(*pick, args);
    e.sym = pick;
    e.aux = R(callee.kind == ExprKind::Super ? Res::CtorSuper : Res::CtorThis);
    return voidT();
  }

  // super.method(...)
  if (callee.kind == ExprKind::Member && callee.kids[0]->kind == ExprKind::Super) {
    if (cls_->base == nullptr) { error(e.loc, "this class has no base class"); return nullptr; }
    if (lookupVar("this", e.loc) == nullptr) { error(e.loc, "'super' is not available in a static context"); return nullptr; }
    std::vector<MethodSym*> ms;
    collectMethods(cls_->base, callee.text, ms);
    auto args = prepareArgs(e.kids, 1);
    MethodSym* pick = pickOverload(ms, args, e.loc, "method 'super." + callee.text + "'");
    if (pick == nullptr) return nullptr;
    if (pick->isAbstract()) { error(e.loc, "cannot call abstract method '" + pick->signature() + "' through super"); return nullptr; }
    if (pick->mods.isStatic) { error(e.loc, "'" + callee.text + "' is static"); return nullptr; }
    requireAccess(pick->mods.access, pick->owner, e.loc, "method '" + callee.text + "'");
    finishArgs(*pick, args);
    e.sym = pick;
    e.aux = R(pick->mods.isCoroutine ? Res::CoroutineCall : Res::SuperCall);
    return pick->mods.isCoroutine ? p_.coroutineClass->thisType : pick->ret;
  }

  // Method call by simple name.
  if (callee.kind == ExprKind::Name) {
    if (VarSym* v = lookupVar(callee.text, callee.loc)) {
      callee.sym = v;
      callee.aux = R(Res::Local);
      callee.type = v->type;
      if (v->type->kind != TypeKind::Func) {
        error(e.loc, "'" + callee.text + "' is not a function (it is '" + v->type->name() + "')");
        return nullptr;
      }
      auto args = prepareArgs(e.kids, 1);
      finishClosureArgs(v->type, args, e.loc);
      e.aux = R(Res::ClosureCall);
      return v->type->ret;
    }
    std::vector<MethodSym*> ms;
    if (cls_ != nullptr) collectMethods(cls_, callee.text, ms);
    if (ms.empty()) {
      // A field holding a function value.
      if (cls_ != nullptr) {
        if (FieldSym* f = cls_->findField(callee.text)) {
          const Type* ft = nameExpr(callee);
          callee.type = ft;
          if (ft == nullptr) return nullptr;
          if (ft->kind != TypeKind::Func) { error(e.loc, "'" + callee.text + "' is not a function"); return nullptr; }
          (void)f;
          auto args = prepareArgs(e.kids, 1);
          finishClosureArgs(ft, args, e.loc);
          e.aux = R(Res::ClosureCall);
          return ft->ret;
        }
      }
      error(callee.loc, "unknown function '" + callee.text + "'");
      for (std::size_t i = 1; i < e.kids.size(); ++i) expr(*e.kids[i]);
      return nullptr;
    }
    auto args = prepareArgs(e.kids, 1);
    MethodSym* pick = pickOverload(ms, args, e.loc, "method '" + callee.text + "'");
    if (pick == nullptr) return nullptr;
    requireAccess(pick->mods.access, pick->owner, e.loc, "method '" + callee.text + "'");
    finishArgs(*pick, args);
    e.sym = pick;
    if (pick->mods.isStatic) {
      e.aux = R(pick->mods.isCoroutine ? Res::CoroutineCall : Res::StaticCall);
    } else {
      if (lookupVar("this", e.loc) == nullptr) {
        error(e.loc, "instance method '" + callee.text + "' cannot be called from a static context");
        return nullptr;
      }
      if (pick->mods.isCoroutine) e.aux = R(Res::CoroutineCall);
      else if (pick->vtableSlot >= 0) e.aux = R(Res::VirtualCall);
      else e.aux = R(Res::SuperCall);  // private: direct call
    }
    return pick->mods.isCoroutine ? p_.coroutineClass->thisType : pick->ret;
  }

  // obj.method(...) / Type.method(...) / obj.field(...)
  if (callee.kind == ExprKind::Member) {
    const std::string& name = callee.text;
    Expr& obj = *callee.kids[0];
    if (ClassSym* tc = typeNameOf(obj)) {
      obj.sym = tc;
      obj.aux = R(Res::TypeName);
      std::vector<MethodSym*> ms;
      collectMethods(tc, name, ms);
      auto args = prepareArgs(e.kids, 1);
      if (ms.empty()) {
        error(callee.loc, "'" + tc->name + "' has no method '" + name + "'");
        return nullptr;
      }
      MethodSym* pick = pickOverload(ms, args, e.loc, "method '" + tc->name + "." + name + "'");
      if (pick == nullptr) return nullptr;
      if (!pick->mods.isStatic) { error(e.loc, "'" + name + "' is an instance method; call it on an object"); return nullptr; }
      requireAccess(pick->mods.access, pick->owner, e.loc, "method '" + name + "'");
      finishArgs(*pick, args);
      e.sym = pick;
      e.aux = R(pick->mods.isCoroutine ? Res::CoroutineCall : Res::StaticCall);
      return pick->mods.isCoroutine ? p_.coroutineClass->thisType : pick->ret;
    }
    const Type* ot = exprNoVoid(obj);
    if (ot == nullptr) {
      for (std::size_t i = 1; i < e.kids.size(); ++i) expr(*e.kids[i]);
      return nullptr;
    }
    if (ot->isClassLike()) {
      std::vector<MethodSym*> ms;
      collectMethods(ot->cls, name, ms);
      if (!ms.empty()) {
        auto args = prepareArgs(e.kids, 1);
        MethodSym* pick = pickOverload(ms, args, e.loc, "method '" + name + "'");
        if (pick == nullptr) return nullptr;
        requireAccess(pick->mods.access, pick->owner, e.loc, "method '" + name + "'");
        if (pick->mods.isStatic) { error(e.loc, "'" + name + "' is static; call it as " + pick->owner->name + "." + name); return nullptr; }
        finishArgs(*pick, args);
        e.sym = pick;
        callee.sym = pick;
        if (pick->owner->kind == TypeDeclKind::Interface) {
          if (pick->mods.isCoroutine) { error(e.loc, "interface methods cannot be coroutines"); return nullptr; }
          e.aux = R(Res::InterfaceCall);
        } else if (pick->mods.isCoroutine) {
          e.aux = R(Res::CoroutineCall);
        } else if (pick->vtableSlot >= 0) {
          e.aux = R(Res::VirtualCall);
        } else {
          e.aux = R(Res::SuperCall);
        }
        // Calls through an interface-typed receiver where the picked method is declared in the interface.
        return pick->mods.isCoroutine ? p_.coroutineClass->thisType : pick->ret;
      }
      if (FieldSym* f = ot->cls->findField(name)) {
        if (!f->isStatic && f->type->kind == TypeKind::Func) {
          requireAccess(f->access, f->owner, e.loc, "field '" + name + "'");
          callee.sym = f;
          callee.aux = R(Res::Field);
          callee.type = f->type;
          auto args = prepareArgs(e.kids, 1);
          finishClosureArgs(f->type, args, e.loc);
          e.aux = R(Res::ClosureCall);
          return f->type->ret;
        }
      }
      error(callee.loc, "'" + ot->name() + "' has no method '" + name + "'");
      for (std::size_t i = 1; i < e.kids.size(); ++i) expr(*e.kids[i]);
      return nullptr;
    }
    error(callee.loc, "type '" + ot->name() + "' has no method '" + name + "'");
    for (std::size_t i = 1; i < e.kids.size(); ++i) expr(*e.kids[i]);
    return nullptr;
  }

  // Any other expression of function type: f(x)(y), (lambda)(x), arr[i](x) ...
  const Type* ct = exprNoVoid(callee);
  if (ct == nullptr) {
    for (std::size_t i = 1; i < e.kids.size(); ++i) expr(*e.kids[i]);
    return nullptr;
  }
  if (ct->kind != TypeKind::Func) {
    error(e.loc, "cannot call a value of type '" + ct->name() + "'");
    return nullptr;
  }
  auto args = prepareArgs(e.kids, 1);
  finishClosureArgs(ct, args, e.loc);
  e.aux = R(Res::ClosureCall);
  return ct->ret;
}

const Type* Checker::newExpr(Expr& e) {
  const Type* t = resolve(*e.typeRef);
  if (t == nullptr || t->kind != TypeKind::Class) {
    if (t != nullptr && t->kind == TypeKind::Interface) error(e.loc, "cannot instantiate interface '" + t->name() + "'");
    else if (t != nullptr && t->kind == TypeKind::Enum) error(e.loc, "cannot instantiate enum '" + t->name() + "'");
    else if (t != nullptr) error(e.loc, "cannot instantiate '" + t->name() + "'");
    for (auto& k : e.kids) expr(*k);
    return nullptr;
  }
  ClassSym* c = t->cls;
  if (c->mods.isAbstract) {
    error(e.loc, "cannot instantiate abstract class '" + c->name + "'");
    return nullptr;
  }
  std::vector<MethodSym*> ctors;
  for (auto& m : c->methods) {
    if (m->mkind == MethodKind::Constructor) ctors.push_back(m.get());
  }
  auto args = prepareArgs(e.kids, 0);
  MethodSym* pick = pickOverload(ctors, args, e.loc, "constructor of '" + c->name + "'");
  if (pick == nullptr) return nullptr;
  requireAccess(pick->mods.access, c, e.loc, "constructor of '" + c->name + "'");
  finishArgs(*pick, args);
  e.sym = pick;
  return t;
}

// ---- operators --------------------------------------------------------------------------------------------

const Type* Checker::userBinary(Expr& e, Tok op, const Type* a, const Type* b, bool& found) {
  found = false;
  std::vector<MethodSym*> cands;
  if (a->isClassLike()) collectOperators(a->cls, op, cands);
  if (b->isClassLike() && (!a->isClassLike() || b->cls != a->cls)) collectOperators(b->cls, op, cands);
  std::vector<MethodSym*> binary;
  for (MethodSym* m : cands) {
    if (m->params.size() == 2) binary.push_back(m);
  }
  if (binary.empty()) return nullptr;
  std::vector<Arg> args(2);
  args[0].e = e.kids[0].get();
  args[0].type = a;
  args[1].e = e.kids[1].get();
  args[1].type = b;
  // Applicability without a diagnostic: operators fall back to the built-in rules when none fits.
  MethodSym* best = nullptr;
  int bestCost = 0;
  bool tie = false;
  for (MethodSym* m : binary) {
    int cost = 0;
    if (!applicable(*m, args, cost)) continue;
    if (best == nullptr || cost < bestCost) { best = m; bestCost = cost; tie = false; }
    else if (cost == bestCost) tie = true;
  }
  if (best == nullptr) return nullptr;
  if (tie) {
    error(e.loc, std::string("operator ") + opText(op) + " is ambiguous for '" + a->name() + "' and '" + b->name() + "'");
    found = true;
    return nullptr;
  }
  found = true;
  e.sym = best;
  e.aux = R(Res::Operator);
  return best->ret;
}

const Type* Checker::unaryExpr(Expr& e) {
  Expr& operand = *e.kids[0];
  switch (e.uop) {
    case UnaryOp::AddrOf: {
      const Type* t = expr(operand);
      if (operand.aux == R(Res::TypeName)) { error(e.loc, "cannot take the address of a type"); return nullptr; }
      if (t == nullptr) return nullptr;
      if (!isRefable(operand)) { error(e.loc, "cannot take the address of this expression; it is not a variable, field or element"); return nullptr; }
      markAddressTaken(operand);
      return p_.types.pointerTo(t);
    }
    case UnaryOp::Deref: {
      const Type* t = exprNoVoid(operand);
      if (t == nullptr) return nullptr;
      if (t->kind != TypeKind::Pointer) { error(e.loc, "cannot dereference '" + t->name() + "' (not a pointer)"); return nullptr; }
      e.aux = R(Res::Deref);
      return t->elem;
    }
    case UnaryOp::PreInc: case UnaryOp::PreDec: case UnaryOp::PostInc: case UnaryOp::PostDec: {
      const Type* t = expr(operand);
      if (t == nullptr) return nullptr;
      if (!isLValue(operand)) { error(e.loc, "operand of ++/-- must be a variable, field or element"); return nullptr; }
      if (!t->isNumeric()) { error(e.loc, "++/-- needs an int or float, not '" + t->name() + "'"); return nullptr; }
      if (operand.sym != nullptr && operand.sym->kind == SymKind::Var) static_cast<VarSym*>(operand.sym)->reassigned = true;
      if (operand.sym != nullptr && operand.sym->kind == SymKind::Field && static_cast<FieldSym*>(operand.sym)->isFinal)
        error(e.loc, "cannot modify final field '" + operand.sym->name + "'");
      return t;
    }
    default: break;
  }
  const Type* t = exprNoVoid(operand);
  if (t == nullptr) return nullptr;
  Tok tok = e.uop == UnaryOp::Neg ? Tok::Minus : e.uop == UnaryOp::Not ? Tok::Bang : e.uop == UnaryOp::BitNot ? Tok::Tilde : Tok::Plus;
  if (t->isClassLike()) {
    std::vector<MethodSym*> cands, unary;
    collectOperators(t->cls, tok, cands);
    for (MethodSym* m : cands) {
      if (m->params.size() == 1 && m->params[0].type == t) unary.push_back(m);
    }
    if (unary.size() == 1) {
      e.sym = unary[0];
      e.aux = R(Res::Operator);
      return unary[0]->ret;
    }
    error(e.loc, std::string("operator ") + opText(tok) + " is not defined for '" + t->name() + "'");
    return nullptr;
  }
  switch (e.uop) {
    case UnaryOp::Neg: case UnaryOp::Plus:
      if (t->isNumeric()) return t;
      break;
    case UnaryOp::Not:
      if (t->kind == TypeKind::Bool) return t;
      break;
    case UnaryOp::BitNot:
      if (t->kind == TypeKind::Int) return t;
      break;
    default: break;
  }
  error(e.loc, std::string("operator ") + opText(tok) + " cannot be applied to '" + t->name() + "'");
  return nullptr;
}

const Type* Checker::binaryExpr(Expr& e) {
  const Tok op = e.op;
  const Type* a = exprNoVoid(*e.kids[0]);
  const Type* b = exprNoVoid(*e.kids[1]);
  if (a == nullptr || b == nullptr) return nullptr;

  if (op == Tok::AmpAmp || op == Tok::PipePipe) {
    if (a->kind != TypeKind::Bool || b->kind != TypeKind::Bool) {
      error(e.loc, std::string("operator ") + opText(op) + " needs bool operands, got '" + a->name() + "' and '" + b->name() + "'");
      return nullptr;
    }
    return boolT();
  }

  if (a->isClassLike() || b->isClassLike()) {
    bool found = false;
    const Type* r = userBinary(e, op, a, b, found);
    if (found) return r;
    if (op == Tok::NotEq) {  // derive != from a user ==
      r = userBinary(e, Tok::EqEq, a, b, found);
      if (found) { e.ival = 1; return r; }
    }
  }

  const bool bothNum = a->isNumeric() && b->isNumeric();
  const Type* num = (a->kind == TypeKind::Float || b->kind == TypeKind::Float) ? floatT() : intT();
  auto bad = [&]() -> const Type* {
    error(e.loc, std::string("operator ") + opText(op) + " cannot be applied to '" + a->name() + "' and '" + b->name() + "'");
    return nullptr;
  };
  switch (op) {
    case Tok::Plus:
      if (a->kind == TypeKind::String || b->kind == TypeKind::String) {
        auto okStr = [](const Type* t) { return t->kind == TypeKind::String || t->kind == TypeKind::Int || t->kind == TypeKind::Float || t->kind == TypeKind::Bool || t->kind == TypeKind::Enum; };
        if (okStr(a) && okStr(b)) return stringT();
        return bad();
      }
      [[fallthrough]];
    case Tok::Minus: case Tok::Star: case Tok::Slash: case Tok::Percent:
      return bothNum ? num : bad();
    case Tok::Amp: case Tok::Pipe: case Tok::Caret:
      if (a->kind == TypeKind::Int && b->kind == TypeKind::Int) return intT();
      if (a->kind == TypeKind::Bool && b->kind == TypeKind::Bool) return boolT();
      return bad();
    case Tok::Shl: case Tok::Shr:
      return (a->kind == TypeKind::Int && b->kind == TypeKind::Int) ? intT() : bad();
    case Tok::Lt: case Tok::Gt: case Tok::LtEq: case Tok::GtEq:
      if (bothNum || (a->kind == TypeKind::String && b->kind == TypeKind::String)) return boolT();
      return bad();
    case Tok::EqEq: case Tok::NotEq:
      if (bothNum) return boolT();
      if (a == b && (a->kind == TypeKind::Bool || a->kind == TypeKind::String || a->kind == TypeKind::Enum)) return boolT();
      if (a->isRefType() && b->isRefType()) {
        if (a->kind == TypeKind::Null || b->kind == TypeKind::Null || a == b || c_.assignable(a, b) || c_.assignable(b, a)) return boolT();
        if (a->isClassLike() && b->isClassLike() && (a->kind == TypeKind::Interface || b->kind == TypeKind::Interface)) return boolT();
      }
      return bad();
    default: return bad();
  }
}

const Type* Checker::assignExpr(Expr& e) {
  Expr& lhs = *e.kids[0];
  Expr& rhs = *e.kids[1];
  const Type* lt = expr(lhs);
  if (lhs.aux == R(Res::TypeName)) { error(lhs.loc, "cannot assign to a type"); expr(rhs); return nullptr; }
  if (lt == nullptr) { expr(rhs); return nullptr; }
  if (!isLValue(lhs)) {
    error(e.loc, "left side of assignment is not a variable, field or element");
    expr(rhs);
    return nullptr;
  }
  if (lhs.sym != nullptr && lhs.sym->kind == SymKind::Var) {
    auto* v = static_cast<VarSym*>(lhs.sym);
    if (v->name == "this") { error(e.loc, "cannot assign to 'this'"); return nullptr; }
    v->reassigned = true;
  }
  if (lhs.sym != nullptr && lhs.sym->kind == SymKind::Field) {
    auto* f = static_cast<FieldSym*>(lhs.sym);
    if (f->isFinal) {
      const bool viaThis = lhs.kind == ExprKind::Name || (lhs.kind == ExprKind::Member && lhs.kids[0]->kind == ExprKind::This);
      const bool inCtor = fn_->isCtor && fn_->parent == nullptr && f->owner == cls_ && viaThis && !f->isStatic;
      if (!inCtor) error(e.loc, "cannot assign to final field '" + f->name + "' here (only a constructor of '" + f->owner->name + "' may, once, via this)");
    }
  }

  if (e.op == Tok::Assign) {
    const Type* rt = exprNoVoid(rhs, lt);
    if (rt != nullptr) requireAssignable(rt, lt, rhs.loc, "assignment");
    return lt;
  }

  // compound
  const Tok bop = baseOp(e.op);
  const Type* rt = exprNoVoid(rhs, nullptr);
  if (rt == nullptr) return lt;
  if (lt->isClassLike() || rt->isClassLike()) {
    // Reuse the binary machinery on a scratch node: operands are the lhs/rhs expressions themselves.
    bool found = false;
    // userBinary reads e.kids[0], e.kids[1]: exactly lhs and rhs.
    const Type* r = userBinary(e, bop, lt, rt, found);
    if (found) {
      if (r != nullptr) requireAssignable(r, lt, e.loc, "compound assignment");
      return lt;
    }
  }
  auto bad = [&]() {
    error(e.loc, std::string("operator ") + opText(e.op) + " cannot be applied to '" + lt->name() + "' and '" + rt->name() + "'");
    return lt;
  };
  switch (bop) {
    case Tok::Plus:
      if (lt->kind == TypeKind::String) {
        if (rt->kind == TypeKind::String || rt->kind == TypeKind::Int || rt->kind == TypeKind::Float || rt->kind == TypeKind::Bool || rt->kind == TypeKind::Enum) return lt;
        return bad();
      }
      [[fallthrough]];
    case Tok::Minus: case Tok::Star: case Tok::Slash: case Tok::Percent:
      if (lt->isNumeric() && rt->isNumeric()) {
        if (lt->kind == TypeKind::Int && rt->kind == TypeKind::Float) {
          error(e.loc, "cannot apply a float to an int with " + std::string(opText(e.op)) + " (use 'as int' or write the assignment out)");
        }
        return lt;
      }
      return bad();
    case Tok::Amp: case Tok::Pipe: case Tok::Caret:
      if ((lt->kind == TypeKind::Int && rt->kind == TypeKind::Int) || (lt->kind == TypeKind::Bool && rt->kind == TypeKind::Bool)) return lt;
      return bad();
    case Tok::Shl: case Tok::Shr:
      return (lt->kind == TypeKind::Int && rt->kind == TypeKind::Int) ? lt : bad();
    default: return bad();
  }
}

const Type* Checker::indexExpr(Expr& e) {
  const Type* a = exprNoVoid(*e.kids[0]);
  const Type* i = exprNoVoid(*e.kids[1]);
  if (a == nullptr || i == nullptr) return nullptr;
  if (a->kind == TypeKind::Array) {
    if (i->kind != TypeKind::Int) { error(e.kids[1]->loc, "array index must be an int, not '" + i->name() + "'"); return nullptr; }
    e.aux = R(Res::ArrayIndex);
    return a->elem;
  }
  if (a->kind == TypeKind::String) {
    if (i->kind != TypeKind::Int) { error(e.kids[1]->loc, "string index must be an int"); return nullptr; }
    e.aux = R(Res::StringIndex);
    return intT();
  }
  if (a->isClassLike()) {
    std::vector<MethodSym*> cands;
    collectOperators(a->cls, Tok::LBracket, cands);
    std::vector<MethodSym*> getters, setters;
    for (MethodSym* m : cands) (m->params.size() == 2 ? getters : setters).push_back(m);
    std::vector<Arg> args(2);
    args[0].e = e.kids[0].get(); args[0].type = a;
    args[1].e = e.kids[1].get(); args[1].type = i;
    MethodSym* g = nullptr;
    int bestCost = 0;
    for (MethodSym* m : getters) {
      int cost = 0;
      if (!applicable(*m, args, cost)) continue;
      if (g == nullptr || cost < bestCost) { g = m; bestCost = cost; }
    }
    if (g == nullptr) { error(e.loc, "'" + a->name() + "' cannot be indexed with '" + i->name() + "'"); return nullptr; }
    e.sym = g;
    e.aux = R(Res::OperatorIndex);
    for (MethodSym* s : setters) {
      if (s->params[0].type == g->params[0].type && s->params[1].type == g->params[1].type && s->params[2].type == g->ret) e.extra = s;
    }
    return g->ret;
  }
  error(e.loc, "type '" + a->name() + "' cannot be indexed");
  return nullptr;
}

const Type* Checker::castExpr(Expr& e) {
  const Type* s = exprNoVoid(*e.kids[0]);
  const Type* t = resolve(*e.typeRef);
  if (s == nullptr || t == nullptr) return t;
  auto fail = [&]() -> const Type* {
    error(e.loc, "cannot convert '" + s->name() + "' to '" + t->name() + "'");
    return t;
  };
  if (s == t) { e.aux = R(Res::UpCast); return t; }
  if (s->kind == TypeKind::Null) {
    if (t->isRefType()) { e.aux = R(Res::UpCast); return t; }
    return fail();
  }
  if (s->isNumeric() && t->isNumeric()) { e.aux = R(Res::NumConv); return t; }
  if (t->kind == TypeKind::String) {
    if (s->kind == TypeKind::Int || s->kind == TypeKind::Float || s->kind == TypeKind::Bool || s->kind == TypeKind::Enum) { e.aux = R(Res::ToString); return t; }
    return fail();
  }
  if (s->kind == TypeKind::Enum && t->kind == TypeKind::Int) { e.aux = R(Res::EnumToInt); return t; }
  if (s->kind == TypeKind::Int && t->kind == TypeKind::Enum) { e.aux = R(Res::IntToEnum); return t; }
  if (s->isClassLike() && t->isClassLike()) {
    if (c_.assignable(s, t)) { e.aux = R(Res::UpCast); return t; }
    const bool down = t->cls->isAssignableTo(s->cls);
    const bool viaIface = (s->kind == TypeKind::Interface || t->kind == TypeKind::Interface) && !(s->cls->mods.isFinal || t->cls->mods.isFinal);
    if (down || viaIface) { e.aux = R(Res::RefCast); e.sym = t->cls; return t; }
  }
  return fail();
}

const Type* Checker::ternaryExpr(Expr& e, const Type* expected) {
  const Type* c = exprNoVoid(*e.kids[0]);
  if (c != nullptr && c->kind != TypeKind::Bool) error(e.kids[0]->loc, "condition must be bool, not '" + c->name() + "'");
  const Type* a = exprNoVoid(*e.kids[1], expected);
  const Type* b = exprNoVoid(*e.kids[2], expected);
  if (a == nullptr || b == nullptr) return nullptr;
  const Type* u = unify(a, b);
  if (u == nullptr) {
    error(e.loc, "the branches of ?: have incompatible types '" + a->name() + "' and '" + b->name() + "'");
    return nullptr;
  }
  if (u->kind == TypeKind::Null) { error(e.loc, "cannot infer the type of this conditional"); return nullptr; }
  return u;
}

const Type* Checker::arrayLitExpr(Expr& e, const Type* expected) {
  const Type* elem = nullptr;
  if (expected != nullptr && expected->kind == TypeKind::Array) elem = expected->elem;
  if (e.kids.empty()) {
    if (elem == nullptr) { error(e.loc, "cannot infer the element type of an empty array literal; assign it to a typed variable"); return nullptr; }
    return p_.types.arrayOf(elem);
  }
  if (elem != nullptr) {
    for (auto& k : e.kids) {
      const Type* t = exprNoVoid(*k, elem);
      if (t != nullptr) requireAssignable(t, elem, k->loc, "array element");
    }
    return p_.types.arrayOf(elem);
  }
  const Type* u = nullptr;
  bool bad = false;
  for (auto& k : e.kids) {
    const Type* t = exprNoVoid(*k);
    if (t == nullptr) { bad = true; continue; }
    if (u == nullptr) { u = t; continue; }
    const Type* n = unify(u, t);
    if (n == nullptr) {
      error(k->loc, "array element of type '" + t->name() + "' does not match '" + u->name() + "'");
      bad = true;
    } else {
      u = n;
    }
  }
  if (bad) return nullptr;
  if (u->kind == TypeKind::Null) { error(e.loc, "cannot infer the element type of this array literal"); return nullptr; }
  return p_.types.arrayOf(u);
}

const Type* Checker::lambdaExpr(Expr& e, const Type* expected) {
  LambdaData& ld = *e.lambda;
  const Type* expFn = (expected != nullptr && expected->kind == TypeKind::Func) ? expected : nullptr;
  if (expFn != nullptr && expFn->params.size() != ld.params.size()) {
    error(e.loc, "this lambda takes " + std::to_string(ld.params.size()) + " parameter(s) but '" + expFn->name() + "' needs " + std::to_string(expFn->params.size()));
    return nullptr;
  }
  FuncInfo* info = newFunc(fn_->info);
  info->isLambda = true;
  info->cls = cls_;
  info->isStatic = fn_->info->isStatic;
  e.extra = info;

  FnState st;
  beginFn(st, info, fn_);
  bool ok = true;
  std::vector<FuncParam> ptypes;
  for (std::size_t i = 0; i < ld.params.size(); ++i) {
    Param& p = ld.params[i];
    const Type* pt = nullptr;
    if (p.type) pt = resolve(*p.type);
    if (expFn != nullptr) {
      const FuncParam& want = expFn->params[i];
      if (pt != nullptr && pt != want.type) {
        error(p.loc, "lambda parameter '" + p.name + "' is '" + pt->name() + "' but '" + want.type->name() + "' is expected");
        ok = false;
      }
      if (p.isRef != want.isRef) {
        error(p.loc, "lambda parameter '" + p.name + "' must " + std::string(want.isRef ? "be" : "not be") + " 'ref'");
        ok = false;
      }
      if (pt == nullptr) pt = want.type;
    }
    if (pt == nullptr) {
      error(p.loc, "cannot infer the type of lambda parameter '" + p.name + "'; declare it or use the lambda where a function type is expected");
      pt = intT();
      ok = false;
    }
    VarSym* v = declareVar(p.name, pt, p.loc, true);
    v->isRefParam = p.isRef;
    ptypes.push_back({pt, p.isRef});
  }

  const Type* declaredRet = expFn != nullptr ? expFn->ret : nullptr;
  info->ret = declaredRet;
  info->retInferred = declaredRet == nullptr;
  const Type* ret = declaredRet;
  if (ld.bodyExpr) {
    const Type* bt = expr(*ld.bodyExpr, declaredRet);
    if (ld.bodyExpr->aux == R(Res::TypeName)) { error(ld.bodyExpr->loc, "a type is not a value"); bt = nullptr; }
    if (declaredRet == nullptr) {
      ret = bt != nullptr ? bt : voidT();
      if (ret->kind == TypeKind::Null) { error(ld.bodyExpr->loc, "cannot infer the lambda's return type from 'null'"); ret = intT(); }
    } else if (bt != nullptr && declaredRet->kind != TypeKind::Void) {
      requireAssignable(bt, declaredRet, ld.bodyExpr->loc, "lambda result");
    }
  } else {
    const bool completes = stmt(*ld.bodyBlock);
    if (declaredRet == nullptr) {
      if (st.returnTypes.empty()) {
        ret = voidT();
      } else {
        ret = st.returnTypes[0];
        for (const Type* t : st.returnTypes) {
          const Type* n = unify(ret, t);
          if (n == nullptr) { error(ld.bodyBlock->loc, "lambda returns incompatible types '" + ret->name() + "' and '" + t->name() + "'"); break; }
          ret = n;
        }
        if (ret->kind == TypeKind::Null) { error(ld.bodyBlock->loc, "cannot infer the lambda's return type"); ret = intT(); }
      }
    }
    if (completes && ret != nullptr && ret->kind != TypeKind::Void) error(ld.bodyBlock->endLoc, "lambda must return a value on every path");
  }
  info->ret = ret;
  endFn();
  if (!ok || ret == nullptr) return nullptr;
  return p_.types.func(std::move(ptypes), ret);
}

}  // namespace mingol::script
