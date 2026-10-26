// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/script/symbols.hpp"

namespace mingol::script {

std::string MethodSym::signature() const {
  std::string s = name + "(";
  for (std::size_t i = 0; i < params.size(); ++i) s += (i ? ", " : "") + std::string(params[i].isRef ? "ref " : "") + params[i].type->name();
  return s + ")";
}

bool ClassSym::isSubclassOf(const ClassSym* other) const noexcept {
  for (const ClassSym* c = this; c != nullptr; c = c->base) {
    if (c == other) return true;
  }
  return false;
}

bool ClassSym::implements(const ClassSym* iface) const noexcept {
  for (const ClassSym* i : allInterfaces) {
    if (i == iface) return true;
  }
  return false;
}

bool ClassSym::isAssignableTo(const ClassSym* other) const noexcept {
  if (other == this) return true;
  if (other->kind == TypeDeclKind::Interface) return implements(other);
  return isSubclassOf(other);
}

FieldSym* ClassSym::findField(const std::string& n, bool includeInherited) {
  for (ClassSym* c = this; c != nullptr; c = includeInherited ? c->base : nullptr) {
    for (auto& f : c->fields) {
      if (f->name == n) return f.get();
    }
  }
  return nullptr;
}

const FieldSym* ClassSym::findField(const std::string& n) const { return const_cast<ClassSym*>(this)->findField(n, true); }

}  // namespace mingol::script
