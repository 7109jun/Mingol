// SPDX-License-Identifier: GPL-2.0-only
// Public entry point of the Mingol Script compiler.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mingol/script/program.hpp"
#include "mingol/script/source.hpp"

namespace mingol::script {

struct SourceInput {
  std::string name;  // shown in diagnostics (usually the file path)
  std::string text;
};

// The language prelude (module `mingol`): Object, Exception and the runtime error classes, Coroutine,
// reflection (Type/FieldInfo/MethodInfo) and the box classes. It is compiled together with every program.
const char* preludeSource() noexcept;

// Compiles `sources` (plus the prelude) into an immutable Program. Returns null when there are errors,
// which are reported through `diags` (at most Diagnostics::kMaxErrors).
std::unique_ptr<Program> compile(const std::vector<SourceInput>& sources, Diagnostics& diags);

}  // namespace mingol::script
