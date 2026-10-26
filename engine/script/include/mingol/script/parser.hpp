// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string_view>

#include "mingol/script/ast.hpp"
#include "mingol/script/source.hpp"

namespace mingol::script {

// Parses one source file. Syntax errors are reported to `diags`; the parser recovers at statement
// and member boundaries so one run reports several errors. The returned unit is always non-null but
// must not be used for code generation when diags.hasErrors(). Nesting deeper than 200 levels is an
// error (protects the compiler's own stack).
std::unique_ptr<CompilationUnit> parse(std::string_view source, std::uint32_t fileIndex, Diagnostics& diags);

}  // namespace mingol::script
