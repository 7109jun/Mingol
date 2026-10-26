// SPDX-License-Identifier: GPL-2.0-only
// Internal interface shared by the declaration pass, the checker and the code generator.
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mingol/script/program.hpp"
#include "mingol/script/source.hpp"

namespace mingol::script {

// What a checked expression resolved to; stored in Expr::aux.
enum class Res : int {
  None = 0,
  Local,         // Name -> VarSym
  Field,         // Name/Member -> instance FieldSym (implicit this for Name)
  StaticField,
  TypeName,      // expression names a type (qualifier only)
  EnumConst,
  Constant,      // folded switch label
  ArrayLength,
  StringLength,
  ArrayIndex,
  StringIndex,
  OperatorIndex, // Index via operator[]
  StaticCall,
  VirtualCall,
  InterfaceCall,
  SuperCall,     // non-virtual call of a base implementation
  ClosureCall,
  CtorSuper,     // super(...) / this(...) inside a constructor
  CtorThis,
  Operator,      // Unary / Binary / Assign resolved to a user operator
  MethodValue,   // obj.method used as a function value (bound)
  StaticMethodValue,
  CoroutineCall, // call of a coroutine function: starts a coroutine
  NumConv,       // Cast: int<->float
  ToString,      // Cast: -> string
  EnumToInt,
  IntToEnum,
  RefCast,       // Cast: class/interface (checked)
  UpCast,        // Cast: statically safe, no-op
  Deref,
};

struct UnitInfo {
  CompilationUnit* unit = nullptr;
  std::string module;                // dotted
  std::vector<std::string> imports;  // dotted module names (user imports)
};

class Compiler {
 public:
  Compiler(Program& program, Diagnostics& diags) : p_(program), d_(diags) {}

  // Takes ownership of the parsed units (the prelude included). Returns false when errors were reported.
  bool compile(std::vector<std::unique_ptr<CompilationUnit>> units);

 // Internal header: the passes (declaration, checker, code generator) share the compiler's state.
  friend class CodeGen;
  Program& p_;
  Diagnostics& d_;
  std::vector<UnitInfo> units_;
  std::map<std::string, std::map<std::string, ClassSym*>> modules_;  // module -> type name -> class
  std::map<const ClassSym*, UnitInfo*> unitOf_;
  std::map<const CompilationUnit*, UnitInfo*> unitByPtr_;

  void error(SourceLoc l, std::string m) { d_.error(l, std::move(m)); }

  // ---- declaration pass (compiler_decl.cpp) ----
  void collectTypes();
  void checkModules();
  void resolveAllHeaders();
  void resolveHeader(ClassSym& c, std::vector<ClassSym*>& stack);
  void declareMembers(ClassSym& c);
  void layoutClass(ClassSym& c);
  void buildTables(ClassSym& c);
  void checkOperatorDecl(ClassSym& c, MethodSym& m);
  void bindWellKnown();
  void assignFuncIds();
  const Type* resolveType(const TypeRef& t, const UnitInfo& u, bool allowVoid);
  ClassSym* lookupClass(const std::vector<std::string>& path, const UnitInfo& u, SourceLoc loc, bool report);
  ClassSym* lookupSimple(const std::string& name, const UnitInfo& u, SourceLoc loc, bool report);
  std::map<const ClassSym*, int> headerState_;  // 0 new, 1 in progress, 2 done

  // ---- checker ----
  void checkAllBodies();

  // ---- helpers shared by the passes ----
 bool assignable(const Type* from, const Type* to) const;
  const Type* objectType() const;
  const Type* exceptionType() const;
};

// Parse + compile in one call used by the public API (compile.cpp).

}  // namespace mingol::script
