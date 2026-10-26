// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/script/compile.hpp"

#include "codegen.hpp"
#include "compiler_internal.hpp"
#include "mingol/script/parser.hpp"

namespace mingol::script {

ClassSym* Program::findClass(const std::string& qualifiedName) const {
  for (const auto& c : classes) {
    if (c->qualifiedName == qualifiedName) return c.get();
  }
  return nullptr;
}

const MethodSym* Program::findMethod(const std::string& qualifiedClass, const std::string& name) const {
  const ClassSym* c = findClass(qualifiedClass);
  for (; c != nullptr; c = c->base) {
    for (const auto& m : c->methods) {
      if (m->mkind == MethodKind::Method && m->name == name) return m.get();
    }
  }
  return nullptr;
}

std::unique_ptr<Program> compile(const std::vector<SourceInput>& sources, Diagnostics& diags) {
  auto program = std::make_unique<Program>();
  std::vector<std::string> names;
  names.push_back("<prelude>");
  for (const auto& s : sources) names.push_back(s.name);
  diags.setFiles(names);
  program->files = names;

  std::vector<std::unique_ptr<CompilationUnit>> units;
  units.push_back(parse(preludeSource(), 0, diags));
  for (std::size_t i = 0; i < sources.size(); ++i) units.push_back(parse(sources[i].text, static_cast<std::uint32_t>(i + 1), diags));
  if (diags.hasErrors()) return nullptr;

  Compiler compiler(*program, diags);
  if (!compiler.compile(std::move(units))) return nullptr;
  CodeGen gen(compiler);
  gen.run();
  return program;
}

}  // namespace mingol::script
