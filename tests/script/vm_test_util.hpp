// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include <string>

#include "mingol/script/compile.hpp"
#include "mingol/script/vm.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

using namespace mingol::script;

// A compiled program plus a VM with static initialisers run.
struct Loaded {
  std::unique_ptr<Program> program;
  std::unique_ptr<VM> vm;
  std::string compileErrors;
  std::string initError;
  bool ok() const { return program != nullptr && vm != nullptr && initError.empty(); }
};

inline Loaded loadSources(const std::vector<SourceInput>& sources, VMOptions opt = {}) {
  Loaded l;
  Diagnostics d;
  l.program = compile(sources, d);
  for (const auto& e : d.errors()) l.compileErrors += e.format() + "\n";
  if (!l.program) return l;
  l.vm = std::make_unique<VM>(*l.program, std::move(opt));
  ScriptException ex;
  if (!l.vm->initialize(ex)) l.initError = ex.describe();
  return l;
}

inline Loaded load(const std::string& src, VMOptions opt = {}) { return loadSources({{"test.mins", src}}, std::move(opt)); }

struct Outcome {
  bool ok = false;
  Value value{};
  ScriptException ex;
};

inline Outcome callStatic(Loaded& l, const std::string& cls, const std::string& method, std::vector<Value> args = {}) {
  Outcome o;
  const MethodSym* m = l.program->findMethod(cls, method);
  if (m == nullptr) {
    o.ex = {"test", "no such method " + cls + "." + method, ""};
    return o;
  }
  o.ok = l.vm->call(*m, makeNull(), args, o.value, o.ex);
  return o;
}

// Compiles `src`, runs `Main.f()` and returns the outcome; compile errors are reported as test failures.
inline Outcome runMain(Loaded& l) {
  if (!l.ok()) {
    Outcome o;
    o.ex = {"test", l.compileErrors + l.initError, ""};
    return o;
  }
  return callStatic(l, "Main", "f");
}

}  // namespace mingol::test

// Evaluates Main.f() of `src` and compares with an expected int.
#define CHECK_INT(src, expected)                                                                          \
  do {                                                                                                    \
    auto mtL = ::mingol::test::load(src);                                                                 \
    auto mtO = ::mingol::test::runMain(mtL);                                                              \
    if (!mtO.ok) ::mingol::test::fail(__FILE__, __LINE__, "program failed: " + mtO.ex.describe() + "\n--- source:\n" + std::string(src)); \
    else if (mtO.value.tag != MS_TAG_INT || mtO.value.u.i != static_cast<std::int64_t>(expected))         \
      ::mingol::test::fail(__FILE__, __LINE__, "expected " #expected " got " + std::to_string(mtO.value.u.i) + " (tag " + std::to_string(mtO.value.tag) + ")\n--- source:\n" + std::string(src)); \
  } while (false)

#define CHECK_FLOAT(src, expected)                                                                        \
  do {                                                                                                    \
    auto mtL = ::mingol::test::load(src);                                                                 \
    auto mtO = ::mingol::test::runMain(mtL);                                                              \
    if (!mtO.ok) ::mingol::test::fail(__FILE__, __LINE__, "program failed: " + mtO.ex.describe() + "\n--- source:\n" + std::string(src)); \
    else if (mtO.value.tag != MS_TAG_FLOAT || mtO.value.u.f != (expected))                                \
      ::mingol::test::fail(__FILE__, __LINE__, "expected " #expected " got " + std::to_string(mtO.value.u.f) + "\n--- source:\n" + std::string(src)); \
  } while (false)

#define CHECK_BOOL(src, expected)                                                                         \
  do {                                                                                                    \
    auto mtL = ::mingol::test::load(src);                                                                 \
    auto mtO = ::mingol::test::runMain(mtL);                                                              \
    if (!mtO.ok) ::mingol::test::fail(__FILE__, __LINE__, "program failed: " + mtO.ex.describe() + "\n--- source:\n" + std::string(src)); \
    else if (mtO.value.tag != MS_TAG_BOOL || (mtO.value.u.i != 0) != (expected))                          \
      ::mingol::test::fail(__FILE__, __LINE__, "unexpected bool result\n--- source:\n" + std::string(src)); \
  } while (false)

#define CHECK_STR(src, expected)                                                                          \
  do {                                                                                                    \
    auto mtL = ::mingol::test::load(src);                                                                 \
    auto mtO = ::mingol::test::runMain(mtL);                                                              \
    if (!mtO.ok) ::mingol::test::fail(__FILE__, __LINE__, "program failed: " + mtO.ex.describe() + "\n--- source:\n" + std::string(src)); \
    else if (!mtL.vm->isString(mtO.value) || mtL.vm->stringValue(mtO.value) != (expected))                \
      ::mingol::test::fail(__FILE__, __LINE__, "expected \"" + std::string(expected) + "\" got \"" + std::string(mtL.vm->stringValue(mtO.value)) + "\"\n--- source:\n" + std::string(src)); \
  } while (false)

// Expects Main.f() to throw an exception whose qualified class name is `type`.
#define CHECK_THROWS(src, typeName)                                                                           \
  do {                                                                                                    \
    auto mtL = ::mingol::test::load(src);                                                                 \
    auto mtO = ::mingol::test::runMain(mtL);                                                              \
    if (mtO.ok) ::mingol::test::fail(__FILE__, __LINE__, "expected an exception " typeName " but the program returned\n--- source:\n" + std::string(src)); \
    else if (mtO.ex.type != (typeName)) ::mingol::test::fail(__FILE__, __LINE__, "expected " typeName " got " + mtO.ex.describe() + "\n--- source:\n" + std::string(src)); \
  } while (false)
