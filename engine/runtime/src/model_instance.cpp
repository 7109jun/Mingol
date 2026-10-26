// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/runtime/model_instance.hpp"

#include <map>
#include <utility>

#include "mingol/core/log.hpp"

namespace mingol::runtime {
namespace {

using TexturePtr = std::shared_ptr<rhi::Texture>;

// One GPU texture per (image, colour space) pair.
class TextureCache {
 public:
  TextureCache(rhi::Device& d, const assets::ModelData& m) : device_(d), model_(m) {}

  Result<TexturePtr> get(int image, bool srgb) {
    if (image < 0) return TexturePtr{};
    if (static_cast<std::size_t>(image) >= model_.images.size())
      return makeError(ErrorCode::OutOfRange, "image index {} out of range", image);
    const auto key = std::make_pair(image, srgb);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    const assets::Image& img = model_.images[static_cast<std::size_t>(image)];
    rhi::TextureDesc td;
    td.width = img.width;
    td.height = img.height;
    td.mipLevels = rhi::fullMipCount(img.width, img.height);
    td.format = srgb ? rhi::Format::RGBA8Srgb : rhi::Format::RGBA8Unorm;
    td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc;
    td.debugName = "model-image-" + std::to_string(image);
    MINGOL_TRY_ASSIGN(auto tex, device_.createTextureWithData(td, img.rgba, true));
    cache_.emplace(key, tex);
    return TexturePtr(tex);
  }

 private:
  rhi::Device& device_;
  const assets::ModelData& model_;
  std::map<std::pair<int, bool>, TexturePtr> cache_;
};

}  // namespace

Result<GpuModel> uploadModel(rhi::Device& device, renderer::Renderer& renderer, const assets::ModelData& model) {
  GpuModel out;
  TextureCache textures(device, model);

  renderer::MaterialDesc def;
  def.metallic = 0.0F;
  def.roughness = 0.8F;
  MINGOL_TRY_ASSIGN(auto defMat, renderer.createMaterial(def));
  out.defaultMaterial = std::move(defMat);

  for (const assets::ModelMaterial& m : model.materials) {
    renderer::MaterialDesc d;
    d.baseColor = m.baseColor;
    d.metallic = m.metallic;
    d.roughness = m.roughness;
    d.emissive = m.emissive;
    d.normalScale = m.normalScale;
    d.alphaMode = m.alphaMode;
    d.doubleSided = m.doubleSided;
    MINGOL_TRY_ASSIGN(auto bc, textures.get(m.baseColorImage, true));
    MINGOL_TRY_ASSIGN(auto mr, textures.get(m.metallicRoughnessImage, false));
    MINGOL_TRY_ASSIGN(auto nm, textures.get(m.normalImage, false));
    MINGOL_TRY_ASSIGN(auto em, textures.get(m.emissiveImage, true));
    d.baseColorTexture = std::move(bc);
    d.metallicRoughnessTexture = std::move(mr);
    d.normalTexture = std::move(nm);
    d.emissiveTexture = std::move(em);
    MINGOL_TRY_ASSIGN(auto mat, renderer.createMaterial(d));
    out.materials.push_back(std::move(mat));
  }

  for (const assets::ModelMesh& mesh : model.meshes) {
    auto& prims = out.meshes.emplace_back();
    for (const assets::ModelPrimitive& p : mesh.primitives) {
      if (p.material >= static_cast<int>(model.materials.size()))
        return makeError(ErrorCode::OutOfRange, "mesh '{}' uses material {} out of range", mesh.name, p.material);
      MINGOL_TRY_ASSIGN(auto gm, renderer.createMesh(p.data));
      prims.push_back(std::move(gm));
    }
  }
  for (const assets::ModelSkin& s : model.skins) {
    out.skeletons.push_back(std::make_shared<const renderer::Skeleton>(s.skeleton));
    auto& cl = out.clips.emplace_back();
    for (const auto& c : s.clips) cl.push_back(std::make_shared<const renderer::AnimationClip>(c));
  }
  return out;
}

Result<ModelInstance> instantiateModel(scene::Scene& scene, const assets::ModelData& model, const GpuModel& gpu,
                                       const std::string& name, scene::GameObject* parent) {
  if (gpu.meshes.size() != model.meshes.size() || gpu.materials.size() != model.materials.size())
    return makeError(ErrorCode::InvalidArgument, "GpuModel does not match ModelData");
  const std::size_t n = model.nodes.size();
  for (std::size_t i = 0; i < n; ++i) {
    const assets::ModelNode& nd = model.nodes[i];
    if (nd.mesh >= static_cast<int>(model.meshes.size()) || nd.skin >= static_cast<int>(model.skins.size()) ||
        nd.parent >= static_cast<int>(n))
      return makeError(ErrorCode::OutOfRange, "node {} has an index out of range", i);
  }

  // Shared skeleton/clip data, one set per skin.
  std::vector<std::shared_ptr<const renderer::Skeleton>> skeletons = gpu.skeletons;
  std::vector<std::vector<std::shared_ptr<const renderer::AnimationClip>>> clips = gpu.clips;
  if (skeletons.size() != model.skins.size() || clips.size() != model.skins.size()) {
    skeletons.clear();
    clips.clear();
    for (const assets::ModelSkin& s : model.skins) {
      skeletons.push_back(std::make_shared<const renderer::Skeleton>(s.skeleton));
      auto& cl = clips.emplace_back();
      for (const auto& c : s.clips) cl.push_back(std::make_shared<const renderer::AnimationClip>(c));
    }
  }

  ModelInstance inst;
  inst.root = scene.createObject(name, parent);
  inst.nodes.assign(n, nullptr);

  // Parents are created before children regardless of node order; cycles are rejected.
  std::vector<std::uint8_t> state(n, 0);  // 0 new, 1 in progress, 2 done
  Result<void> status;
  auto build = [&](auto&& self, std::size_t i) -> bool {
    if (state[i] == 2) return true;
    if (state[i] == 1) {
      status = makeError(ErrorCode::Corrupt, "node hierarchy has a cycle at node {}", i);
      return false;
    }
    state[i] = 1;
    const assets::ModelNode& nd = model.nodes[i];
    scene::GameObject* par = inst.root;
    if (nd.parent >= 0) {
      if (!self(self, static_cast<std::size_t>(nd.parent))) return false;
      par = inst.nodes[static_cast<std::size_t>(nd.parent)];
    }
    const bool skinned = nd.mesh >= 0 && nd.skin >= 0;
    if (skinned) par = inst.root;  // glTF: skinned meshes ignore their node transform
    scene::GameObject* go = scene.createObject(nd.name.empty() ? "node" + std::to_string(i) : nd.name, par);
    if (!skinned) go->transform().setLocal(nd.translation, nd.rotation, nd.scale);
    inst.nodes[i] = go;

    if (nd.mesh >= 0) {
      const auto& meshes = gpu.meshes[static_cast<std::size_t>(nd.mesh)];
      const assets::ModelMesh& md = model.meshes[static_cast<std::size_t>(nd.mesh)];
      for (std::size_t p = 0; p < meshes.size(); ++p) {
        auto* mr = go->addComponent<scene::MeshRenderer>();
        mr->mesh = meshes[p];
        const int mi = md.primitives[p].material;
        mr->material = mi >= 0 ? gpu.materials[static_cast<std::size_t>(mi)] : gpu.defaultMaterial;
      }
      if (skinned) {
        auto* an = go->addComponent<scene::Animator>();
        const auto si = static_cast<std::size_t>(nd.skin);
        if (auto r = an->setSkeleton(skeletons[si]); !r.ok()) { status = r.error(); return false; }
        for (const auto& c : clips[si])
          if (auto r = an->addClip(c); !r.ok()) { status = r.error(); return false; }
      }
    }
    state[i] = 2;
    return true;
  };
  for (std::size_t i = 0; i < n; ++i) {
    if (!build(build, i)) {
      scene.destroy(inst.root);
      return status.error();
    }
  }
  return inst;
}

}  // namespace mingol::runtime
