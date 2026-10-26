// SPDX-License-Identifier: GPL-2.0-only
// Backend-neutral GPU device interface. The renderer is written against these classes only;
// a Vulkan implementation is provided by createVulkanDevice().
//
// Threading: a Device may be used from several threads for resource creation. A CommandList is
// recorded by one thread at a time. submit() is internally synchronised.
//
// Lifetime: dropping the last reference to a resource is always safe, even while the GPU is still
// using it - destruction is deferred until the work that may reference it has finished.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/rhi/types.hpp"

namespace mingol::rhi {

class Buffer {
 public:
  virtual ~Buffer() = default;
  const BufferDesc& desc() const noexcept { return desc_; }
  std::uint64_t size() const noexcept { return desc_.size; }
  // Persistently mapped pointer for CpuToGpu / GpuToCpu buffers, nullptr for GpuOnly.
  void* mapped() const noexcept { return mapped_; }
  // Make CPU writes visible to the GPU / GPU writes visible to the CPU (no-ops on coherent memory).
  virtual void flush(std::uint64_t offset = 0, std::uint64_t size = kWholeSize) = 0;
  virtual void invalidate(std::uint64_t offset = 0, std::uint64_t size = kWholeSize) = 0;

 protected:
  explicit Buffer(BufferDesc desc) : desc_(std::move(desc)) {}
  BufferDesc desc_;
  void* mapped_ = nullptr;
};

class Texture {
 public:
  virtual ~Texture() = default;
  const TextureDesc& desc() const noexcept { return desc_; }
  // State as of the most recently *recorded* command (not necessarily executed yet).
  virtual TextureState state() const noexcept = 0;

 protected:
  explicit Texture(TextureDesc desc) : desc_(std::move(desc)) {}
  TextureDesc desc_;
};

class Sampler {
 public:
  virtual ~Sampler() = default;
  const SamplerDesc& desc() const noexcept { return desc_; }

 protected:
  explicit Sampler(SamplerDesc desc) : desc_(desc) {}
  SamplerDesc desc_;
};

class ShaderModule {
 public:
  virtual ~ShaderModule() = default;
  ShaderStage stage() const noexcept { return stage_; }
  const std::string& entryPoint() const noexcept { return entry_; }

 protected:
  ShaderModule(ShaderStage stage, std::string entry) : stage_(stage), entry_(std::move(entry)) {}
  ShaderStage stage_;
  std::string entry_;
};

class BindGroupLayout {
 public:
  virtual ~BindGroupLayout() = default;
  const BindGroupLayoutDesc& desc() const noexcept { return desc_; }

 protected:
  explicit BindGroupLayout(BindGroupLayoutDesc desc) : desc_(std::move(desc)) {}
  BindGroupLayoutDesc desc_;
};

class Buffer;
struct BindGroupEntry {
  std::uint32_t binding = 0;
  std::uint32_t arrayElement = 0;
  // Buffer bindings:
  std::shared_ptr<Buffer> buffer;
  std::uint64_t offset = 0;
  std::uint64_t range = kWholeSize;
  // Sampled texture bindings (texture must be in ShaderRead state when drawn):
  std::shared_ptr<Texture> texture;
  std::shared_ptr<Sampler> sampler;
};

struct BindGroupDesc {
  std::shared_ptr<BindGroupLayout> layout;
  std::vector<BindGroupEntry> entries;
  std::string debugName;
};

// Holds shared references to the bound resources, so they live as long as the group.
class BindGroup {
 public:
  virtual ~BindGroup() = default;
  const BindGroupDesc& desc() const noexcept { return desc_; }

 protected:
  explicit BindGroup(BindGroupDesc desc) : desc_(std::move(desc)) {}
  BindGroupDesc desc_;
};

class GraphicsPipeline {
 public:
  virtual ~GraphicsPipeline() = default;
  const GraphicsPipelineDesc& desc() const noexcept { return desc_; }

 protected:
  explicit GraphicsPipeline(GraphicsPipelineDesc desc) : desc_(std::move(desc)) {}
  GraphicsPipelineDesc desc_;
};

class CommandList {
 public:
  virtual ~CommandList() = default;

  // Waits for any previous submission of this list to finish, then starts recording.
  virtual Result<void> begin() = 0;
  virtual Result<void> end() = 0;

  // Explicit state change. Required before a texture is sampled in a pass and before it is
  // presented; copy and attachment commands transition their targets automatically.
  virtual void transition(Texture& texture, TextureState newState) = 0;

  virtual void beginRendering(const RenderPassDesc& pass) = 0;
  virtual void endRendering() = 0;

  virtual void setViewport(float x, float y, float width, float height, float minDepth = 0.0f,
                           float maxDepth = 1.0f) = 0;
  virtual void setScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) = 0;

  virtual void bindPipeline(GraphicsPipeline& pipeline) = 0;
  virtual void bindGroup(std::uint32_t set, BindGroup& group) = 0;
  virtual void bindVertexBuffer(std::uint32_t binding, Buffer& buffer, std::uint64_t offset = 0) = 0;
  virtual void bindIndexBuffer(Buffer& buffer, IndexType type, std::uint64_t offset = 0) = 0;
  virtual void pushConstants(const void* data, std::uint32_t size, std::uint32_t offset = 0) = 0;

  virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
                    std::uint32_t firstInstance = 0) = 0;
  virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0,
                           std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) = 0;

  // Transfers (outside a render pass). They make their results visible to later commands.
  virtual void copyBuffer(Buffer& src, Buffer& dst, std::uint64_t size, std::uint64_t srcOffset = 0,
                          std::uint64_t dstOffset = 0) = 0;
  // Copies tightly packed pixels into one mip level / array layer; leaves the texture in TransferDst.
  virtual void copyBufferToTexture(Buffer& src, Texture& dst, std::uint32_t mip = 0, std::uint32_t layer = 0,
                                   std::uint64_t srcOffset = 0) = 0;
  // Copies one mip level / layer into a tightly packed buffer; leaves the texture in TransferSrc.
  virtual void copyTextureToBuffer(Texture& src, Buffer& dst, std::uint32_t mip = 0, std::uint32_t layer = 0,
                                   std::uint64_t dstOffset = 0) = 0;
  // Fills mips 1..N-1 from mip 0 (all layers). Texture needs TransferSrc|TransferDst usage and a
  // linear-filterable format. Leaves the texture in TransferSrc.
  virtual void generateMipmaps(Texture& texture) = 0;
};

struct AcquiredImage {
  Texture* texture = nullptr;  // owned by the swapchain; valid until the next acquire()/resize()
  std::uint32_t index = 0;
};

class Swapchain {
 public:
  virtual ~Swapchain() = default;
  virtual Format format() const noexcept = 0;
  virtual std::uint32_t width() const noexcept = 0;
  virtual std::uint32_t height() const noexcept = 0;
  virtual std::uint32_t imageCount() const noexcept = 0;

  // Fails with ErrorCode::OutOfDate when the surface changed; call resize() and try again.
  virtual Result<AcquiredImage> acquire() = 0;
  // Presents the image from the last acquire(); Device::submit(list, &swapchain) must have been
  // called first. Fails with OutOfDate when the swapchain needs recreation.
  virtual Result<void> present() = 0;
  virtual Result<void> resize(std::uint32_t width, std::uint32_t height) = 0;
};

class Device {
 public:
  virtual ~Device() = default;

  virtual const DeviceInfo& info() const noexcept = 0;

  virtual Result<std::shared_ptr<Buffer>> createBuffer(const BufferDesc& desc) = 0;
  virtual Result<std::shared_ptr<Texture>> createTexture(const TextureDesc& desc) = 0;
  virtual Result<std::shared_ptr<Sampler>> createSampler(const SamplerDesc& desc) = 0;
  virtual Result<std::shared_ptr<ShaderModule>> createShaderModule(const ShaderModuleDesc& desc) = 0;
  virtual Result<std::shared_ptr<BindGroupLayout>> createBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
  virtual Result<std::shared_ptr<BindGroup>> createBindGroup(const BindGroupDesc& desc) = 0;
  virtual Result<std::shared_ptr<GraphicsPipeline>> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
  virtual Result<std::unique_ptr<CommandList>> createCommandList() = 0;
  virtual Result<std::unique_ptr<Swapchain>> createSwapchain(const SwapchainDesc& desc) = 0;

  // Submits a recorded list. Returns the submission serial (monotonic, starts at 1). When
  // `presenting` is set, the submission waits for that swapchain's acquired image and the image
  // must be in TextureState::Present at the end of the list.
  virtual Result<std::uint64_t> submit(CommandList& list, Swapchain* presenting = nullptr) = 0;
  virtual bool isComplete(std::uint64_t serial) const = 0;
  virtual Result<void> waitForSerial(std::uint64_t serial) = 0;
  virtual Result<void> waitIdle() = 0;

  // Number of validation-layer errors reported so far (always 0 when validation is off).
  virtual std::uint64_t validationErrorCount() const noexcept = 0;

  // ---- Convenience helpers built on the virtual interface (device.cpp) ---------------------------

  // Creates a buffer holding `data`. GpuOnly buffers are filled through a staging copy and the
  // call blocks until it completes; CpuToGpu buffers are written directly.
  Result<std::shared_ptr<Buffer>> createBufferWithData(const BufferDesc& desc, std::span<const std::uint8_t> data);

  // Creates a texture from tightly packed mip-0 pixels, optionally generating the mip chain, and
  // leaves it in ShaderRead. Blocks until the upload completes.
  Result<std::shared_ptr<Texture>> createTextureWithData(TextureDesc desc, std::span<const std::uint8_t> pixels,
                                                         bool generateMips);

  // Reads back one mip level / layer as tightly packed pixels (blocking). Texture needs TransferSrc usage.
  Result<std::vector<std::uint8_t>> readbackTexture(Texture& texture, std::uint32_t mip = 0, std::uint32_t layer = 0);
};

// Creates the Vulkan implementation. Fails with Unsupported if no suitable Vulkan 1.3 device exists.
Result<std::unique_ptr<Device>> createVulkanDevice(const DeviceDesc& desc = {});

}  // namespace mingol::rhi
