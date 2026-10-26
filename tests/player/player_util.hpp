// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "editor_util.hpp"
#include "mingol/core/fs.hpp"
#include "mingol/package/archive.hpp"
#include "mingol/player/game_app.hpp"
#include "mingol/scripting/script_component.hpp"

namespace mingol::test {

// Builds a project with the editor document: Hero script, a camera/light scene, object "H" with the script,
// and (with a renderer) a box. Returns the .mindev path.
inline std::filesystem::path buildProject(const TempDir& dir, rhi::Device* dev, renderer::Renderer* rnd) {
  writeText(dir.path / "scripts" / "hero.mins", kHeroScript);
  auto d = editor::EditorDocument::create(dev, rnd);
  if (!d.ok()) { fail(__FILE__, __LINE__, "document: " + d.error().describe()); return {}; }
  auto& doc = *d.value();
  if (auto r = doc.createProject(dir.path, "Demo"); !r.ok()) { fail(__FILE__, __LINE__, "createProject: " + r.error().describe()); return {}; }
  auto* h = doc.createEmpty("H");
  auto sc = doc.addScript(h, "Hero");
  if (!sc.ok() || !sc.value()->setInt("hp", 42).ok()) { fail(__FILE__, __LINE__, "script setup"); return {}; }
  if (rnd != nullptr) {
    auto box = doc.createPrimitive(editor::PrimitiveKind::Box);
    if (!box.ok()) { fail(__FILE__, __LINE__, "box"); return {}; }
  }
  if (auto r = doc.saveScene(); !r.ok()) { fail(__FILE__, __LINE__, "saveScene: " + r.error().describe()); return {}; }
  return dir.path / "Demo.mindev";
}

}  // namespace mingol::test
