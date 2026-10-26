// SPDX-License-Identifier: GPL-2.0-only
// Syntax tree produced by the parser and annotated in place by the type checker.
//
// Nodes are plain structs with a `kind` tag (no virtual dispatch): the compiler passes switch over
// the tag. Fields that only some kinds use are documented next to the kind.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mingol/script/token.hpp"

namespace mingol::script {

struct Type;         // semantic types (types.hpp)
struct Symbol;       // resolved declarations (sema)

// ---- syntactic types --------------------------------------------------------------------------

struct TypeRef;
using TypeRefPtr = std::unique_ptr<TypeRef>;

struct TypeRef {
  enum class Kind : std::uint8_t {
    Prim,     // int float bool string void: `prim`
    Named,    // possibly qualified: `path` ("a", "b", "C")
    Array,    // `elem[]`: elem
    Pointer,  // `elem*`: elem
    Func,     // `fn(params): ret`: params / paramRef / ret
  };
  Kind kind = Kind::Named;
  SourceLoc loc;
  Tok prim = Tok::KwInt;
  std::vector<std::string> path;
  TypeRefPtr elem;
  std::vector<TypeRefPtr> params;
  std::vector<bool> paramRef;
  TypeRefPtr ret;
};

// ---- expressions ------------------------------------------------------------------------------

enum class ExprKind : std::uint8_t {
  IntLit,     // ival
  FloatLit,   // fval
  StringLit,  // text
  BoolLit,    // ival (0/1)
  NullLit,
  This,
  Name,       // text
  Member,     // kids[0] . text        (kids[0] may be Super)
  Super,      // only as the object of Member / Call
  Index,      // kids[0] [ kids[1] ]
  Call,       // kids[0] ( kids[1..] )  - callee is Name, Member, Super (constructor chaining) or any function value
  New,        // new typeRef ( kids... )
  NewArray,   // new typeRef [ kids[0] ]   (typeRef is the element type)
  ArrayLit,   // [ kids... ]
  Unary,      // op, kids[0]
  Binary,     // op, kids[0], kids[1]   (also && and ||)
  Assign,     // op (Assign or a compound-assign token), kids[0] target, kids[1] value
  Ternary,    // kids[0] ? kids[1] : kids[2]
  Cast,       // kids[0] as typeRef
  Is,         // kids[0] is typeRef
  Typeof,     // typeof(kids[0]) or typeof(typeRef) when kids is empty
  Lambda,     // lambda
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Param {
  SourceLoc loc;
  TypeRefPtr type;  // null for an untyped lambda parameter
  std::string name;
  bool isRef = false;
};

struct LambdaData {
  std::vector<Param> params;
  ExprPtr bodyExpr;  // exactly one of bodyExpr / bodyBlock
  StmtPtr bodyBlock;
};

// Unary operators are stored as tokens; ++/-- need to distinguish prefix from postfix.
enum class UnaryOp : std::uint8_t { Neg, Plus, Not, BitNot, AddrOf, Deref, PreInc, PreDec, PostInc, PostDec };

struct Expr {
  ExprKind kind = ExprKind::NullLit;
  SourceLoc loc;
  std::string text;
  std::int64_t ival = 0;
  double fval = 0;
  Tok op = Tok::End;           // Binary / Assign
  UnaryOp uop = UnaryOp::Neg;  // Unary
  std::vector<ExprPtr> kids;
  TypeRefPtr typeRef;
  std::unique_ptr<LambdaData> lambda;

  // ---- filled by the type checker ----
  const Type* type = nullptr;
  Symbol* sym = nullptr;       // resolved variable/field/method/class
  int aux = 0;                 // checker-specific (e.g. which conversion to apply)
  void* extra = nullptr;       // checker-specific payload (owned elsewhere)
};

// ---- statements -------------------------------------------------------------------------------

enum class StmtKind : std::uint8_t {
  Block,     // stmts
  VarDecl,   // declType (null = `var`), name, exprs[0] initialiser (may be absent)
  ExprStmt,  // exprs[0]
  If,        // exprs[0] cond, stmts[0] then, stmts[1] else (optional)
  While,     // exprs[0], stmts[0]
  DoWhile,   // stmts[0], exprs[0]
  For,       // stmts[0] init (optional: VarDecl/ExprStmt), exprs[0] cond (optional), exprs[1] step (optional), stmts[1] body
  ForEach,   // declType (null = var), name, exprs[0] iterable, stmts[0] body
  Switch,    // exprs[0] subject, cases
  Break,
  Continue,
  Return,    // exprs[0] optional
  Throw,     // exprs[0]
  Try,       // stmts[0] body, catches, stmts[1] finally (optional)
  Yield,     // exprs[0] optional
};

struct SwitchCase {
  SourceLoc loc;
  std::vector<ExprPtr> labels;  // empty = default
  bool isDefault = false;
  std::vector<StmtPtr> body;
};

struct CatchClause {
  SourceLoc loc;
  TypeRefPtr type;
  std::string name;
  StmtPtr body;
  Symbol* sym = nullptr;
};

struct Stmt {
  StmtKind kind = StmtKind::Block;
  SourceLoc loc;
  SourceLoc endLoc;  // closing brace of a block
  std::string name;
  TypeRefPtr declType;
  std::vector<ExprPtr> exprs;
  std::vector<StmtPtr> stmts;
  std::vector<SwitchCase> cases;
  std::vector<CatchClause> catches;
  Symbol* sym = nullptr;  // declared variable
  const Type* type = nullptr;
  void* extra = nullptr;
};

// ---- declarations -----------------------------------------------------------------------------

enum class Access : std::uint8_t { Public, Protected, Private };

struct Modifiers {
  Access access = Access::Public;
  bool explicitAccess = false;
  bool isStatic = false;
  bool isFinal = false;
  bool isAbstract = false;
  bool isOverride = false;
  bool isNative = false;
  bool isCoroutine = false;
};

enum class MemberKind : std::uint8_t { Field, Method, Constructor, Operator, EnumConstant };

struct Member {
  MemberKind kind = MemberKind::Field;
  SourceLoc loc;
  Modifiers mods;
  std::string name;          // field/method/enum constant name; "<init>" for constructors; "operator+" ... for operators
  TypeRefPtr type;           // field type or method return type (null for constructors)
  ExprPtr init;              // field initialiser / enum constant explicit value
  std::vector<Param> params;
  StmtPtr body;              // method / constructor body; null for abstract, native and interface methods
  Tok opToken = Tok::End;    // Operator members; Tok::LBracket stands for operator[]
  Symbol* sym = nullptr;
};

enum class TypeDeclKind : std::uint8_t { Class, Interface, Enum };

struct TypeDecl {
  TypeDeclKind kind = TypeDeclKind::Class;
  SourceLoc loc;
  std::string name;
  Modifiers mods;
  TypeRefPtr base;                    // extends (class) -- interfaces list their parents in `interfaces`
  std::vector<TypeRefPtr> interfaces; // implements (class) / extends (interface)
  std::vector<Member> members;
  Symbol* sym = nullptr;
};
using TypeDeclPtr = std::unique_ptr<TypeDecl>;

struct Import {
  SourceLoc loc;
  std::vector<std::string> path;
};

struct CompilationUnit {
  std::uint32_t file = 0;
  std::vector<std::string> moduleName;  // empty = the default (anonymous) module
  SourceLoc moduleLoc;
  std::vector<Import> imports;
  std::vector<TypeDeclPtr> types;
};

}  // namespace mingol::script
