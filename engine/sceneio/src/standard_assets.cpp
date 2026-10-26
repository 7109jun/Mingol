// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/sceneio/standard_assets.hpp"

#include "mingol/renderer/mesh.hpp"

namespace mingol::sceneio {

Result<BuiltinAssets> BuiltinAssets::create(renderer::Renderer& renderer) {
  BuiltinAssets b;
  MINGOL_TRY_ASSIGN(b.box, renderer.createMesh(renderer::makeBox({0.5F, 0.5F, 0.5F})));
  MINGOL_TRY_ASSIGN(b.sphere, renderer.createMesh(renderer::makeUvSphere(0.5F, 32, 16)));
  MINGOL_TRY_ASSIGN(b.plane, renderer.createMesh(renderer::makePlane(10.0F, 10.0F)));
  renderer::MaterialDesc md;
  md.baseColor = {0.8F, 0.8F, 0.8F, 1.0F};
  md.metallic = 0.0F;
  md.roughness = 0.6F;
  MINGOL_TRY_ASSIGN(b.material, renderer.createMaterial(md));
  return b;
}

void BuiltinAssets::addTo(AssetTable& t) const {
  if (box == nullptr) return;
  t.addMesh("builtin/box", box);
  t.addMesh("builtin/sphere", sphere);
  t.addMesh("builtin/plane", plane);
  t.addMaterial("builtin/default", material);
}

void addModelAssets(AssetTable& t, const std::string& path, const runtime::GpuModel& m) {
  for (std::size_t mi = 0; mi < m.meshes.size(); ++mi)
    for (std::size_t pi = 0; pi < m.meshes[mi].size(); ++pi)
      t.addMesh(path + "#mesh" + std::to_string(mi) + "." + std::to_string(pi), m.meshes[mi][pi]);
  for (std::size_t k = 0; k < m.materials.size(); ++k) t.addMaterial(path + "#material" + std::to_string(k), m.materials[k]);
  if (m.defaultMaterial) t.addMaterial(path + "#default", m.defaultMaterial);
  for (std::size_t k = 0; k < m.skeletons.size(); ++k) {
    t.addSkeleton(path + "#skin" + std::to_string(k), m.skeletons[k]);
    if (k < m.clips.size())
      for (const auto& c : m.clips[k]) t.addAnimationClip(path + "#skin" + std::to_string(k) + "/" + c->name(), c);
  }
}

}  // namespace mingol::sceneio
