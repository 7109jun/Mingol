// SPDX-License-Identifier: GPL-2.0-only
// Internal: concrete Vulkan implementations of the RHI resource interfaces.
#pragma once

#include <map>
#include <mutex>
#include <utility>

#include "vk_common.hpp"

namespace mingol::rhi::vk {

void nameObject(DeviceCore& core, VkObjectType type, std::uint64_t handle, const std::string& name);

struct StateInfo {
  VkImageLayout layout;
  VkPipelineStageFlags2 stages;
  VkAccessFlags2 access;
};
StateInfo stateInfo(TextureState state, bool depth) noexcept;

class VulkanBuffer final : public Buffer {
 public:
  VulkanBuffer(BufferDesc desc, std::shared_ptr<DeviceCore> c) : Buffer(std::move(desc)), core(std::move(c)) {}
  ~VulkanBuffer() override;
  void flush(std::uint64_t offset, std::uint64_t size) override;
  void invalidate(std::uint64_t offset, std::uint64_t size) override;
  void setMapped(void* p) noexcept { mapped_ = p; }

  std::shared_ptr<DeviceCore> core;
  VkBuffer buffer = VK_NULL_HANDLE;
  void* allocation = nullptr;  // VmaAllocation
};

class VulkanTexture final : public Texture {
 public:
  VulkanTexture(TextureDesc desc, std::shared_ptr<DeviceCore> c) : Texture(std::move(desc)), core(std::move(c)) {}
  ~VulkanTexture() override;
  TextureState state() const noexcept override { return state_; }
  void setState(TextureState s) noexcept { state_ = s; }

  VkImageAspectFlags aspect() const noexcept;
  // View of one mip level / array layer for use as a render target. Cached; VK_NULL_HANDLE on failure.
  VkImageView attachmentView(std::uint32_t mip, std::uint32_t layer);

  std::shared_ptr<DeviceCore> core;
  VkImage image = VK_NULL_HANDLE;
  void* allocation = nullptr;  // VmaAllocation; null for swapchain images
  bool ownsImage = true;
  VkImageView view = VK_NULL_HANDLE;  // all mips/layers, for sampling

 private:
  std::mutex viewMutex_;
  std::map<std::pair<std::uint32_t, std::uint32_t>, VkImageView> subViews_;
  TextureState state_ = TextureState::Undefined;
};

class VulkanSampler final : public Sampler {
 public:
  VulkanSampler(SamplerDesc desc, std::shared_ptr<DeviceCore> c) : Sampler(desc), core(std::move(c)) {}
  ~VulkanSampler() override;
  std::shared_ptr<DeviceCore> core;
  VkSampler sampler = VK_NULL_HANDLE;
};

class VulkanShaderModule final : public ShaderModule {
 public:
  VulkanShaderModule(ShaderStage stage, std::string entry, std::shared_ptr<DeviceCore> c)
      : ShaderModule(stage, std::move(entry)), core(std::move(c)) {}
  ~VulkanShaderModule() override;
  std::shared_ptr<DeviceCore> core;
  VkShaderModule module = VK_NULL_HANDLE;
};

class VulkanBindGroupLayout final : public BindGroupLayout {
 public:
  VulkanBindGroupLayout(BindGroupLayoutDesc desc, std::shared_ptr<DeviceCore> c)
      : BindGroupLayout(std::move(desc)), core(std::move(c)) {}
  ~VulkanBindGroupLayout() override;
  std::shared_ptr<DeviceCore> core;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
};

class VulkanBindGroup final : public BindGroup {
 public:
  VulkanBindGroup(BindGroupDesc desc, std::shared_ptr<DeviceCore> c) : BindGroup(std::move(desc)), core(std::move(c)) {}
  ~VulkanBindGroup() override;
  std::shared_ptr<DeviceCore> core;
  VkDescriptorPool pool = VK_NULL_HANDLE;  // one pool per group; freed with the group
  VkDescriptorSet set = VK_NULL_HANDLE;
};

class VulkanPipeline final : public GraphicsPipeline {
 public:
  VulkanPipeline(GraphicsPipelineDesc desc, std::shared_ptr<DeviceCore> c)
      : GraphicsPipeline(std::move(desc)), core(std::move(c)) {}
  ~VulkanPipeline() override;
  std::shared_ptr<DeviceCore> core;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
};

class VulkanCommandList final : public CommandList {
 public:
  static Result<std::unique_ptr<VulkanCommandList>> create(std::shared_ptr<DeviceCore> core);
  ~VulkanCommandList() override;

  Result<void> begin() override;
  Result<void> end() override;
  void transition(Texture& texture, TextureState newState) override;
  void beginRendering(const RenderPassDesc& pass) override;
  void endRendering() override;
  void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) override;
  void setScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) override;
  void bindPipeline(GraphicsPipeline& pipeline) override;
  void bindGroup(std::uint32_t set, BindGroup& group) override;
  void bindVertexBuffer(std::uint32_t binding, Buffer& buffer, std::uint64_t offset) override;
  void bindIndexBuffer(Buffer& buffer, IndexType type, std::uint64_t offset) override;
  void pushConstants(const void* data, std::uint32_t size, std::uint32_t offset) override;
  void draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
            std::uint32_t firstInstance) override;
  void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                   std::int32_t vertexOffset, std::uint32_t firstInstance) override;
  void copyBuffer(Buffer& src, Buffer& dst, std::uint64_t size, std::uint64_t srcOffset,
                  std::uint64_t dstOffset) override;
  void copyBufferToTexture(Buffer& src, Texture& dst, std::uint32_t mip, std::uint32_t layer,
                           std::uint64_t srcOffset) override;
  void copyTextureToBuffer(Texture& src, Buffer& dst, std::uint32_t mip, std::uint32_t layer,
                           std::uint64_t dstOffset) override;
  void generateMipmaps(Texture& texture) override;

  VkCommandBuffer handle() const noexcept { return cmd_; }
  bool executable() const noexcept { return executable_; }
  void markSubmitted(std::uint64_t serial) noexcept { lastSerial_ = serial; }

 private:
  explicit VulkanCommandList(std::shared_ptr<DeviceCore> core) : core_(std::move(core)) {}
  void memoryBarrierAfterTransfer();

  std::shared_ptr<DeviceCore> core_;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  std::uint64_t lastSerial_ = 0;
  bool recording_ = false;
  bool executable_ = false;
  bool inRendering_ = false;
  VulkanPipeline* pipeline_ = nullptr;
};

class VulkanSwapchain final : public Swapchain {
 public:
  static Result<std::unique_ptr<VulkanSwapchain>> create(std::shared_ptr<DeviceCore> core, const SwapchainDesc& desc);
  ~VulkanSwapchain() override;

  Format format() const noexcept override { return format_; }
  std::uint32_t width() const noexcept override { return width_; }
  std::uint32_t height() const noexcept override { return height_; }
  std::uint32_t imageCount() const noexcept override { return static_cast<std::uint32_t>(images_.size()); }
  Result<AcquiredImage> acquire() override;
  Result<void> present() override;
  Result<void> resize(std::uint32_t width, std::uint32_t height) override;

  // Used by Device::submit.
  bool acquired() const noexcept { return acquired_; }
  bool submittedForPresent() const noexcept { return readyToPresent_; }
  VulkanTexture* currentTexture() const noexcept { return acquired_ ? images_[index_].get() : nullptr; }
  VkSemaphore acquireSemaphore() const noexcept { return acquireSems_[acquireSlot_]; }
  VkSemaphore renderFinishedSemaphore() const noexcept { return renderDone_[index_]; }
  void markSubmitted(std::uint64_t serial) noexcept {
    acquireSlotSerial_[acquireSlot_] = serial;
    readyToPresent_ = true;
  }

 private:
  explicit VulkanSwapchain(std::shared_ptr<DeviceCore> core) : core_(std::move(core)) {}
  Result<void> build(std::uint32_t width, std::uint32_t height);
  void destroyImagesAndSync();

  std::shared_ptr<DeviceCore> core_;
  SwapchainDesc desc_;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  Format format_ = Format::Undefined;
  std::uint32_t width_ = 0, height_ = 0;
  std::vector<std::shared_ptr<VulkanTexture>> images_;
  std::vector<VkSemaphore> acquireSems_;
  std::vector<std::uint64_t> acquireSlotSerial_;
  std::vector<VkSemaphore> renderDone_;
  std::size_t acquireSlot_ = 0;
  std::uint32_t index_ = 0;
  bool acquired_ = false;
  bool readyToPresent_ = false;
};

}  // namespace mingol::rhi::vk
