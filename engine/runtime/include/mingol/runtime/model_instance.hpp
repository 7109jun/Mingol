// SPDX-License-Identifier: GPL-2.0-only
// Turns loaded glTF data (assets::ModelData) into GPU resources and into a GameObject hierarchy.
#pragma once

#include <memory>
#include <vector>

#include "mingol/assets/model.hpp"
#include "mingol/renderer/renderer.hpp"
#include "mingol/rhi/device.hpp"
#include "mingol/scene/components.hpp"

namespace mingol::runtime {

// GPU-side resources of a model; shareable between any number of instances.
struct GpuModel {
  std::vector<std::vector<std::shared_ptr<const renderer::Mesh>>> meshes;  // [ModelMesh][primitive]
  std::vector<std::shared_ptr<const renderer::Material>> materials;        // [ModelMaterial]
  std::shared_ptr<const renderer::Material> defaultMaterial;               // for primitives without one
  // Shared skeletal data, one entry per ModelSkin (filled by uploadModel; instances share it, so a
  // scene can reference it by name). instantiateModel builds private copies when these are empty.
  std::vector<std::shared_ptr<const renderer::Skeleton>> skeletons;
  std::vector<std::vector<std::shared_ptr<const renderer::AnimationClip>>> clips;
};

// Uploads textures (base colour/emissive as sRGB, the rest linear; full mip chains), builds
// materials and meshes. Blocks until the uploads finish.
Result<GpuModel> uploadModel(rhi::Device& device, renderer::Renderer& renderer, const assets::ModelData& model);

struct ModelInstance {
  scene::GameObject* root = nullptr;
  std::vector<scene::GameObject*> nodes;  // one entry per ModelNode (same index); never null
};

// Creates `name` under `parent` (or at the scene root) with one GameObject per glTF node. Mesh
// nodes get a MeshRenderer per primitive; skinned nodes also get an Animator holding the skeleton
// and all clips (nothing plays until you call Animator::play). Skinned mesh objects are placed
// directly under the instance root with an identity transform, as the glTF specification requires.
Result<ModelInstance> instantiateModel(scene::Scene& scene, const assets::ModelData& model, const GpuModel& gpu,
                                       const std::string& name = "model", scene::GameObject* parent = nullptr);

}  // namespace mingol::runtime
