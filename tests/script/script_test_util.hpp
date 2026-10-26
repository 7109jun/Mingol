// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mingol/script/parser.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

// Deterministic generator for the fuzz-style tests.
struct ScriptLcg {
  std::uint64_t s;
  explicit ScriptLcg(std::uint64_t seed) : s(seed) {}
  std::uint32_t next() {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::uint32_t>(s >> 33);
  }
  std::uint32_t below(std::uint32_t n) { return n == 0 ? 0 : next() % n; }
};

namespace sd {
using namespace mingol::script;

inline std::string opName(Tok t) {
  std::string n = tokName(t);
  if (n.size() >= 2 && n.front() == '\'') n = n.substr(1, n.size() - 2);
  return n;
}

inline std::string uop(UnaryOp o) {
  switch (o) {
    case UnaryOp::Neg: return "neg";
    case UnaryOp::Plus: return "pos";
    case UnaryOp::Not: return "not";
    case UnaryOp::BitNot: return "bitnot";
    case UnaryOp::AddrOf: return "addr";
    case UnaryOp::Deref: return "deref";
    case UnaryOp::PreInc: return "preinc";
    case UnaryOp::PreDec: return "predec";
    case UnaryOp::PostInc: return "postinc";
    case UnaryOp::PostDec: return "postdec";
  }
  return "?";
}

inline std::string type(const TypeRef* t) {
  if (t == nullptr) return "var";
  switch (t->kind) {
    case TypeRef::Kind::Prim: return opName(t->prim);
    case TypeRef::Kind::Named: {
      std::string s;
      for (std::size_t i = 0; i < t->path.size(); ++i) s += (i ? "." : "") + t->path[i];
      return s;
    }
    case TypeRef::Kind::Array: return type(t->elem.get()) + "[]";
    case TypeRef::Kind::Pointer: return type(t->elem.get()) + "*";
    case TypeRef::Kind::Func: {
      std::string s = "fn(";
      for (std::size_t i = 0; i < t->params.size(); ++i) s += (i ? "," : "") + std::string(t->paramRef[i] ? "ref " : "") + type(t->params[i].get());
      return s + "):" + type(t->ret.get());
    }
  }
  return "?";
}

std::string stmt(const Stmt* s);

inline std::string expr(const Expr* e) {
  if (e == nullptr) return "_";
  auto kids = [&](std::size_t from = 0) {
    std::string s;
    for (std::size_t i = from; i < e->kids.size(); ++i) s += " " + expr(e->kids[i].get());
    return s;
  };
  switch (e->kind) {
    case ExprKind::IntLit: return std::to_string(e->ival);
    case ExprKind::FloatLit: { char b[64]; std::snprintf(b, sizeof b, "%g", e->fval); return std::string(b) + "f"; }
    case ExprKind::StringLit: return "\"" + e->text + "\"";
    case ExprKind::BoolLit: return e->ival ? "true" : "false";
    case ExprKind::NullLit: return "null";
    case ExprKind::This: return "this";
    case ExprKind::Super: return "super";
    case ExprKind::Name: return e->text;
    case ExprKind::Member: return "(. " + expr(e->kids[0].get()) + " " + e->text + ")";
    case ExprKind::Index: return "([] " + expr(e->kids[0].get()) + " " + expr(e->kids[1].get()) + ")";
    case ExprKind::Call: return "(call" + kids() + ")";
    case ExprKind::New: return "(new " + type(e->typeRef.get()) + kids() + ")";
    case ExprKind::NewArray: return "(newarray " + type(e->typeRef.get()) + kids() + ")";
    case ExprKind::ArrayLit: return "(array" + kids() + ")";
    case ExprKind::Unary: return "(" + uop(e->uop) + kids() + ")";
    case ExprKind::Binary: return "(" + opName(e->op) + kids() + ")";
    case ExprKind::Assign: return "(" + opName(e->op) + kids() + ")";
    case ExprKind::Ternary: return "(?" + kids() + ")";
    case ExprKind::Cast: return "(as " + expr(e->kids[0].get()) + " " + type(e->typeRef.get()) + ")";
    case ExprKind::Is: return "(is " + expr(e->kids[0].get()) + " " + type(e->typeRef.get()) + ")";
    case ExprKind::Typeof: return e->typeRef ? "(typeof " + type(e->typeRef.get()) + ")" : "(typeof" + kids() + ")";
    case ExprKind::Lambda: {
      std::string s = "(lambda (";
      for (std::size_t i = 0; i < e->lambda->params.size(); ++i) {
        const Param& p = e->lambda->params[i];
        s += (i ? " " : "") + std::string(p.isRef ? "ref " : "") + (p.type ? type(p.type.get()) + " " : "") + p.name;
      }
      s += ") ";
      s += e->lambda->bodyExpr ? expr(e->lambda->bodyExpr.get()) : stmt(e->lambda->bodyBlock.get());
      return s + ")";
    }
  }
  return "?";
}

inline std::string stmt(const Stmt* s) {
  if (s == nullptr) return "_";
  auto st = [&](std::size_t i) { return i < s->stmts.size() ? stmt(s->stmts[i].get()) : std::string("_"); };
  auto ex = [&](std::size_t i) { return i < s->exprs.size() ? expr(s->exprs[i].get()) : std::string("_"); };
  switch (s->kind) {
    case StmtKind::Block: {
      std::string r = "{";
      for (const auto& c : s->stmts) r += " " + stmt(c.get());
      return r + " }";
    }
    case StmtKind::VarDecl: return "(decl " + type(s->declType.get()) + " " + s->name + (s->exprs.empty() ? "" : " " + ex(0)) + ")";
    case StmtKind::ExprStmt: return "(expr " + ex(0) + ")";
    case StmtKind::If: return "(if " + ex(0) + " " + st(0) + (s->stmts.size() > 1 ? " " + st(1) : "") + ")";
    case StmtKind::While: return "(while " + ex(0) + " " + st(0) + ")";
    case StmtKind::DoWhile: return "(do " + st(0) + " " + ex(0) + ")";
    case StmtKind::For: return "(for " + st(0) + " " + ex(0) + " " + ex(1) + " " + st(1) + ")";
    case StmtKind::ForEach: return "(foreach " + type(s->declType.get()) + " " + s->name + " " + ex(0) + " " + st(0) + ")";
    case StmtKind::Switch: {
      std::string r = "(switch " + ex(0);
      for (const auto& c : s->cases) {
        r += c.isDefault ? " (default" : " (case";
        for (const auto& l : c.labels) r += " " + expr(l.get());
        r += " :";
        for (const auto& b : c.body) r += " " + stmt(b.get());
        r += ")";
      }
      return r + ")";
    }
    case StmtKind::Break: return "(break)";
    case StmtKind::Continue: return "(continue)";
    case StmtKind::Return: return "(return" + (s->exprs.empty() ? "" : " " + ex(0)) + ")";
    case StmtKind::Throw: return "(throw " + ex(0) + ")";
    case StmtKind::Yield: return "(yield" + (s->exprs.empty() ? "" : " " + ex(0)) + ")";
    case StmtKind::Try: {
      std::string r = "(try " + st(0);
      for (const auto& c : s->catches) r += " (catch " + type(c.type.get()) + " " + c.name + " " + stmt(c.body.get()) + ")";
      if (s->stmts.size() > 1) r += " (finally " + st(1) + ")";
      return r + ")";
    }
  }
  return "?";
}

inline std::string mods(const Modifiers& m) {
  std::string s;
  if (m.explicitAccess) s += m.access == Access::Public ? "public " : m.access == Access::Private ? "private " : "protected ";
  if (m.isStatic) s += "static ";
  if (m.isFinal) s += "final ";
  if (m.isAbstract) s += "abstract ";
  if (m.isOverride) s += "override ";
  if (m.isNative) s += "native ";
  if (m.isCoroutine) s += "coroutine ";
  return s;
}

inline std::string member(const Member& m) {
  std::string r;
  switch (m.kind) {
    case MemberKind::Field: r = "(field " + mods(m.mods) + type(m.type.get()) + " " + m.name + (m.init ? " " + expr(m.init.get()) : ""); break;
    case MemberKind::EnumConstant: r = "(const " + m.name + (m.init ? " " + expr(m.init.get()) : ""); break;
    default: {
      r = std::string(m.kind == MemberKind::Constructor ? "(ctor " : m.kind == MemberKind::Operator ? "(op " : "(method ") + mods(m.mods) +
          (m.type ? type(m.type.get()) + " " : "") + m.name + " (";
      for (std::size_t i = 0; i < m.params.size(); ++i)
        r += (i ? ", " : "") + std::string(m.params[i].isRef ? "ref " : "") + type(m.params[i].type.get()) + " " + m.params[i].name;
      r += ")";
      if (m.body) r += " " + stmt(m.body.get());
    }
  }
  return r + ")";
}

inline std::string unit(const CompilationUnit& u) {
  std::string r;
  if (!u.moduleName.empty()) {
    r += "module ";
    for (std::size_t i = 0; i < u.moduleName.size(); ++i) r += (i ? "." : "") + u.moduleName[i];
    r += "\n";
  }
  for (const auto& im : u.imports) {
    r += "import ";
    for (std::size_t i = 0; i < im.path.size(); ++i) r += (i ? "." : "") + im.path[i];
    r += "\n";
  }
  for (const auto& t : u.types) {
    r += std::string(t->kind == TypeDeclKind::Class ? "class " : t->kind == TypeDeclKind::Interface ? "interface " : "enum ") + mods(t->mods) + t->name;
    if (t->base) r += " extends " + type(t->base.get());
    for (const auto& i : t->interfaces) r += " : " + type(i.get());
    r += "\n";
    for (const auto& m : t->members) r += "  " + member(m) + "\n";
  }
  return r;
}
}  // namespace sd

struct Parsed {
  std::unique_ptr<script::CompilationUnit> unit;
  script::Diagnostics diags;
  std::string errors() const {
    std::string s;
    for (const auto& d : diags.errors()) s += d.format() + "\n";
    return s;
  }
};

inline Parsed parseText(std::string_view src) {
  Parsed p;
  p.diags.addFile("test.mins");
  p.unit = script::parse(src, 0, p.diags);
  return p;
}

// Parses `expr` as the initialiser of a field and returns the s-expression.
inline std::string exprOf(const std::string& e) {
  Parsed p = parseText("class T { int x = " + e + "; }");
  if (p.diags.hasErrors()) return "ERROR: " + p.errors();
  return sd::expr(p.unit->types.at(0)->members.at(0).init.get());
}

// Parses `stmt` as the body of a method and returns its s-expression.
inline std::string stmtOf(const std::string& s) {
  Parsed p = parseText("class T { void f() { " + s + " } }");
  if (p.diags.hasErrors()) return "ERROR: " + p.errors();
  const auto& body = *p.unit->types.at(0)->members.at(0).body;
  if (body.stmts.size() != 1) return "COUNT " + std::to_string(body.stmts.size());
  return sd::stmt(body.stmts[0].get());
}

}  // namespace mingol::test
