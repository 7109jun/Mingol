// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cmath>
#include <memory>
#include <string>

#include "mingol/scene/scene.hpp"
#include "mingol/scripting/script_component.hpp"
#include "mingol/scripting/script_host.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

inline std::unique_ptr<scripting::ScriptHost> makeHost(const std::string& source, scripting::HostDesc desc = {}) {
  auto h = scripting::ScriptHost::create({{"test.mins", source}}, std::move(desc));
  if (!h.ok()) {
    mingol::test::fail(__FILE__, __LINE__, "ScriptHost::create failed: " + h.error().message);
    return nullptr;
  }
  return std::move(h).value();
}

// Reads the static `Trace.s` string of a test script.
inline std::string traceOf(scripting::ScriptHost& host) {
  const auto* cls = host.program().findClass("Trace");
  const auto* f = cls != nullptr ? cls->findField("s") : nullptr;
  if (f == nullptr) return "<no Trace.s>";
  return std::string(host.vm().stringValue(host.vm().getStatic(*f)));
}

}  // namespace mingol::test

