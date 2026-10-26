// SPDX-License-Identifier: GPL-2.0-only
// Declaration pass: collects types, resolves headers, lays out classes and builds dispatch tables.
#include <algorithm>

#include "compiler_internal.hpp"

namespace mingol::script {
namespace {

std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "." : "") + v[i];
  return s;
}

bool sameParams(const MethodSym& a, const MethodSym& b) {
  if (a.params.size() != b.params.size()) return false;
  for (std::size_t i = 0; i < a.params.size(); ++i) {
    if (a.params[i].type != b.params[i].type || a.params[i].isRef != b.params[i].isRef) return false;
  }
  return true;
}

int accessRank(Access a) { return a == Access::Public ? 2 : a == Access::Protected ? 1 : 0; }

const char* accessName(Access a) { return a == Access::Public ? "public" : a == Access::Protected ? "protected" : "private"; }

}  // namespace

const Type* Compiler::objectType() const { return p_.objectClass != nullptr ? p_.objectClass->thisType : nullptr; }
const Type* Compiler::exceptionType() const { return p_.exceptionClass != nullptr ? p_.exceptionClass->thisType : nullptr; }

bool Compiler::assignable(const Type* from, const Type* to) const {
  if (from == to) return true;
  if (from == nullptr || to == nullptr) return false;
  if (from->kind == TypeKind::Int && to->kind == TypeKind::Float) return true;
  if (from->kind == TypeKind::Null) return to->isRefType() && to->kind != TypeKind::Null;
  if (from->isClassLike() && to->isClassLike()) return from->cls->isAssignableTo(to->cls) || (p_.objectClass != nullptr && to->cls == p_.objectClass);
  return false;
}

void Compiler::collectTypes() {
  for (UnitInfo& u : units_) {
    for (auto& td : u.unit->types) {
      auto c = std::make_unique<ClassSym>();
      c->name = td->name;
      c->moduleName = u.module;
      c->qualifiedName = u.module.empty() ? td->name : u.module + "." + td->name;
      c->kind = td->kind;
      c->mods = td->mods;
      c->decl = td.get();
      c->unit = u.unit;
      c->loc = td->loc;
      c->id = static_cast<int>(p_.classes.size());
      td->sym = c.get();
      auto& slot = modules_[u.module][td->name];
      if (slot != nullptr) {
        error(td->loc, "type '" + c->qualifiedName + "' is already declared");
      } else {
        slot = c.get();
      }
      unitOf_[c.get()] = &u;
      p_.classes.push_back(std::move(c));
    }
  }
  for (auto& c : p_.classes) c->thisType = p_.types.classType(c.get());
}

void Compiler::bindWellKnown() {
  auto find = [&](const char* n) -> ClassSym* {
    auto it = modules_.find("mingol");
    if (it == modules_.end()) return nullptr;
    auto jt = it->second.find(n);
    return jt == it->second.end() ? nullptr : jt->second;
  };
  p_.objectClass = find("Object");
  p_.exceptionClass = find("Exception");
  p_.coroutineClass = find("Coroutine");
  p_.typeClass = find("Type");
}

void Compiler::checkModules() {
  // Every import must name a module that exists; modules must not import each other in a cycle.
  std::map<std::string, std::set<std::string>> edges;
  for (UnitInfo& u : units_) {
    for (const Import& im : u.unit->imports) {
      const std::string m = join(im.path);
      if (modules_.find(m) == modules_.end()) {
        error(im.loc, "unknown module '" + m + "'");
        continue;
      }
      u.imports.push_back(m);
      if (m != u.module) edges[u.module].insert(m);
    }
  }
  enum { White, Grey, Black };
  std::map<std::string, int> color;
  std::vector<std::string> path;
  std::function<void(const std::string&)> dfs = [&](const std::string& m) {
    color[m] = Grey;
    path.push_back(m);
    for (const std::string& n : edges[m]) {
      if (color[n] == Grey) {
        std::string cyc;
        bool on = false;
        for (const std::string& x : path) {
          if (x == n) on = true;
          if (on) cyc += (cyc.empty() ? "" : " -> ") + (x.empty() ? "<default>" : x);
        }
        cyc += " -> " + n;
        error({}, "circular module dependency: " + cyc);
      } else if (color[n] == White) {
        dfs(n);
      }
    }
    path.pop_back();
    color[m] = Black;
  };
  for (const auto& [m, _] : edges) {
    if (color[m] == White) dfs(m);
  }
}

ClassSym* Compiler::lookupSimple(const std::string& name, const UnitInfo& u, SourceLoc loc, bool report) {
  auto own = modules_.find(u.module);
  if (own != modules_.end()) {
    auto it = own->second.find(name);
    if (it != own->second.end()) return it->second;
  }
  ClassSym* found = nullptr;
  std::string firstModule;
  for (const std::string& m : u.imports) {
    auto mod = modules_.find(m);
    if (mod == modules_.end()) continue;
    auto it = mod->second.find(name);
    if (it == mod->second.end() || it->second == found) continue;
    if (found != nullptr) {
      if (report) error(loc, "'" + name + "' is ambiguous between modules '" + firstModule + "' and '" + m + "'; qualify it");
      return found;
    }
    found = it->second;
    firstModule = m;
  }
  if (found != nullptr) return found;
  auto pre = modules_.find("mingol");
  if (pre != modules_.end()) {
    auto it = pre->second.find(name);
    if (it != pre->second.end()) return it->second;
  }
  if (report) error(loc, "unknown type '" + name + "'");
  return nullptr;
}

ClassSym* Compiler::lookupClass(const std::vector<std::string>& path, const UnitInfo& u, SourceLoc loc, bool report) {
  if (path.size() == 1) return lookupSimple(path[0], u, loc, report);
  const std::vector<std::string> modPath(path.begin(), path.end() - 1);
  auto mod = modules_.find(join(modPath));
  if (mod != modules_.end()) {
    auto it = mod->second.find(path.back());
    if (it != mod->second.end()) return it->second;
  }
  if (report) error(loc, "unknown type '" + join(path) + "'");
  return nullptr;
}

const Type* Compiler::resolveType(const TypeRef& t, const UnitInfo& u, bool allowVoid) {
  switch (t.kind) {
    case TypeRef::Kind::Prim:
      switch (t.prim) {
        case Tok::KwInt: return p_.types.intType();
        case Tok::KwFloat: return p_.types.floatType();
        case Tok::KwBool: return p_.types.boolType();
        case Tok::KwString: return p_.types.stringType();
        default:
          if (!allowVoid) error(t.loc, "'void' is not allowed here");
          return p_.types.voidType();
      }
    case TypeRef::Kind::Named: {
      ClassSym* c = lookupClass(t.path, u, t.loc, true);
      return c != nullptr ? c->thisType : p_.types.intType();  // int after an error: avoids cascades
    }
    case TypeRef::Kind::Array: {
      const Type* e = resolveType(*t.elem, u, false);
      return p_.types.arrayOf(e);
    }
    case TypeRef::Kind::Pointer: {
      const Type* e = resolveType(*t.elem, u, false);
      return p_.types.pointerTo(e);
    }
    case TypeRef::Kind::Func: {
      std::vector<FuncParam> ps;
      for (std::size_t i = 0; i < t.params.size(); ++i) ps.push_back({resolveType(*t.params[i], u, false), t.paramRef[i]});
      return p_.types.func(std::move(ps), resolveType(*t.ret, u, true));
    }
  }
  return p_.types.intType();
}

void Compiler::resolveAllHeaders() {
  std::vector<ClassSym*> stack;
  for (auto& c : p_.classes) resolveHeader(*c, stack);
}

void Compiler::resolveHeader(ClassSym& c, std::vector<ClassSym*>& stack) {
  int& st = headerState_[&c];
  if (st == 2) return;
  if (st == 1) {
    std::string cyc;
    for (ClassSym* s : stack) cyc += s->name + " -> ";
    error(c.loc, "cyclic inheritance involving '" + c.name + "' (" + cyc + c.name + ")");
    return;
  }
  st = 1;
  stack.push_back(&c);
  const UnitInfo& u = *unitOf_[&c];
  TypeDecl& td = *c.decl;

  auto resolveParent = [&](const TypeRef& ref, bool wantInterface) -> ClassSym* {
    ClassSym* p = ref.kind == TypeRef::Kind::Named ? lookupClass(ref.path, u, ref.loc, true) : nullptr;
    if (ref.kind != TypeRef::Kind::Named) error(ref.loc, "expected a class or interface name");
    if (p == nullptr) return nullptr;
    if (wantInterface && p->kind != TypeDeclKind::Interface) {
      error(ref.loc, "'" + p->qualifiedName + "' is not an interface");
      return nullptr;
    }
    if (!wantInterface && p->kind != TypeDeclKind::Class) {
      error(ref.loc, "'" + p->qualifiedName + "' is not a class" + (p->kind == TypeDeclKind::Interface ? "; use 'implements'" : ""));
      return nullptr;
    }
    resolveHeader(*p, stack);
    return p;
  };

  if (c.kind == TypeDeclKind::Class) {
    if (td.base) {
      ClassSym* b = resolveParent(*td.base, false);
      if (b != nullptr) {
        if (b->mods.isFinal) error(td.base->loc, "cannot extend final class '" + b->qualifiedName + "'");
        else if (b == &c) error(td.base->loc, "a class cannot extend itself");
        else c.base = b;
      }
    }
    for (auto& i : td.interfaces) {
      if (ClassSym* ic = resolveParent(*i, true)) {
        if (std::find(c.interfaces.begin(), c.interfaces.end(), ic) != c.interfaces.end()) error(i->loc, "interface '" + ic->name + "' is listed twice");
        else c.interfaces.push_back(ic);
      }
    }
    if (c.base == nullptr && &c != p_.objectClass && p_.objectClass != nullptr && headerState_[p_.objectClass] != 1) {
      c.base = p_.objectClass;
      resolveHeader(*p_.objectClass, stack);
    }
  } else if (c.kind == TypeDeclKind::Interface) {
    for (auto& i : td.interfaces) {
      if (ClassSym* ic = resolveParent(*i, true)) {
        if (ic == &c) error(i->loc, "an interface cannot extend itself");
        else c.interfaces.push_back(ic);
      }
    }
  }

  declareMembers(c);
  layoutClass(c);
  buildTables(c);
  c.resolved = true;
  stack.pop_back();
  headerState_[&c] = 2;
}

void Compiler::checkOperatorDecl(ClassSym& c, MethodSym& m) {
  const SourceLoc l = m.loc;
  if (!m.mods.isStatic) error(l, "operator methods must be 'static'");
  for (const ParamInfo& pi : m.params) {
    if (pi.isRef) error(l, "operator parameters cannot be 'ref'");
  }
  const std::size_t n = m.params.size();
  bool ownsParam = false;
  for (const ParamInfo& pi : m.params) ownsParam = ownsParam || pi.type == c.thisType;
  if (!ownsParam) error(l, "an operator of '" + c.name + "' must have a parameter of type '" + c.name + "'");
  switch (m.opToken) {
    case Tok::Bang: case Tok::Tilde:
      if (n != 1) error(l, "unary operator needs exactly one parameter");
      break;
    case Tok::Plus: case Tok::Minus:
      if (n != 1 && n != 2) error(l, "operator needs one or two parameters");
      break;
    case Tok::LBracket:
      if (n != 2 && n != 3) error(l, "operator[] needs (object, index) or (object, index, value)");
      else if (n == 2 && m.ret->kind == TypeKind::Void) error(l, "operator[] getter must return a value");
      else if (n == 3 && m.ret->kind != TypeKind::Void) error(l, "operator[] setter must return void");
      break;
    default:
      if (n != 2) error(l, "binary operator needs exactly two parameters");
      if (m.opToken == Tok::EqEq || m.opToken == Tok::NotEq || m.opToken == Tok::Lt || m.opToken == Tok::Gt || m.opToken == Tok::LtEq ||
          m.opToken == Tok::GtEq) {
        if (m.ret->kind != TypeKind::Bool) error(l, "comparison operators must return bool");
      }
  }
  if (m.opToken != Tok::LBracket && m.ret->kind == TypeKind::Void) error(l, "operator must return a value");
}

void Compiler::declareMembers(ClassSym& c) {
  const UnitInfo& u = *unitOf_[&c];
  TypeDecl& td = *c.decl;
  const bool isIface = c.kind == TypeDeclKind::Interface;
  std::int64_t nextEnum = 0;
  bool hasCtor = false;

  for (Member& m : td.members) {
    if (m.kind == MemberKind::EnumConstant) {
      auto ec = std::make_unique<EnumConstSym>();
      ec->name = m.name;
      ec->loc = m.loc;
      ec->owner = &c;
      ec->type = c.thisType;
      std::int64_t v = nextEnum;
      if (m.init) {
        const Expr& e = *m.init;
        if (e.kind == ExprKind::IntLit) v = e.ival;
        else if (e.kind == ExprKind::Unary && e.uop == UnaryOp::Neg && e.kids[0]->kind == ExprKind::IntLit) v = -e.kids[0]->ival;
        else error(e.loc, "enum values must be integer literals");
      }
      for (auto& other : c.enumConsts) {
        if (other->name == m.name) error(m.loc, "duplicate enum constant '" + m.name + "'");
      }
      ec->value = v;
      nextEnum = v + 1;
      c.enumConsts.push_back(std::move(ec));
      continue;
    }

    const Modifiers& mo = m.mods;
    if (isIface && (mo.explicitAccess && mo.access != Access::Public)) error(m.loc, "interface members are always public");
    if (isIface && m.kind != MemberKind::Method) error(m.loc, "interfaces can only declare methods");
    if (isIface && (mo.isStatic || mo.isFinal || mo.isAbstract || mo.isOverride || mo.isNative)) error(m.loc, "invalid modifier on an interface method");
    if (c.kind == TypeDeclKind::Enum) continue;

    if (m.kind == MemberKind::Field) {
      auto f = std::make_unique<FieldSym>();
      f->name = m.name;
      f->loc = m.loc;
      f->owner = &c;
      f->decl = &m;
      f->isStatic = mo.isStatic;
      f->isFinal = mo.isFinal;
      f->access = mo.access;
      f->type = resolveType(*m.type, u, false);
      if (mo.isAbstract || mo.isOverride || mo.isNative || mo.isCoroutine) error(m.loc, "invalid modifier on a field");
      for (auto& o : c.fields) {
        if (o->name == m.name) error(m.loc, "duplicate field '" + m.name + "' in '" + c.name + "'");
      }
      m.sym = f.get();
      c.fields.push_back(std::move(f));
      continue;
    }

    auto ms = std::make_unique<MethodSym>();
    ms->name = m.name;
    ms->loc = m.loc;
    ms->owner = &c;
    ms->decl = &m;
    ms->mods = mo;
    ms->mkind = m.kind == MemberKind::Constructor ? MethodKind::Constructor : m.kind == MemberKind::Operator ? MethodKind::Operator : MethodKind::Method;
    ms->opToken = m.opToken;
    for (const Param& p : m.params) {
      ParamInfo pi;
      pi.name = p.name;
      pi.isRef = p.isRef;
      pi.type = resolveType(*p.type, u, false);
      for (const ParamInfo& prev : ms->params) {
        if (prev.name == p.name) error(p.loc, "duplicate parameter '" + p.name + "'");
      }
      ms->params.push_back(std::move(pi));
    }
    ms->ret = m.kind == MemberKind::Constructor ? p_.types.voidType() : resolveType(*m.type, u, true);
    if (ms->mkind == MethodKind::Constructor) {
      hasCtor = true;
      if (mo.isStatic || mo.isAbstract || mo.isOverride || mo.isFinal || mo.isNative || mo.isCoroutine) error(m.loc, "invalid modifier on a constructor");
    }
    if (ms->mkind == MethodKind::Operator) {
      checkOperatorDecl(c, *ms);
      if (mo.isAbstract || mo.isOverride || mo.isNative || mo.isCoroutine) error(m.loc, "invalid modifier on an operator");
    }
    if (mo.isCoroutine && ms->mkind == MethodKind::Method && mo.isAbstract) error(m.loc, "a coroutine cannot be abstract");
    if (mo.isStatic && (mo.isAbstract || mo.isOverride)) error(m.loc, "a static method cannot be abstract or override");
    if (mo.isAbstract && mo.isFinal) error(m.loc, "a method cannot be both abstract and final");
    if (mo.isAbstract && mo.access == Access::Private) error(m.loc, "an abstract method cannot be private");
    if (mo.isAbstract && !c.mods.isAbstract && !isIface) error(m.loc, "abstract method '" + m.name + "' in non-abstract class '" + c.name + "'");
    if (mo.isNative && !isIface && m.body) error(m.loc, "a native method cannot have a body");
    const bool needsBody = !isIface && !mo.isAbstract && !mo.isNative;
    if (needsBody && !m.body) error(m.loc, "method '" + m.name + "' needs a body");
    if ((isIface || mo.isAbstract) && m.body) error(m.loc, "an abstract or interface method cannot have a body");
    if (mo.isNative && !c.mods.isNative) error(m.loc, "native methods are only allowed in 'native' classes");
    if (isIface && mo.isCoroutine) error(m.loc, "interface methods cannot be coroutines");
    if (isIface) {
      ms->mods.isAbstract = true;
      ms->interfaceKey = p_.interfaceKeyCount++;
    }
    for (auto& o : c.methods) {
      if (o->name == ms->name && sameParams(*o, *ms)) {
        error(m.loc, "duplicate method '" + ms->signature() + "' in '" + c.name + "'");
        break;
      }
    }
    m.sym = ms.get();
    c.methods.push_back(std::move(ms));
  }

  if (c.kind == TypeDeclKind::Class && !hasCtor) {
    auto ms = std::make_unique<MethodSym>();
    ms->name = "<init>";
    ms->loc = c.loc;
    ms->owner = &c;
    ms->mkind = MethodKind::Constructor;
    ms->ret = p_.types.voidType();
    ms->mods.access = c.mods.isAbstract ? Access::Protected : Access::Public;
    c.methods.push_back(std::move(ms));  // decl == nullptr marks the synthesised default constructor
  }
}

void Compiler::layoutClass(ClassSym& c) {
  if (c.kind == TypeDeclKind::Interface || c.kind == TypeDeclKind::Enum) {
    for (auto& f : c.fields) {
      if (f->isStatic) f->index = p_.staticCount++;
    }
    return;
  }
  c.baseFieldCount = c.base != nullptr ? c.base->instanceFieldCount : 0;
  c.depth = c.base != nullptr ? c.base->depth + 1 : 0;
  int next = c.baseFieldCount;
  for (auto& f : c.fields) {
    if (f->isStatic) {
      f->index = p_.staticCount++;
      p_.staticFields.push_back(f.get());
    } else {
      f->index = next++;
      if (c.base != nullptr && c.base->findField(f->name) != nullptr) {
        error(f->loc, "field '" + f->name + "' hides a field of base class '" + c.base->name + "'");
      }
    }
  }
  c.instanceFieldCount = next;
  if (next > 60000) error(c.loc, "class has too many fields");
}

void Compiler::buildTables(ClassSym& c) {
  auto addUnique = [](std::vector<ClassSym*>& v, ClassSym* x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
  };
  if (c.kind == TypeDeclKind::Interface) {
    for (ClassSym* i : c.interfaces) {
      addUnique(c.allInterfaces, i);
      for (ClassSym* j : i->allInterfaces) addUnique(c.allInterfaces, j);
    }
    return;
  }
  if (c.kind == TypeDeclKind::Enum) return;

  if (c.base != nullptr) {
    c.vtable = c.base->vtable;
    c.allInterfaces = c.base->allInterfaces;
  }
  for (ClassSym* i : c.interfaces) {
    addUnique(c.allInterfaces, i);
    for (ClassSym* j : i->allInterfaces) addUnique(c.allInterfaces, j);
  }

  for (auto& mp : c.methods) {
    MethodSym& m = *mp;
    if (m.mkind != MethodKind::Method) continue;
    if (m.mods.isStatic || m.mods.access == Access::Private) {
      if (m.mods.isOverride) error(m.loc, "'" + m.name + "' is " + std::string(m.mods.isStatic ? "static" : "private") + " and cannot override");
      continue;
    }
    int found = -1;
    for (std::size_t i = 0; i < c.vtable.size(); ++i) {
      if (c.vtable[i]->name == m.name && sameParams(*c.vtable[i], m)) { found = static_cast<int>(i); break; }
    }
    if (found >= 0) {
      MethodSym* b = c.vtable[static_cast<std::size_t>(found)];
      if (!m.mods.isOverride) {
        error(m.loc, "'" + m.signature() + "' overrides '" + b->owner->name + "." + b->signature() + "'; add the 'override' modifier");
      }
      if (b->mods.isFinal) error(m.loc, "cannot override final method '" + b->owner->name + "." + b->signature() + "'");
      if (b->ret != m.ret) error(m.loc, "override of '" + m.signature() + "' must return '" + b->ret->name() + "', not '" + m.ret->name() + "'");
      if (accessRank(m.mods.access) < accessRank(b->mods.access))
        error(m.loc, std::string("override cannot be less accessible than the base method (") + accessName(b->mods.access) + ")");
      if (b->mods.isCoroutine != m.mods.isCoroutine) error(m.loc, "override must match the base method's 'coroutine' modifier");
      m.overrides = b;
      m.vtableSlot = found;
      c.vtable[static_cast<std::size_t>(found)] = &m;
    } else {
      if (m.mods.isOverride) error(m.loc, "'" + m.signature() + "' does not override anything");
      m.vtableSlot = static_cast<int>(c.vtable.size());
      c.vtable.push_back(&m);
    }
  }

  if (!c.isAbstractType()) {
    for (MethodSym* m : c.vtable) {
      if (m->mods.isAbstract) error(c.loc, "class '" + c.name + "' must implement abstract method '" + m->owner->name + "." + m->signature() + "'");
    }
  }

  for (ClassSym* iface : c.allInterfaces) {
    for (auto& im : iface->methods) {
      MethodSym* impl = nullptr;
      for (MethodSym* m : c.vtable) {
        if (m->name == im->name && sameParams(*m, *im)) { impl = m; break; }
      }
      if (impl == nullptr) {
        if (!c.isAbstractType()) error(c.loc, "class '" + c.name + "' does not implement '" + iface->name + "." + im->signature() + "'");
        continue;
      }
      if (impl->ret != im->ret) error(impl->loc, "'" + impl->signature() + "' must return '" + im->ret->name() + "' to implement '" + iface->name + "'");
      if (impl->mods.access != Access::Public) error(impl->loc, "'" + impl->signature() + "' implements an interface method and must be public");
      if (impl->mods.isCoroutine != im->mods.isCoroutine && im->mods.isCoroutine) error(impl->loc, "interface method is a coroutine");
      c.itable[im->interfaceKey] = impl;
    }
  }
}

void Compiler::assignFuncIds() {
  for (auto& c : p_.classes) {
    for (auto& m : c->methods) {
      if (m->isAbstract() && !m->mods.isNative) continue;
      if (c->kind == TypeDeclKind::Interface) continue;
      FuncProto fp;
      fp.name = c->name + "." + m->signature();
      fp.method = m.get();
      fp.isNative = m->mods.isNative;
      fp.isCoroutine = m->mods.isCoroutine;
      fp.hasThis = !m->mods.isStatic && m->mkind != MethodKind::Operator;
      if (fp.isNative) fp.nativeName = c->qualifiedName + "." + m->signature();
      fp.numParams = static_cast<int>(m->params.size()) + (fp.hasThis ? 1 : 0);
      m->funcId = static_cast<int>(p_.funcs.size());
      p_.funcs.push_back(std::move(fp));
    }
  }
}

}  // namespace mingol::script
