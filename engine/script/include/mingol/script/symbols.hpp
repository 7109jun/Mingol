// SPDX-License-Identifier: GPL-2.0-only
// Declarations resolved by the compiler: classes, fields, methods, variables. They stay alive in the
// compiled Program and double as the runtime metadata used by reflection and native binding.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mingol/script/ast.hpp"
#include "mingol/script/types.hpp"

namespace mingol::script {

enum class SymKind : std::uint8_t { Var, Field, Method, Class, EnumConst };

struct Symbol {
  SymKind kind;
  std::string name;
  SourceLoc loc;
  const Type* type = nullptr;
  explicit Symbol(SymKind k) : kind(k) {}
  virtual ~Symbol() = default;
};

struct FuncInfo;

struct VarSym final : Symbol {
  VarSym() : Symbol(SymKind::Var) {}
  bool isParam = false;
  bool isRefParam = false;   // the slot holds a reference to the caller's storage
  bool isFinal = false;
  FuncInfo* owner = nullptr; // the function (method or lambda) that declares it
  int slot = -1;             // frame slot, assigned by code generation
  bool captured = false;     // used by a nested lambda
  bool reassigned = false;   // assigned anywhere other than its declaration
  bool addressTaken = false; // &x, or passed to a `ref` parameter
  bool initializing = false; // its own initialiser is being checked
  // Variables shared between closures, or whose address escapes, live in a heap box.
  bool boxed() const noexcept { return !isRefParam && (addressTaken || (captured && reassigned)); }
};

struct ClassSym;

struct FieldSym final : Symbol {
  FieldSym() : Symbol(SymKind::Field) {}
  ClassSym* owner = nullptr;
  bool isStatic = false;
  bool isFinal = false;
  Access access = Access::Public;
  int index = -1;            // instance: slot in the object (base class fields first); static: global index
  Member* decl = nullptr;
};

struct ParamInfo {
  std::string name;
  const Type* type = nullptr;
  bool isRef = false;
};

enum class MethodKind : std::uint8_t { Method, Constructor, Operator };

struct MethodSym final : Symbol {
  MethodSym() : Symbol(SymKind::Method) {}
  ClassSym* owner = nullptr;
  MethodKind mkind = MethodKind::Method;
  Modifiers mods;
  Tok opToken = Tok::End;
  std::vector<ParamInfo> params;
  const Type* ret = nullptr;      // void for constructors
  Member* decl = nullptr;
  int funcId = -1;                // index into Program::funcs
  FuncInfo* info = nullptr;       // set by the checker when the method has a body
  int vtableSlot = -1;            // virtual instance methods
  int interfaceKey = -1;          // interface methods: unique key for dispatch
  MethodSym* overrides = nullptr; // base-class method this overrides
  bool isAbstract() const noexcept { return mods.isAbstract || (owner != nullptr && decl != nullptr && decl->body == nullptr && !mods.isNative); }
  bool isStatic() const noexcept { return mods.isStatic; }
  std::string signature() const;  // "name(int, ref float)"
};

struct EnumConstSym final : Symbol {
  EnumConstSym() : Symbol(SymKind::EnumConst) {}
  ClassSym* owner = nullptr;
  std::int64_t value = 0;
};

struct ClassSym final : Symbol {
  ClassSym() : Symbol(SymKind::Class) {}
  std::string qualifiedName;
  std::string moduleName;       // dotted; empty for the default module
  TypeDeclKind kind = TypeDeclKind::Class;
  Modifiers mods;
  TypeDecl* decl = nullptr;
  const CompilationUnit* unit = nullptr;
  int id = -1;                  // index into Program::classes
  ClassSym* base = nullptr;
  std::vector<ClassSym*> interfaces;  // directly declared
  std::vector<std::unique_ptr<FieldSym>> fields;       // own fields (static and instance)
  std::vector<std::unique_ptr<MethodSym>> methods;     // own methods incl. constructors and operators
  std::vector<std::unique_ptr<EnumConstSym>> enumConsts;
  int instanceFieldCount = 0;   // including inherited
  int baseFieldCount = 0;
  std::vector<MethodSym*> vtable;              // virtual dispatch table (inherited entries first)
  std::map<int, MethodSym*> itable;            // interface method key -> implementation
  std::vector<ClassSym*> allInterfaces;        // transitive, including inherited
  int depth = 0;                               // inheritance depth (Object = 0)
  bool resolved = false;                       // headers resolved
  FuncInfo* fieldInit = nullptr;               // synthesised: instance field initialisers (runs after the base constructor)
  FuncInfo* staticInit = nullptr;              // synthesised: static field initialisers
  int fieldInitFunc = -1, staticInitFunc = -1; // Program::funcs indices
  bool isAbstractType() const noexcept { return mods.isAbstract || kind == TypeDeclKind::Interface; }

  const Type* thisType = nullptr;
  bool isSubclassOf(const ClassSym* other) const noexcept;  // reflexive; classes only
  bool implements(const ClassSym* iface) const noexcept;    // transitive incl. inherited
  bool isAssignableTo(const ClassSym* other) const noexcept;
  FieldSym* findField(const std::string& n, bool includeInherited = true);
  const FieldSym* findField(const std::string& n) const;
};

// A function body being compiled: a method, constructor, operator, static initialiser, or lambda.
struct FuncInfo {
  FuncInfo* parent = nullptr;   // enclosing function for lambdas
  MethodSym* method = nullptr;  // null for lambdas and initialisers
  ClassSym* cls = nullptr;
  bool isStatic = false;
  bool isCoroutine = false;
  bool isLambda = false;
  const Type* ret = nullptr;    // may be inferred for lambdas
  bool retInferred = false;
  std::vector<VarSym*> captures;  // variables of enclosing functions used here (in capture-slot order)
  bool capturesThis = false;
  int funcId = -1;
  std::vector<std::unique_ptr<VarSym>> vars;  // owns every variable declared in this function
};

}  // namespace mingol::script
