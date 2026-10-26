// SPDX-License-Identifier: GPL-2.0-only
// Forward PBR renderer with a directional shadow map and GPU skinning, written against the RHI.
#pragma once

#include <memory>

#include "mingol/core/error.hpp"
#include "mingol/renderer/mesh.hpp"
#include "mingol/renderer/scene.hpp"
#include "mingol/rhi/device.hpp"

namespace mingol::renderer {

// GPU-resident mesh. Immutable after creation.
class Mesh {
 public:
  std::uint32_t indexCount() const noexcept { return indexCount_; }
  std::uint32_t vertexCount() const noexcept { return vertexCount_; }
  bool skinned() const noexcept { return skin_ != nullptr; }
  const Aabb& bounds() const noexcept { return bounds_; }
  // Highest joint index with non-zero weight (skinned meshes), used to validate joint matrices.
  std::uint32_t maxJointIndex() const noexcept { return maxJoint_; }

 private:
  friend class Renderer;
  std::shared_ptr<rhi::Buffer> vertices_;
  std::shared_ptr<rhi::Buffer> skin_;
  std::shared_ptr<rhi::Buffer> indices_;
  std::uint32_t indexCount_ = 0;
  std::uint32_t vertexCount_ = 0;
  std::uint32_t maxJoint_ = 0;
  Aabb bounds_;
};

enum class AlphaMode : std::uint8_t { Opaque, Blend };

struct MaterialDesc {
  Vec4 baseColor{1, 1, 1, 1};  // linear, multiplied with the base colour texture
  float metallic = 0.0F;
  float roughness = 1.0F;
  Vec3 emissive{0, 0, 0};      // linear radiance added after lighting
  float normalScale = 1.0F;
  AlphaMode alphaMode = AlphaMode::Opaque;
  bool doubleSided = false;
  // Optional textures (nullptr = neutral default). Base colour and emissive should be sRGB formats
  // so the hardware linearises them; metallic-roughness (G = roughness, B = metallic, as in glTF)
  // and normal maps must be linear formats. Textures must be in ShaderRead state.
  std::shared_ptr<rhi::Texture> baseColorTexture;
  std::shared_ptr<rhi::Texture> metallicRoughnessTexture;
  std::shared_ptr<rhi::Texture> normalTexture;
  std::shared_ptr<rhi::Texture> emissiveTexture;
};

// Immutable after creation; create a new material to change parameters.
class Material {
 public:
  const MaterialDesc& desc() const noexcept { return desc_; }

 private:
  friend class Renderer;
  MaterialDesc desc_;
  std::shared_ptr<rhi::Buffer> uniforms_;
  std::shared_ptr<rhi::BindGroup> bindGroup_;
};

struct RendererDesc {
  std::uint32_t maxObjects = 4096;          // draw items per frame
  std::uint32_t maxSkinMatrices = 8192;     // joint matrices per frame across all skinned items
  std::uint32_t shadowMapSize = 2048;       // square, directional light
  std::uint32_t framesInFlight = 2;         // CPU may record this many frames ahead of the GPU
  float shadowNormalBias = 1.5F;            // receiver offset along the normal, in shadow-map texels
  float shadowDepthBiasConstant = 1.25F;
  float shadowDepthBiasSlope = 1.75F;
};

struct FrameStats {
  std::uint32_t drawCalls = 0;
  std::uint32_t shadowDrawCalls = 0;
  std::uint32_t objects = 0;
  std::uint32_t pipelinesCreated = 0;  // cumulative
  bool shadowsRendered = false;
};

// Not thread-safe: use from one thread (usually the render thread). The device must outlive it.
class Renderer {
 public:
  static Result<std::unique_ptr<Renderer>> create(rhi::Device& device, const RendererDesc& desc = {});
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  Result<std::shared_ptr<Mesh>> createMesh(const MeshData& data);
  Result<std::shared_ptr<Material>> createMaterial(const MaterialDesc& desc);

  // Renders the scene into `target` (needs ColorAttachment usage, RGBA8/BGRA8 sRGB or UNORM) and
  // submits. With `presenting` the image is left in Present state and the submission waits for the
  // swapchain image; otherwise the target stays in ColorAttachment state (copy/sample it next).
  // Returns the submission serial.
  Result<std::uint64_t> render(const RenderScene& scene, rhi::Texture& target, rhi::Swapchain* presenting = nullptr);

  const FrameStats& stats() const noexcept { return stats_; }

 private:
  struct Impl;
  explicit Renderer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
  FrameStats stats_;
};

}  // namespace mingol::renderer
