// SPDX-License-Identifier: GPL-2.0-only
// The asset names every Mingol tool agrees on, so a scene saved by the editor loads in the player:
//   builtin/box, builtin/sphere, builtin/plane, builtin/default      (primitives and their material)
//   <path>#mesh<m>.<p>  <path>#material<k>  <path>#default            (a glTF model at project path <path>)
//   <path>#skin<k>  <path>#skin<k>/<clip>                             (skeletons and animation clips)
//   <path>                                                            (audio files)
#pragma once

#include <memory>
#include <string>

#include "mingol/core/error.hpp"
#include "mingol/renderer/renderer.hpp"
#include "mingol/runtime/model_instance.hpp"
#include "mingol/sceneio/scene_io.hpp"

namespace mingol::sceneio {

struct BuiltinAssets {
  std::shared_ptr<renderer::Mesh> box, sphere, plane;
  std::shared_ptr<renderer::Material> material;

  // Unit box, sphere of diameter 1, 10 x 10 plane, grey material.
  static Result<BuiltinAssets> create(renderer::Renderer& renderer);
  void addTo(AssetTable& table) const;
};

// Registers meshes, materials, skeletons and clips of an uploaded model under `path`.
void addModelAssets(AssetTable& table, const std::string& path, const runtime::GpuModel& model);

}  // namespace mingol::sceneio
