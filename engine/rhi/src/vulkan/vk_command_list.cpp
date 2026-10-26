// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>

#include "vk_objects.hpp"

namespace mingol::rhi::vk {

StateInfo stateInfo(TextureState state, bool depth) noexcept {
  switch (state) {
    case TextureState::Undefined:
      return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
    case TextureState::ShaderRead:
      return {depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case TextureState::ColorAttachment:
      return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case TextureState::DepthAttachment:
      return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case TextureState::TransferSrc:
      return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
    case TextureState::TransferDst:
      return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case TextureState::Storage:
      return {VK_IMAGE_LAYOUT_GENERAL,
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
              VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
    case TextureState::Present:
      return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
  }
  return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
}

namespace {

bool isReadOnlyState(TextureState s) {
  return s == TextureState::ShaderRead || s == TextureState::TransferSrc || s == TextureState::Present;
}

std::uint32_t mipExtent(std::uint32_t base, std::uint32_t mip) { return std::max(1u, base >> mip); }

void imageBarrier(VkCommandBuffer cmd, VulkanTexture& tex, const StateInfo& from, const StateInfo& to,
                  std::uint32_t baseMip, std::uint32_t mipCount) {
  VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = from.stages;
  b.srcAccessMask = from.access;
  b.dstStageMask = to.stages;
  b.dstAccessMask = to.access;
  b.oldLayout = from.layout;
  b.newLayout = to.layout;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = tex.image;
  b.subresourceRange = {tex.aspect(), baseMip, mipCount, 0, VK_REMAINING_ARRAY_LAYERS};
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

VulkanTexture& asVk(Texture& t) { return static_cast<VulkanTexture&>(t); }
VulkanBuffer& asVk(Buffer& b) { return static_cast<VulkanBuffer&>(b); }

}  // namespace

Result<std::unique_ptr<VulkanCommandList>> VulkanCommandList::create(std::shared_ptr<DeviceCore> core) {
  auto list = std::unique_ptr<VulkanCommandList>(new VulkanCommandList(core));
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.queueFamilyIndex = core->queueFamily;
  pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  MINGOL_VK_TRY(vkCreateCommandPool(core->device, &pci, nullptr, &list->pool_), "vkCreateCommandPool");
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = list->pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  MINGOL_VK_TRY(vkAllocateCommandBuffers(core->device, &ai, &list->cmd_), "vkAllocateCommandBuffers");
  return list;
}

VulkanCommandList::~VulkanCommandList() {
  if (pool_ == VK_NULL_HANDLE) return;
  // The pool may still be executing; wait for the last submission before destroying it.
  (void)core_->waitSerial(lastSerial_);
  vkDestroyCommandPool(core_->device, pool_, nullptr);
}

Result<void> VulkanCommandList::begin() {
  if (recording_) return makeError(ErrorCode::InvalidArgument, "begin() called while already recording");
  MINGOL_TRY(core_->waitSerial(lastSerial_));
  MINGOL_VK_TRY(vkResetCommandPool(core_->device, pool_, 0), "vkResetCommandPool");
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  MINGOL_VK_TRY(vkBeginCommandBuffer(cmd_, &bi), "vkBeginCommandBuffer");
  recording_ = true;
  executable_ = false;
  inRendering_ = false;
  pipeline_ = nullptr;
  return {};
}

Result<void> VulkanCommandList::end() {
  if (!recording_) return makeError(ErrorCode::InvalidArgument, "end() called without begin()");
  if (inRendering_) return makeError(ErrorCode::InvalidArgument, "end() called inside a render pass");
  MINGOL_VK_TRY(vkEndCommandBuffer(cmd_), "vkEndCommandBuffer");
  recording_ = false;
  executable_ = true;
  return {};
}

void VulkanCommandList::transition(Texture& texture, TextureState newState) {
  MINGOL_VERIFY_MSG(recording_ && !inRendering_, "transition() must be recorded outside a render pass");
  VulkanTexture& tex = asVk(texture);
  const TextureState oldState = tex.state();
  if (oldState == newState && isReadOnlyState(newState)) return;
  const bool depth = isDepthFormat(tex.desc().format);
  imageBarrier(cmd_, tex, stateInfo(oldState, depth), stateInfo(newState, depth), 0, VK_REMAINING_MIP_LEVELS);
  tex.setState(newState);
}

void VulkanCommandList::beginRendering(const RenderPassDesc& pass) {
  MINGOL_VERIFY_MSG(recording_ && !inRendering_, "beginRendering() requires a recording list outside a pass");
  MINGOL_VERIFY_MSG(!pass.colors.empty() || pass.depth.texture != nullptr, "render pass has no attachments");

  std::vector<VkRenderingAttachmentInfo> colorInfos;
  std::uint32_t width = 0, height = 0;
  auto setExtent = [&](VulkanTexture& tex, std::uint32_t mip) {
    const std::uint32_t w = mipExtent(tex.desc().width, mip), h = mipExtent(tex.desc().height, mip);
    if (width == 0) {
      width = w;
      height = h;
    }
    MINGOL_VERIFY_MSG(w == width && h == height, "all attachments of a pass must have the same size");
  };

  for (const ColorAttachment& c : pass.colors) {
    MINGOL_VERIFY_MSG(c.texture != nullptr, "null colour attachment");
    VulkanTexture& tex = asVk(*c.texture);
    MINGOL_VERIFY_MSG(hasFlag(tex.desc().usage, TextureUsage::ColorAttachment), "texture lacks ColorAttachment usage");
    MINGOL_VERIFY_MSG(c.mip < tex.desc().mipLevels && c.layer < tex.desc().arrayLayers, "attachment subresource out of range");
    setExtent(tex, c.mip);
    transition(tex, TextureState::ColorAttachment);
    VkImageView view = tex.attachmentView(c.mip, c.layer);
    MINGOL_VERIFY_MSG(view != VK_NULL_HANDLE, "could not create attachment view");

    VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    info.imageView = view;
    info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.loadOp = c.load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                  : c.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    info.storeOp = c.store == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    for (int i = 0; i < 4; ++i) info.clearValue.color.float32[i] = c.clear[i];
    colorInfos.push_back(info);
  }

  VkRenderingAttachmentInfo depthInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  const bool hasDepth = pass.depth.texture != nullptr;
  if (hasDepth) {
    VulkanTexture& tex = asVk(*pass.depth.texture);
    MINGOL_VERIFY_MSG(hasFlag(tex.desc().usage, TextureUsage::DepthStencil), "texture lacks DepthStencil usage");
    MINGOL_VERIFY_MSG(pass.depth.mip < tex.desc().mipLevels && pass.depth.layer < tex.desc().arrayLayers, "depth subresource out of range");
    setExtent(tex, pass.depth.mip);
    transition(tex, TextureState::DepthAttachment);
    VkImageView view = tex.attachmentView(pass.depth.mip, pass.depth.layer);
    MINGOL_VERIFY_MSG(view != VK_NULL_HANDLE, "could not create depth view");
    depthInfo.imageView = view;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthInfo.loadOp = pass.depth.load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                       : pass.depth.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthInfo.storeOp = pass.depth.store == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthInfo.clearValue.depthStencil = {pass.depth.clearDepth, 0};
  }

  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea = {{0, 0}, {width, height}};
  ri.layerCount = 1;
  ri.colorAttachmentCount = static_cast<std::uint32_t>(colorInfos.size());
  ri.pColorAttachments = colorInfos.data();
  ri.pDepthAttachment = hasDepth ? &depthInfo : nullptr;
  vkCmdBeginRendering(cmd_, &ri);
  inRendering_ = true;

  // Sensible defaults; callers may override.
  setViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
  setScissor(0, 0, width, height);
}

void VulkanCommandList::endRendering() {
  MINGOL_VERIFY_MSG(inRendering_, "endRendering() without beginRendering()");
  vkCmdEndRendering(cmd_);
  inRendering_ = false;
  pipeline_ = nullptr;
}

void VulkanCommandList::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
  MINGOL_VERIFY(recording_);
  const VkViewport vp{x, y, width, height, minDepth, maxDepth};
  vkCmdSetViewport(cmd_, 0, 1, &vp);
}

void VulkanCommandList::setScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) {
  MINGOL_VERIFY(recording_);
  const VkRect2D rect{{x, y}, {width, height}};
  vkCmdSetScissor(cmd_, 0, 1, &rect);
}

void VulkanCommandList::bindPipeline(GraphicsPipeline& pipeline) {
  MINGOL_VERIFY_MSG(inRendering_, "bindPipeline() must be inside a render pass");
  pipeline_ = static_cast<VulkanPipeline*>(&pipeline);
  vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->pipeline);
}

void VulkanCommandList::bindGroup(std::uint32_t set, BindGroup& group) {
  MINGOL_VERIFY_MSG(pipeline_ != nullptr, "bindGroup() needs a bound pipeline");
  MINGOL_VERIFY_MSG(set < pipeline_->desc().setLayouts.size(), "set index not declared by the pipeline");
  auto& g = static_cast<VulkanBindGroup&>(group);
  vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout, set, 1, &g.set, 0, nullptr);
}

void VulkanCommandList::bindVertexBuffer(std::uint32_t binding, Buffer& buffer, std::uint64_t offset) {
  MINGOL_VERIFY(inRendering_);
  MINGOL_VERIFY_MSG(hasFlag(buffer.desc().usage, BufferUsage::Vertex), "buffer lacks Vertex usage");
  const VkBuffer b = asVk(buffer).buffer;
  const VkDeviceSize o = offset;
  vkCmdBindVertexBuffers(cmd_, binding, 1, &b, &o);
}

void VulkanCommandList::bindIndexBuffer(Buffer& buffer, IndexType type, std::uint64_t offset) {
  MINGOL_VERIFY(inRendering_);
  MINGOL_VERIFY_MSG(hasFlag(buffer.desc().usage, BufferUsage::Index), "buffer lacks Index usage");
  vkCmdBindIndexBuffer(cmd_, asVk(buffer).buffer, offset, type == IndexType::U16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::pushConstants(const void* data, std::uint32_t size, std::uint32_t offset) {
  MINGOL_VERIFY_MSG(pipeline_ != nullptr, "pushConstants() needs a bound pipeline");
  MINGOL_VERIFY_MSG(size % 4 == 0 && offset % 4 == 0 && offset + size <= pipeline_->desc().pushConstantSize,
                    "push constant range exceeds the pipeline's declared size");
  vkCmdPushConstants(cmd_, pipeline_->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, offset, size, data);
}

void VulkanCommandList::draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                             std::uint32_t firstInstance) {
  MINGOL_VERIFY_MSG(inRendering_ && pipeline_ != nullptr, "draw() needs a render pass and a pipeline");
  vkCmdDraw(cmd_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                                    std::int32_t vertexOffset, std::uint32_t firstInstance) {
  MINGOL_VERIFY_MSG(inRendering_ && pipeline_ != nullptr, "drawIndexed() needs a render pass and a pipeline");
  vkCmdDrawIndexed(cmd_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

// Makes transfer writes visible to everything that follows, including host reads after a wait.
void VulkanCommandList::memoryBarrierAfterTransfer() {
  VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
  mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
  mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.memoryBarrierCount = 1;
  dep.pMemoryBarriers = &mb;
  vkCmdPipelineBarrier2(cmd_, &dep);
}

void VulkanCommandList::copyBuffer(Buffer& src, Buffer& dst, std::uint64_t size, std::uint64_t srcOffset,
                                   std::uint64_t dstOffset) {
  MINGOL_VERIFY_MSG(recording_ && !inRendering_, "copyBuffer() must be outside a render pass");
  MINGOL_VERIFY_MSG(hasFlag(src.desc().usage, BufferUsage::TransferSrc), "source lacks TransferSrc usage");
  MINGOL_VERIFY_MSG(hasFlag(dst.desc().usage, BufferUsage::TransferDst), "destination lacks TransferDst usage");
  MINGOL_VERIFY_MSG(srcOffset + size <= src.size() && dstOffset + size <= dst.size(), "copy range out of bounds");
  const VkBufferCopy region{srcOffset, dstOffset, size};
  vkCmdCopyBuffer(cmd_, asVk(src).buffer, asVk(dst).buffer, 1, &region);
  memoryBarrierAfterTransfer();
}

void VulkanCommandList::copyBufferToTexture(Buffer& src, Texture& dst, std::uint32_t mip, std::uint32_t layer,
                                            std::uint64_t srcOffset) {
  MINGOL_VERIFY_MSG(recording_ && !inRendering_, "copy must be outside a render pass");
  VulkanTexture& tex = asVk(dst);
  MINGOL_VERIFY_MSG(hasFlag(src.desc().usage, BufferUsage::TransferSrc), "source lacks TransferSrc usage");
  MINGOL_VERIFY_MSG(hasFlag(tex.desc().usage, TextureUsage::TransferDst), "texture lacks TransferDst usage");
  MINGOL_VERIFY_MSG(mip < tex.desc().mipLevels && layer < tex.desc().arrayLayers, "subresource out of range");
  const std::uint32_t w = mipExtent(tex.desc().width, mip), h = mipExtent(tex.desc().height, mip);
  MINGOL_VERIFY_MSG(srcOffset + static_cast<std::uint64_t>(w) * h * bytesPerPixel(tex.desc().format) <= src.size(),
                    "source buffer too small for the copy");
  transition(tex, TextureState::TransferDst);

  VkBufferImageCopy region{};
  region.bufferOffset = srcOffset;
  region.imageSubresource = {tex.aspect(), mip, layer, 1};
  region.imageExtent = {w, h, 1};
  vkCmdCopyBufferToImage(cmd_, asVk(src).buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanCommandList::copyTextureToBuffer(Texture& src, Buffer& dst, std::uint32_t mip, std::uint32_t layer,
                                            std::uint64_t dstOffset) {
  MINGOL_VERIFY_MSG(recording_ && !inRendering_, "copy must be outside a render pass");
  VulkanTexture& tex = asVk(src);
  MINGOL_VERIFY_MSG(hasFlag(tex.desc().usage, TextureUsage::TransferSrc), "texture lacks TransferSrc usage");
  MINGOL_VERIFY_MSG(hasFlag(dst.desc().usage, BufferUsage::TransferDst), "destination lacks TransferDst usage");
  MINGOL_VERIFY_MSG(mip < tex.desc().mipLevels && layer < tex.desc().arrayLayers, "subresource out of range");
  const std::uint32_t w = mipExtent(tex.desc().width, mip), h = mipExtent(tex.desc().height, mip);
  MINGOL_VERIFY_MSG(dstOffset + static_cast<std::uint64_t>(w) * h * bytesPerPixel(tex.desc().format) <= dst.size(),
                    "destination buffer too small for the copy");
  transition(tex, TextureState::TransferSrc);

  VkBufferImageCopy region{};
  region.bufferOffset = dstOffset;
  region.imageSubresource = {tex.aspect(), mip, layer, 1};
  region.imageExtent = {w, h, 1};
  vkCmdCopyImageToBuffer(cmd_, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, asVk(dst).buffer, 1, &region);
  memoryBarrierAfterTransfer();
}

void VulkanCommandList::generateMipmaps(Texture& texture) {
  MINGOL_VERIFY_MSG(recording_ && !inRendering_, "generateMipmaps() must be outside a render pass");
  VulkanTexture& tex = asVk(texture);
  const TextureDesc& d = tex.desc();
  MINGOL_VERIFY_MSG(hasFlag(d.usage, TextureUsage::TransferSrc) && hasFlag(d.usage, TextureUsage::TransferDst),
                    "generateMipmaps() needs TransferSrc and TransferDst usage");
  MINGOL_VERIFY_MSG(!isDepthFormat(d.format), "cannot generate mipmaps for depth textures");
  if (d.mipLevels <= 1) {
    transition(tex, TextureState::TransferSrc);
    return;
  }

  // Whole image to TransferDst first (keeps mip 0 contents), then walk the chain level by level.
  transition(tex, TextureState::TransferDst);
  const StateInfo dst = stateInfo(TextureState::TransferDst, false);
  const StateInfo srcInfo = stateInfo(TextureState::TransferSrc, false);

  for (std::uint32_t level = 1; level < d.mipLevels; ++level) {
    imageBarrier(cmd_, tex, dst, srcInfo, level - 1, 1);  // previous level becomes the blit source

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, d.arrayLayers};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(mipExtent(d.width, level - 1)),
                          static_cast<std::int32_t>(mipExtent(d.height, level - 1)), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, d.arrayLayers};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(mipExtent(d.width, level)),
                          static_cast<std::int32_t>(mipExtent(d.height, level)), 1};
    vkCmdBlitImage(cmd_, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
  }
  imageBarrier(cmd_, tex, dst, srcInfo, d.mipLevels - 1, 1);  // last level: dst -> src, whole image now TransferSrc
  tex.setState(TextureState::TransferSrc);
}

}  // namespace mingol::rhi::vk
