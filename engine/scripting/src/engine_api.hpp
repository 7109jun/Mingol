// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "mingol/core/error.hpp"
#include "mingol/script/vm.hpp"

namespace mingol::scripting {
// Binds the natives declared by engineApiSource() to `vm`.
Result<void> bindEngineNatives(script::VM& vm);
}  // namespace mingol::scripting
