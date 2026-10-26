// SPDX-License-Identifier: GPL-2.0-only
// Semantic types. Every distinct type is interned in a TypeTable, so types are compared by pointer.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mingol::script {

struct ClassSym;

enum class TypeKind : std::uint8_t { Void, Int, Float, Bool, String, Null, Class, Interface, Enum, Array, Pointer, Func };

struct Type;

struct FuncParam {
  const Type* type = nullptr;
  bool isRef = false;
  friend bool operator==(const FuncParam& a, const FuncParam& b) { return a.type == b.type && a.isRef == b.isRef; }
};

struct Type {
  TypeKind kind = TypeKind::Void;
  ClassSym* cls = nullptr;          // Class / Interface / Enum
  const Type* elem = nullptr;       // Array / Pointer
  std::vector<FuncParam> params;    // Func
  const Type* ret = nullptr;        // Func

  bool isNumeric() const noexcept { return kind == TypeKind::Int || kind == TypeKind::Float; }
  bool isRefType() const noexcept {  // has null as a value
    return kind == TypeKind::Class || kind == TypeKind::Interface || kind == TypeKind::Array || kind == TypeKind::Pointer ||
           kind == TypeKind::Func || kind == TypeKind::Null;
  }
  bool isClassLike() const noexcept { return kind == TypeKind::Class || kind == TypeKind::Interface; }
  std::string name() const;
};

class TypeTable {
 public:
  TypeTable();
  TypeTable(const TypeTable&) = delete;
  TypeTable& operator=(const TypeTable&) = delete;

  const Type* voidType() const { return void_; }
  const Type* intType() const { return int_; }
  const Type* floatType() const { return float_; }
  const Type* boolType() const { return bool_; }
  const Type* stringType() const { return string_; }
  const Type* nullType() const { return null_; }

  const Type* arrayOf(const Type* elem);
  const Type* pointerTo(const Type* elem);
  const Type* func(std::vector<FuncParam> params, const Type* ret);
  const Type* classType(ClassSym* cls);  // Class, Interface or Enum depending on cls->kind

 private:
  const Type* make(Type t);
  std::vector<std::unique_ptr<Type>> all_;
  const Type *void_, *int_, *float_, *bool_, *string_, *null_;
  std::map<const Type*, const Type*> arrays_, pointers_;
  std::map<std::string, const Type*> funcs_;
  std::map<ClassSym*, const Type*> classes_;
};

}  // namespace mingol::script
