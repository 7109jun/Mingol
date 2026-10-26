// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/script/types.hpp"

#include "mingol/script/symbols.hpp"

namespace mingol::script {

std::string Type::name() const {
  switch (kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Int: return "int";
    case TypeKind::Float: return "float";
    case TypeKind::Bool: return "bool";
    case TypeKind::String: return "string";
    case TypeKind::Null: return "null";
    case TypeKind::Class:
    case TypeKind::Interface:
    case TypeKind::Enum: return cls != nullptr ? cls->qualifiedName : "<class>";
    case TypeKind::Array: return elem->name() + "[]";
    case TypeKind::Pointer: return elem->name() + "*";
    case TypeKind::Func: {
      std::string s = "fn(";
      for (std::size_t i = 0; i < params.size(); ++i) s += (i ? ", " : "") + std::string(params[i].isRef ? "ref " : "") + params[i].type->name();
      return s + "): " + ret->name();
    }
  }
  return "?";
}

const Type* TypeTable::make(Type t) {
  all_.push_back(std::make_unique<Type>(std::move(t)));
  return all_.back().get();
}

TypeTable::TypeTable() {
  auto prim = [&](TypeKind k) {
    Type t;
    t.kind = k;
    return make(std::move(t));
  };
  void_ = prim(TypeKind::Void);
  int_ = prim(TypeKind::Int);
  float_ = prim(TypeKind::Float);
  bool_ = prim(TypeKind::Bool);
  string_ = prim(TypeKind::String);
  null_ = prim(TypeKind::Null);
}

const Type* TypeTable::arrayOf(const Type* elem) {
  auto it = arrays_.find(elem);
  if (it != arrays_.end()) return it->second;
  Type t;
  t.kind = TypeKind::Array;
  t.elem = elem;
  return arrays_[elem] = make(std::move(t));
}

const Type* TypeTable::pointerTo(const Type* elem) {
  auto it = pointers_.find(elem);
  if (it != pointers_.end()) return it->second;
  Type t;
  t.kind = TypeKind::Pointer;
  t.elem = elem;
  return pointers_[elem] = make(std::move(t));
}

const Type* TypeTable::func(std::vector<FuncParam> params, const Type* ret) {
  std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(ret)) + ":";
  for (const FuncParam& p : params) key += std::to_string(reinterpret_cast<std::uintptr_t>(p.type)) + (p.isRef ? "r," : ",");
  auto it = funcs_.find(key);
  if (it != funcs_.end()) return it->second;
  Type t;
  t.kind = TypeKind::Func;
  t.params = std::move(params);
  t.ret = ret;
  return funcs_[key] = make(std::move(t));
}

const Type* TypeTable::classType(ClassSym* cls) {
  auto it = classes_.find(cls);
  if (it != classes_.end()) return it->second;
  Type t;
  t.kind = cls->kind == TypeDeclKind::Interface ? TypeKind::Interface : cls->kind == TypeDeclKind::Enum ? TypeKind::Enum : TypeKind::Class;
  t.cls = cls;
  return classes_[cls] = make(std::move(t));
}

}  // namespace mingol::script
