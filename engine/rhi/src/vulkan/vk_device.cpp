// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstring>
#include <set>

#include "vk_mem_alloc.h"
#include "vk_objects.hpp"

namespace mingol::rhi::vk {

// ================================================================================================
// Helpers
// ================================================================================================

const char* resultName(VkResult r) noexcept {
  switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VkResult(other)";
  }
}

VkFormat toVk(Format f) noexcept {
  switch (f) {
    case Format::Undefined: return VK_FORMAT_UNDEFINED;
    case Format::R8Unorm: return VK_FORMAT_R8_UNORM;
    case Format::RG8Unorm: return VK_FORMAT_R8G8_UNORM;
    case Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R16Float: return VK_FORMAT_R16_SFLOAT;
    case Format::RG16Float: return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
    case Format::RG32Float: return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGB32Float: return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::D16Unorm: return VK_FORMAT_D16_UNORM;
    case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
  }
  return VK_FORMAT_UNDEFINED;
}

Format fromVk(VkFormat f) noexcept {
  switch (f) {
    case VK_FORMAT_R8_UNORM: return Format::R8Unorm;
    case VK_FORMAT_R8G8_UNORM: return Format::RG8Unorm;
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8Srgb;
    case VK_FORMAT_R16_SFLOAT: return Format::R16Float;
    case VK_FORMAT_R16G16_SFLOAT: return Format::RG16Float;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16Float;
    case VK_FORMAT_R32_SFLOAT: return Format::R32Float;
    case VK_FORMAT_R32G32_SFLOAT: return Format::RG32Float;
    case VK_FORMAT_R32G32B32_SFLOAT: return Format::RGB32Float;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::RGBA32Float;
    case VK_FORMAT_D16_UNORM: return Format::D16Unorm;
    case VK_FORMAT_D32_SFLOAT: return Format::D32Float;
    default: return Format::Undefined;
  }
}

void nameObject(DeviceCore& core, VkObjectType type, std::uint64_t handle, const std::string& name) {
  if (core.setObjectName == nullptr || name.empty() || handle == 0) return;
  VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
  info.objectType = type;
  info.objectHandle = handle;
  info.pObjectName = name.c_str();
  core.setObjectName(core.device, &info);
}

// ================================================================================================
// DeferredDeletes / DeviceCore
// ================================================================================================

void DeferredDeletes::add(std::uint64_t serial, std::function<void()> fn) {
  std::lock_guard lock(mutex_);
  items_.emplace_back(serial, std::move(fn));
}

void DeferredDeletes::collect(std::uint64_t completed) {
  // Run outside the lock so destruction callbacks cannot deadlock against add().
  std::vector<std::function<void()>> ready;
  {
    std::lock_guard lock(mutex_);
    while (!items_.empty() && items_.front().first <= completed) {
      ready.push_back(std::move(items_.front().second));
      items_.pop_front();
    }
  }
  for (auto& fn : ready) fn();
}

void DeferredDeletes::flushAll() {
  std::deque<std::pair<std::uint64_t, std::function<void()>>> all;
  {
    std::lock_guard lock(mutex_);
    all.swap(items_);
  }
  for (auto& item : all) item.second();
}

std::size_t DeferredDeletes::pending() const {
  std::lock_guard lock(mutex_);
  return items_.size();
}

std::uint64_t DeviceCore::completedSerial() const {
  std::uint64_t value = 0;
  if (device != VK_NULL_HANDLE && timeline != VK_NULL_HANDLE) {
    vkGetSemaphoreCounterValue(device, timeline, &value);
  }
  return value;
}

void DeviceCore::deferDestroy(std::function<void()> fn) {
  // Everything submitted so far may reference the object; anything recorded but not yet submitted
  // must be kept alive by the caller until submit() returns (see Device documentation).
  const std::uint64_t serial = lastSubmitted.load();
  if (serial <= completedSerial()) {
    fn();
    return;
  }
  deferred.add(serial, std::move(fn));
}

Result<void> DeviceCore::waitSerial(std::uint64_t serial) {
  if (serial == 0) return {};
  if (serial > lastSubmitted.load()) {
    return makeError(ErrorCode::InvalidArgument, "serial {} has not been submitted", serial);
  }
  VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  info.semaphoreCount = 1;
  info.pSemaphores = &timeline;
  info.pValues = &serial;
  MINGOL_VK_TRY(vkWaitSemaphores(device, &info, UINT64_MAX), "vkWaitSemaphores");
  collectGarbage();
  return {};
}

DeviceCore::~DeviceCore() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    deferred.flushAll();
    if (timeline != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline, nullptr);
    if (allocator != nullptr) vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
  }
  if (instance != VK_NULL_HANDLE) {
    if (messenger != VK_NULL_HANDLE) {
      auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      if (destroy != nullptr) destroy(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
  }
}

// ================================================================================================
// Resource destructors
// ================================================================================================

VulkanBuffer::~VulkanBuffer() {
  if (buffer == VK_NULL_HANDLE) return;
  VmaAllocator allocator = core->allocator;
  VkBuffer b = buffer;
  void* a = allocation;
  core->deferDestroy([allocator, b, a]() { vmaDestroyBuffer(allocator, b, static_cast<VmaAllocation>(a)); });
}

void VulkanBuffer::flush(std::uint64_t offset, std::uint64_t size) {
  if (allocation != nullptr && mapped_ != nullptr) {
    vmaFlushAllocation(core->allocator, static_cast<VmaAllocation>(allocation), offset, size);
  }
}

void VulkanBuffer::invalidate(std::uint64_t offset, std::uint64_t size) {
  if (allocation != nullptr && mapped_ != nullptr) {
    vmaInvalidateAllocation(core->allocator, static_cast<VmaAllocation>(allocation), offset, size);
  }
}

VulkanTexture::~VulkanTexture() {
  std::vector<VkImageView> views;
  for (auto& [key, v] : subViews_) views.push_back(v);
  if (view != VK_NULL_HANDLE) views.push_back(view);
  VkDevice device = core->device;
  VmaAllocator allocator = core->allocator;
  VkImage img = ownsImage ? image : VK_NULL_HANDLE;
  void* alloc = allocation;
  core->deferDestroy([device, allocator, views, img, alloc]() {
    for (VkImageView v : views) vkDestroyImageView(device, v, nullptr);
    if (img != VK_NULL_HANDLE) vmaDestroyImage(allocator, img, static_cast<VmaAllocation>(alloc));
  });
}

VkImageAspectFlags VulkanTexture::aspect() const noexcept {
  return isDepthFormat(desc_.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkImageView VulkanTexture::attachmentView(std::uint32_t mip, std::uint32_t layer) {
  std::lock_guard lock(viewMutex_);
  const auto key = std::make_pair(mip, layer);
  if (auto it = subViews_.find(key); it != subViews_.end()) return it->second;

  VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  ci.image = image;
  ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ci.format = toVk(desc_.format);
  ci.subresourceRange = {aspect(), mip, 1, layer, 1};
  VkImageView v = VK_NULL_HANDLE;
  if (vkCreateImageView(core->device, &ci, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
  subViews_.emplace(key, v);
  return v;
}

VulkanSampler::~VulkanSampler() {
  VkDevice device = core->device;
  VkSampler s = sampler;
  core->deferDestroy([device, s]() { vkDestroySampler(device, s, nullptr); });
}

VulkanShaderModule::~VulkanShaderModule() {
  VkDevice device = core->device;
  VkShaderModule m = module;
  core->deferDestroy([device, m]() { vkDestroyShaderModule(device, m, nullptr); });
}

VulkanBindGroupLayout::~VulkanBindGroupLayout() {
  VkDevice device = core->device;
  VkDescriptorSetLayout l = layout;
  core->deferDestroy([device, l]() { vkDestroyDescriptorSetLayout(device, l, nullptr); });
}

VulkanBindGroup::~VulkanBindGroup() {
  VkDevice device = core->device;
  VkDescriptorPool p = pool;
  core->deferDestroy([device, p]() { vkDestroyDescriptorPool(device, p, nullptr); });
}

VulkanPipeline::~VulkanPipeline() {
  VkDevice device = core->device;
  VkPipeline p = pipeline;
  VkPipelineLayout l = layout;
  core->deferDestroy([device, p, l]() {
    vkDestroyPipeline(device, p, nullptr);
    vkDestroyPipelineLayout(device, l, nullptr);
  });
}

// ================================================================================================
// Conversions
// ================================================================================================

namespace {

using ::mingol::rhi::vk::toVk;  // keep toVk(Format) visible next to the overloads below

VkBufferUsageFlags toVk(BufferUsage u) {
  VkBufferUsageFlags f = 0;
  if (hasFlag(u, BufferUsage::Vertex)) f |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (hasFlag(u, BufferUsage::Index)) f |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if (hasFlag(u, BufferUsage::Uniform)) f |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (hasFlag(u, BufferUsage::Storage)) f |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  if (hasFlag(u, BufferUsage::TransferSrc)) f |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (hasFlag(u, BufferUsage::TransferDst)) f |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  return f;
}

VkImageUsageFlags toVk(TextureUsage u) {
  VkImageUsageFlags f = 0;
  if (hasFlag(u, TextureUsage::Sampled)) f |= VK_IMAGE_USAGE_SAMPLED_BIT;
  if (hasFlag(u, TextureUsage::ColorAttachment)) f |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (hasFlag(u, TextureUsage::DepthStencil)) f |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (hasFlag(u, TextureUsage::TransferSrc)) f |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (hasFlag(u, TextureUsage::TransferDst)) f |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (hasFlag(u, TextureUsage::Storage)) f |= VK_IMAGE_USAGE_STORAGE_BIT;
  return f;
}

VkShaderStageFlags toVkStages(ShaderStage s) {
  VkShaderStageFlags f = 0;
  if (hasFlag(s, ShaderStage::Vertex)) f |= VK_SHADER_STAGE_VERTEX_BIT;
  if (hasFlag(s, ShaderStage::Fragment)) f |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (hasFlag(s, ShaderStage::Compute)) f |= VK_SHADER_STAGE_COMPUTE_BIT;
  return f;
}

VkDescriptorType toVk(BindingType t) {
  switch (t) {
    case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  }
  return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

VkFilter toVk(FilterMode m) { return m == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

VkSamplerAddressMode toVk(AddressMode m) {
  switch (m) {
    case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  }
  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkCompareOp toVk(CompareOp op) {
  switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
  }
  return VK_COMPARE_OP_ALWAYS;
}

VkBlendFactor toVk(BlendFactor f) {
  switch (f) {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  }
  return VK_BLEND_FACTOR_ONE;
}

VkBlendOp toVk(BlendOp op) {
  switch (op) {
    case BlendOp::Add: return VK_BLEND_OP_ADD;
    case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min: return VK_BLEND_OP_MIN;
    case BlendOp::Max: return VK_BLEND_OP_MAX;
  }
  return VK_BLEND_OP_ADD;
}

VkPrimitiveTopology toVk(Topology t) {
  switch (t) {
    case Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  }
  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

DeviceType toDeviceType(VkPhysicalDeviceType t) {
  switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return DeviceType::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return DeviceType::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return DeviceType::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return DeviceType::Cpu;
    default: return DeviceType::Other;
  }
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* user) {
  auto* core = static_cast<DeviceCore*>(user);
  const char* msg = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "(no message)";
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    if (core != nullptr) core->validationErrors.fetch_add(1);
    MINGOL_LOG_ERROR(kLogCat, "validation: {}", msg);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    MINGOL_LOG_WARN(kLogCat, "validation: {}", msg);
  }
  return VK_FALSE;
}

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
  return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

}  // namespace

// ================================================================================================
// VulkanDevice
// ================================================================================================

namespace {

class VulkanDevice final : public Device {
 public:
  static Result<std::unique_ptr<VulkanDevice>> create(const DeviceDesc& desc);

  const DeviceInfo& info() const noexcept override { return info_; }

  Result<std::shared_ptr<Buffer>> createBuffer(const BufferDesc& desc) override;
  Result<std::shared_ptr<Texture>> createTexture(const TextureDesc& desc) override;
  Result<std::shared_ptr<Sampler>> createSampler(const SamplerDesc& desc) override;
  Result<std::shared_ptr<ShaderModule>> createShaderModule(const ShaderModuleDesc& desc) override;
  Result<std::shared_ptr<BindGroupLayout>> createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
  Result<std::shared_ptr<BindGroup>> createBindGroup(const BindGroupDesc& desc) override;
  Result<std::shared_ptr<GraphicsPipeline>> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
  Result<std::unique_ptr<CommandList>> createCommandList() override;
  Result<std::unique_ptr<Swapchain>> createSwapchain(const SwapchainDesc& desc) override;

  Result<std::uint64_t> submit(CommandList& list, Swapchain* presenting) override;
  bool isComplete(std::uint64_t serial) const override { return serial <= core_->completedSerial(); }
  Result<void> waitForSerial(std::uint64_t serial) override { return core_->waitSerial(serial); }
  Result<void> waitIdle() override;
  std::uint64_t validationErrorCount() const noexcept override { return core_->validationErrors.load(); }

 private:
  std::shared_ptr<DeviceCore> core_;
  DeviceInfo info_;
};

Result<std::unique_ptr<VulkanDevice>> VulkanDevice::create(const DeviceDesc& desc) {
  auto core = std::make_shared<DeviceCore>();
  auto self = std::unique_ptr<VulkanDevice>(new VulkanDevice());

  // ---- Instance ------------------------------------------------------------------------------
  std::uint32_t extCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
  std::vector<VkExtensionProperties> instanceExts(extCount);
  vkEnumerateInstanceExtensionProperties(nullptr, &extCount, instanceExts.data());

  std::vector<const char*> enabledExts;
  auto addIf = [&](const char* name) {
    if (hasExtension(instanceExts, name)) enabledExts.push_back(name);
  };
  addIf(VK_KHR_SURFACE_EXTENSION_NAME);
  addIf(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
  addIf(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
  addIf(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
  addIf(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
  addIf(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif

  std::vector<const char*> layers;
  bool validation = false;
  if (desc.enableValidation) {
    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> available(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, available.data());
    const bool layerPresent = std::any_of(available.begin(), available.end(), [](const VkLayerProperties& l) {
      return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    });
    if (layerPresent && hasExtension(instanceExts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      enabledExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      validation = true;
    } else {
      MINGOL_LOG_WARN(kLogCat, "validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = desc.appName.c_str();
  app.pEngineName = "Mingol";
  app.apiVersion = VK_API_VERSION_1_3;

  VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  messengerInfo.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  messengerInfo.pfnUserCallback = debugCallback;
  messengerInfo.pUserData = core.get();

  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = static_cast<std::uint32_t>(enabledExts.size());
  ici.ppEnabledExtensionNames = enabledExts.data();
  ici.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
  ici.ppEnabledLayerNames = layers.data();
  if (validation) ici.pNext = &messengerInfo;  // also covers instance creation/destruction messages
  MINGOL_VK_TRY(vkCreateInstance(&ici, nullptr, &core->instance), "vkCreateInstance");

  if (validation) {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(core->instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create != nullptr) create(core->instance, &messengerInfo, nullptr, &core->messenger);
  }

  // ---- Physical device -----------------------------------------------------------------------
  std::uint32_t pdCount = 0;
  vkEnumeratePhysicalDevices(core->instance, &pdCount, nullptr);
  if (pdCount == 0) return makeError(ErrorCode::Unsupported, "no Vulkan physical devices found");
  std::vector<VkPhysicalDevice> pds(pdCount);
  vkEnumeratePhysicalDevices(core->instance, &pdCount, pds.data());

  struct Candidate {
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    int score = -1;
    std::uint32_t family = 0;
    VkPhysicalDeviceProperties props{};
    std::string driver;
  } best;

  std::string rejected;
  for (VkPhysicalDevice pd : pds) {
    VkPhysicalDeviceDriverProperties driverProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &driverProps;
    vkGetPhysicalDeviceProperties2(pd, &props2);
    const VkPhysicalDeviceProperties& props = props2.properties;

    auto reject = [&](const char* why) { rejected += std::string("\n  ") + props.deviceName + ": " + why; };

    if (props.apiVersion < VK_API_VERSION_1_3) { reject("Vulkan 1.3 not supported"); continue; }
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && !desc.allowSoftwareDevice) { reject("software device not allowed"); continue; }

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2) { reject("dynamicRendering/synchronization2 missing"); continue; }
    if (!f12.timelineSemaphore) { reject("timelineSemaphore missing"); continue; }

    std::uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qprops.data());
    constexpr VkQueueFlags kNeeded = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    std::uint32_t family = UINT32_MAX;
    for (std::uint32_t i = 0; i < qCount; ++i) {
      if ((qprops[i].queueFlags & kNeeded) == kNeeded) { family = i; break; }
    }
    if (family == UINT32_MAX) { reject("no graphics+compute queue family"); continue; }

    int score = 0;
    switch (props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 4; break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 3; break;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 2; break;
      case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 0; break;
      default: score = 1; break;
    }
    if (!desc.preferredDeviceName.empty() && std::string(props.deviceName).find(desc.preferredDeviceName) != std::string::npos) {
      score += 100;
    }
    if (score > best.score) {
      best.pd = pd;
      best.score = score;
      best.family = family;
      best.props = props;
      best.driver = std::string(driverProps.driverName) + " " + driverProps.driverInfo;
    }
  }
  if (best.pd == VK_NULL_HANDLE) {
    return makeError(ErrorCode::Unsupported, "no suitable Vulkan 1.3 device found{}", rejected);
  }
  core->physical = best.pd;
  core->queueFamily = best.family;

  // ---- Logical device ------------------------------------------------------------------------
  std::uint32_t devExtCount = 0;
  vkEnumerateDeviceExtensionProperties(best.pd, nullptr, &devExtCount, nullptr);
  std::vector<VkExtensionProperties> devExts(devExtCount);
  vkEnumerateDeviceExtensionProperties(best.pd, nullptr, &devExtCount, devExts.data());
  std::vector<const char*> enabledDevExts;
  if (hasExtension(devExts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
    enabledDevExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    core->swapchainSupported = true;
  }

  VkPhysicalDeviceFeatures supported{};
  vkGetPhysicalDeviceFeatures(best.pd, &supported);
  VkPhysicalDeviceFeatures enabledFeatures{};
  if (supported.samplerAnisotropy) {
    enabledFeatures.samplerAnisotropy = VK_TRUE;
    core->anisotropySupported = true;
  }
  core->maxAnisotropy = best.props.limits.maxSamplerAnisotropy;
  core->maxImageDimension2D = best.props.limits.maxImageDimension2D;

  VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.dynamicRendering = VK_TRUE;
  f13.synchronization2 = VK_TRUE;
  VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  f12.timelineSemaphore = VK_TRUE;
  f12.pNext = &f13;

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = best.family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;

  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.pNext = &f12;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<std::uint32_t>(enabledDevExts.size());
  dci.ppEnabledExtensionNames = enabledDevExts.data();
  dci.pEnabledFeatures = &enabledFeatures;
  MINGOL_VK_TRY(vkCreateDevice(best.pd, &dci, nullptr, &core->device), "vkCreateDevice");
  vkGetDeviceQueue(core->device, best.family, 0, &core->queue);

  if (validation) {
    core->setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(core->instance, "vkSetDebugUtilsObjectNameEXT"));
  }

  // ---- Timeline semaphore, allocator ---------------------------------------------------------
  VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  typeInfo.initialValue = 0;
  VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  sci.pNext = &typeInfo;
  MINGOL_VK_TRY(vkCreateSemaphore(core->device, &sci, nullptr, &core->timeline), "vkCreateSemaphore(timeline)");

  VmaAllocatorCreateInfo aci{};
  aci.instance = core->instance;
  aci.physicalDevice = core->physical;
  aci.device = core->device;
  aci.vulkanApiVersion = VK_API_VERSION_1_3;
  MINGOL_VK_TRY(vmaCreateAllocator(&aci, &core->allocator), "vmaCreateAllocator");

  // ---- Info ----------------------------------------------------------------------------------
  self->info_.name = best.props.deviceName;
  self->info_.driver = best.driver;
  self->info_.type = toDeviceType(best.props.deviceType);
  self->info_.apiMajor = VK_API_VERSION_MAJOR(best.props.apiVersion);
  self->info_.apiMinor = VK_API_VERSION_MINOR(best.props.apiVersion);
  self->info_.apiPatch = VK_API_VERSION_PATCH(best.props.apiVersion);
  self->info_.maxTextureSize = best.props.limits.maxImageDimension2D;
  self->info_.maxSamplerAnisotropy = core->anisotropySupported ? best.props.limits.maxSamplerAnisotropy : 1.0f;
  self->info_.maxPushConstantSize = best.props.limits.maxPushConstantsSize;
  self->info_.validationEnabled = validation;
  self->core_ = std::move(core);

  MINGOL_LOG_INFO(kLogCat, "Vulkan device: {} ({}), API {}.{}.{}{}", self->info_.name, self->info_.driver,
                  self->info_.apiMajor, self->info_.apiMinor, self->info_.apiPatch, validation ? ", validation on" : "");
  return self;
}

// ---- Buffers / textures ------------------------------------------------------------------------

Result<std::shared_ptr<Buffer>> VulkanDevice::createBuffer(const BufferDesc& desc) {
  if (desc.size == 0) return makeError(ErrorCode::InvalidArgument, "buffer '{}' has size 0", desc.debugName);
  if (desc.usage == BufferUsage::None) {
    return makeError(ErrorCode::InvalidArgument, "buffer '{}' has no usage flags", desc.debugName);
  }

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = desc.size;
  bci.usage = toVk(desc.usage);
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo aci{};
  switch (desc.memory) {
    case MemoryType::GpuOnly:
      aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
      break;
    case MemoryType::CpuToGpu:
      aci.usage = VMA_MEMORY_USAGE_AUTO;
      aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      break;
    case MemoryType::GpuToCpu:
      aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
      aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      break;
  }

  auto buffer = std::make_shared<VulkanBuffer>(desc, core_);
  VmaAllocation allocation = nullptr;
  VmaAllocationInfo allocInfo{};
  MINGOL_VK_TRY(vmaCreateBuffer(core_->allocator, &bci, &aci, &buffer->buffer, &allocation, &allocInfo), "vmaCreateBuffer");
  buffer->allocation = allocation;
  if (desc.memory != MemoryType::GpuOnly) {
    if (allocInfo.pMappedData == nullptr) {
      return makeError(ErrorCode::Internal, "buffer '{}' could not be mapped", desc.debugName);
    }
    buffer->setMapped(allocInfo.pMappedData);
  }
  nameObject(*core_, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer->buffer), desc.debugName);
  return std::static_pointer_cast<Buffer>(buffer);
}

Result<std::shared_ptr<Texture>> VulkanDevice::createTexture(const TextureDesc& desc) {
  auto bad = [&](const char* why) { return makeError(ErrorCode::InvalidArgument, "texture '{}': {}", desc.debugName, why); };
  if (desc.width == 0 || desc.height == 0) return bad("zero size");
  if (desc.width > core_->maxImageDimension2D || desc.height > core_->maxImageDimension2D) return bad("exceeds device maximum size");
  if (desc.format == Format::Undefined) return bad("undefined format");
  if (desc.usage == TextureUsage::None) return bad("no usage flags");
  if (desc.mipLevels == 0 || desc.mipLevels > fullMipCount(desc.width, desc.height)) return bad("invalid mip level count");
  if (desc.arrayLayers == 0) return bad("zero array layers");
  if (desc.cube && (desc.arrayLayers != 6 || desc.width != desc.height)) return bad("cube textures need 6 square layers");
  const bool depth = isDepthFormat(desc.format);
  if (depth && hasFlag(desc.usage, TextureUsage::ColorAttachment)) return bad("depth format used as colour attachment");
  if (!depth && hasFlag(desc.usage, TextureUsage::DepthStencil)) return bad("colour format used as depth attachment");

  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.flags = desc.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = toVk(desc.format);
  ici.extent = {desc.width, desc.height, 1};
  ici.mipLevels = desc.mipLevels;
  ici.arrayLayers = desc.arrayLayers;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = toVk(desc.usage);
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo aci{};
  aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

  auto texture = std::make_shared<VulkanTexture>(desc, core_);
  VmaAllocation allocation = nullptr;
  const VkResult r = vmaCreateImage(core_->allocator, &ici, &aci, &texture->image, &allocation, nullptr);
  if (r != VK_SUCCESS) return vkError(r, "vmaCreateImage");
  texture->allocation = allocation;

  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = texture->image;
  vci.viewType = desc.cube ? VK_IMAGE_VIEW_TYPE_CUBE
                           : (desc.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
  vci.format = ici.format;
  vci.subresourceRange = {texture->aspect(), 0, desc.mipLevels, 0, desc.arrayLayers};
  MINGOL_VK_TRY(vkCreateImageView(core_->device, &vci, nullptr, &texture->view), "vkCreateImageView");
  nameObject(*core_, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(texture->image), desc.debugName);
  return std::static_pointer_cast<Texture>(texture);
}

Result<std::shared_ptr<Sampler>> VulkanDevice::createSampler(const SamplerDesc& desc) {
  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = toVk(desc.magFilter);
  sci.minFilter = toVk(desc.minFilter);
  sci.mipmapMode = desc.mipFilter == FilterMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sci.addressModeU = toVk(desc.addressU);
  sci.addressModeV = toVk(desc.addressV);
  sci.addressModeW = toVk(desc.addressW);
  const float aniso = std::min(std::max(desc.maxAnisotropy, 1.0f), core_->maxAnisotropy);
  sci.anisotropyEnable = (core_->anisotropySupported && aniso > 1.0f) ? VK_TRUE : VK_FALSE;
  sci.maxAnisotropy = sci.anisotropyEnable ? aniso : 1.0f;
  sci.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
  sci.compareOp = toVk(desc.compareOp);
  sci.minLod = 0.0f;
  sci.maxLod = desc.maxLod;
  sci.borderColor = desc.borderWhite ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

  auto sampler = std::make_shared<VulkanSampler>(desc, core_);
  MINGOL_VK_TRY(vkCreateSampler(core_->device, &sci, nullptr, &sampler->sampler), "vkCreateSampler");
  return std::static_pointer_cast<Sampler>(sampler);
}

Result<std::shared_ptr<ShaderModule>> VulkanDevice::createShaderModule(const ShaderModuleDesc& desc) {
  if (desc.spirv.size() < 5 || desc.spirv[0] != 0x07230203u) {
    return makeError(ErrorCode::Corrupt, "shader '{}': not a valid SPIR-V module", desc.debugName);
  }
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = desc.spirv.size() * sizeof(std::uint32_t);
  ci.pCode = desc.spirv.data();
  auto module = std::make_shared<VulkanShaderModule>(desc.stage, desc.entryPoint, core_);
  MINGOL_VK_TRY(vkCreateShaderModule(core_->device, &ci, nullptr, &module->module), "vkCreateShaderModule");
  nameObject(*core_, VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<std::uint64_t>(module->module), desc.debugName);
  return std::static_pointer_cast<ShaderModule>(module);
}

// ---- Bind groups -------------------------------------------------------------------------------

Result<std::shared_ptr<BindGroupLayout>> VulkanDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
  if (desc.bindings.empty()) {
    return makeError(ErrorCode::InvalidArgument, "bind group layout '{}' has no bindings", desc.debugName);
  }
  std::set<std::uint32_t> seen;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  for (const BindingDesc& b : desc.bindings) {
    if (b.count == 0) return makeError(ErrorCode::InvalidArgument, "binding {} has count 0", b.binding);
    if (!seen.insert(b.binding).second) return makeError(ErrorCode::InvalidArgument, "binding {} declared twice", b.binding);
    VkDescriptorSetLayoutBinding vb{};
    vb.binding = b.binding;
    vb.descriptorType = toVk(b.type);
    vb.descriptorCount = b.count;
    vb.stageFlags = toVkStages(b.stages);
    bindings.push_back(vb);
  }
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  ci.bindingCount = static_cast<std::uint32_t>(bindings.size());
  ci.pBindings = bindings.data();
  auto layout = std::make_shared<VulkanBindGroupLayout>(desc, core_);
  MINGOL_VK_TRY(vkCreateDescriptorSetLayout(core_->device, &ci, nullptr, &layout->layout), "vkCreateDescriptorSetLayout");
  return std::static_pointer_cast<BindGroupLayout>(layout);
}

Result<std::shared_ptr<BindGroup>> VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
  auto* layout = dynamic_cast<VulkanBindGroupLayout*>(desc.layout.get());
  if (layout == nullptr) return makeError(ErrorCode::InvalidArgument, "bind group '{}' has no valid layout", desc.debugName);
  const BindGroupLayoutDesc& ld = layout->desc();

  // Every (binding, element) of the layout must be provided exactly once, with the right resource.
  std::set<std::pair<std::uint32_t, std::uint32_t>> provided;
  for (const BindGroupEntry& e : desc.entries) {
    const BindingDesc* bd = nullptr;
    for (const BindingDesc& b : ld.bindings) if (b.binding == e.binding) bd = &b;
    if (bd == nullptr) return makeError(ErrorCode::InvalidArgument, "entry for unknown binding {}", e.binding);
    if (e.arrayElement >= bd->count) return makeError(ErrorCode::OutOfRange, "binding {} element {} out of range", e.binding, e.arrayElement);
    if (!provided.emplace(e.binding, e.arrayElement).second) {
      return makeError(ErrorCode::InvalidArgument, "binding {} element {} provided twice", e.binding, e.arrayElement);
    }
    if (bd->type == BindingType::SampledTexture) {
      auto* tex = dynamic_cast<VulkanTexture*>(e.texture.get());
      if (tex == nullptr || dynamic_cast<VulkanSampler*>(e.sampler.get()) == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "binding {} needs a texture and a sampler", e.binding);
      }
      if (!hasFlag(tex->desc().usage, TextureUsage::Sampled)) {
        return makeError(ErrorCode::InvalidArgument, "binding {}: texture lacks Sampled usage", e.binding);
      }
    } else {
      auto* buf = dynamic_cast<VulkanBuffer*>(e.buffer.get());
      if (buf == nullptr) return makeError(ErrorCode::InvalidArgument, "binding {} needs a buffer", e.binding);
      const BufferUsage need = bd->type == BindingType::UniformBuffer ? BufferUsage::Uniform : BufferUsage::Storage;
      if (!hasFlag(buf->desc().usage, need)) {
        return makeError(ErrorCode::InvalidArgument, "binding {}: buffer lacks required usage", e.binding);
      }
      if (e.offset >= buf->size() || (e.range != kWholeSize && e.offset + e.range > buf->size())) {
        return makeError(ErrorCode::OutOfRange, "binding {}: buffer range out of bounds", e.binding);
      }
    }
  }
  for (const BindingDesc& b : ld.bindings) {
    for (std::uint32_t i = 0; i < b.count; ++i) {
      if (provided.count({b.binding, i}) == 0) {
        return makeError(ErrorCode::InvalidArgument, "bind group '{}': binding {} element {} not provided", desc.debugName, b.binding, i);
      }
    }
  }

  // One pool per group, sized exactly for the layout.
  std::map<VkDescriptorType, std::uint32_t> counts;
  for (const BindingDesc& b : ld.bindings) counts[toVk(b.type)] += b.count;
  std::vector<VkDescriptorPoolSize> sizes;
  for (auto& [type, count] : counts) sizes.push_back({type, count});
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pci.maxSets = 1;
  pci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
  pci.pPoolSizes = sizes.data();

  auto group = std::make_shared<VulkanBindGroup>(desc, core_);
  MINGOL_VK_TRY(vkCreateDescriptorPool(core_->device, &pci, nullptr, &group->pool), "vkCreateDescriptorPool");
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = group->pool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layout->layout;
  MINGOL_VK_TRY(vkAllocateDescriptorSets(core_->device, &ai, &group->set), "vkAllocateDescriptorSets");

  std::vector<VkDescriptorBufferInfo> bufferInfos(desc.entries.size());
  std::vector<VkDescriptorImageInfo> imageInfos(desc.entries.size());
  std::vector<VkWriteDescriptorSet> writes;
  for (std::size_t i = 0; i < desc.entries.size(); ++i) {
    const BindGroupEntry& e = desc.entries[i];
    const BindingDesc* bd = nullptr;
    for (const BindingDesc& b : ld.bindings) if (b.binding == e.binding) bd = &b;

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = group->set;
    w.dstBinding = e.binding;
    w.dstArrayElement = e.arrayElement;
    w.descriptorCount = 1;
    w.descriptorType = toVk(bd->type);
    if (bd->type == BindingType::SampledTexture) {
      auto* tex = static_cast<VulkanTexture*>(e.texture.get());
      imageInfos[i].sampler = static_cast<VulkanSampler*>(e.sampler.get())->sampler;
      imageInfos[i].imageView = tex->view;
      imageInfos[i].imageLayout = stateInfo(TextureState::ShaderRead, isDepthFormat(tex->desc().format)).layout;
      w.pImageInfo = &imageInfos[i];
    } else {
      auto* buf = static_cast<VulkanBuffer*>(e.buffer.get());
      bufferInfos[i] = {buf->buffer, e.offset, e.range};
      w.pBufferInfo = &bufferInfos[i];
    }
    writes.push_back(w);
  }
  vkUpdateDescriptorSets(core_->device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  return std::static_pointer_cast<BindGroup>(group);
}

// ---- Pipelines ---------------------------------------------------------------------------------

Result<std::shared_ptr<GraphicsPipeline>> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
  auto bad = [&](const char* why) { return makeError(ErrorCode::InvalidArgument, "pipeline '{}': {}", desc.debugName, why); };
  auto* vs = dynamic_cast<VulkanShaderModule*>(desc.vertexShader.get());
  if (vs == nullptr || vs->stage() != ShaderStage::Vertex) return bad("needs a vertex shader");
  auto* fs = dynamic_cast<VulkanShaderModule*>(desc.fragmentShader.get());
  if (desc.fragmentShader && (fs == nullptr || fs->stage() != ShaderStage::Fragment)) return bad("fragment shader has wrong stage");
  if (desc.colorFormats.empty() && desc.depthFormat == Format::Undefined) return bad("no colour or depth attachments");
  if (desc.depthFormat != Format::Undefined && !isDepthFormat(desc.depthFormat)) return bad("depthFormat is not a depth format");
  if (!desc.blend.empty() && desc.blend.size() != desc.colorFormats.size()) return bad("blend state count must match colour attachments");
  if (desc.pushConstantSize > info_.maxPushConstantSize || desc.pushConstantSize % 4 != 0) return bad("invalid push constant size");

  std::vector<VkDescriptorSetLayout> setLayouts;
  for (const auto& l : desc.setLayouts) {
    auto* vl = dynamic_cast<VulkanBindGroupLayout*>(l.get());
    if (vl == nullptr) return bad("null bind group layout");
    setLayouts.push_back(vl->layout);
  }

  auto pipeline = std::make_shared<VulkanPipeline>(desc, core_);

  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, desc.pushConstantSize};
  VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
  plci.pSetLayouts = setLayouts.data();
  plci.pushConstantRangeCount = desc.pushConstantSize > 0 ? 1 : 0;
  plci.pPushConstantRanges = &pcr;
  MINGOL_VK_TRY(vkCreatePipelineLayout(core_->device, &plci, nullptr, &pipeline->layout), "vkCreatePipelineLayout");

  std::vector<VkPipelineShaderStageCreateInfo> stages;
  {
    VkPipelineShaderStageCreateInfo s{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    s.stage = VK_SHADER_STAGE_VERTEX_BIT;
    s.module = vs->module;
    s.pName = vs->entryPoint().c_str();
    stages.push_back(s);
    if (fs != nullptr) {
      s.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      s.module = fs->module;
      s.pName = fs->entryPoint().c_str();
      stages.push_back(s);
    }
  }

  std::vector<VkVertexInputBindingDescription> vbs;
  for (const VertexBinding& b : desc.vertexBindings) {
    vbs.push_back({b.binding, b.stride, b.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
  }
  std::vector<VkVertexInputAttributeDescription> vas;
  for (const VertexAttribute& a : desc.vertexAttributes) vas.push_back({a.location, a.binding, toVk(a.format), a.offset});
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vbs.size());
  vi.pVertexBindingDescriptions = vbs.data();
  vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vas.size());
  vi.pVertexAttributeDescriptions = vas.data();

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = toVk(desc.topology);

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = desc.cullMode == CullMode::None ? VK_CULL_MODE_NONE
                : desc.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
  rs.frontFace = desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
  rs.depthBiasEnable = desc.depthBias ? VK_TRUE : VK_FALSE;
  rs.depthBiasConstantFactor = desc.depthBiasConstant;
  rs.depthBiasSlopeFactor = desc.depthBiasSlope;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = (desc.depthFormat != Format::Undefined && desc.depthTest) ? VK_TRUE : VK_FALSE;
  ds.depthWriteEnable = (desc.depthFormat != Format::Undefined && desc.depthWrite) ? VK_TRUE : VK_FALSE;
  ds.depthCompareOp = toVk(desc.depthCompare);

  std::vector<VkPipelineColorBlendAttachmentState> blends;
  std::vector<VkFormat> colorFormats;
  for (std::size_t i = 0; i < desc.colorFormats.size(); ++i) {
    colorFormats.push_back(toVk(desc.colorFormats[i]));
    VkPipelineColorBlendAttachmentState b{};
    b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (!desc.blend.empty() && desc.blend[i].enable) {
      const BlendState& s = desc.blend[i];
      b.blendEnable = VK_TRUE;
      b.srcColorBlendFactor = toVk(s.srcColor);
      b.dstColorBlendFactor = toVk(s.dstColor);
      b.colorBlendOp = toVk(s.colorOp);
      b.srcAlphaBlendFactor = toVk(s.srcAlpha);
      b.dstAlphaBlendFactor = toVk(s.dstAlpha);
      b.alphaBlendOp = toVk(s.alphaOp);
    }
    blends.push_back(b);
  }
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = static_cast<std::uint32_t>(blends.size());
  cb.pAttachments = blends.data();

  const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynamicStates;

  VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  ri.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
  ri.pColorAttachmentFormats = colorFormats.data();
  ri.depthAttachmentFormat = toVk(desc.depthFormat);

  VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gp.pNext = &ri;
  gp.stageCount = static_cast<std::uint32_t>(stages.size());
  gp.pStages = stages.data();
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vp;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dyn;
  gp.layout = pipeline->layout;
  MINGOL_VK_TRY(vkCreateGraphicsPipelines(core_->device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline->pipeline), "vkCreateGraphicsPipelines");
  nameObject(*core_, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline->pipeline), desc.debugName);
  return std::static_pointer_cast<GraphicsPipeline>(pipeline);
}

// ---- Command lists, swapchains, submission -----------------------------------------------------

Result<std::unique_ptr<CommandList>> VulkanDevice::createCommandList() {
  MINGOL_TRY_ASSIGN(auto list, VulkanCommandList::create(core_));
  return std::unique_ptr<CommandList>(std::move(list));
}

Result<std::unique_ptr<Swapchain>> VulkanDevice::createSwapchain(const SwapchainDesc& desc) {
  MINGOL_TRY_ASSIGN(auto sc, VulkanSwapchain::create(core_, desc));
  return std::unique_ptr<Swapchain>(std::move(sc));
}

Result<std::uint64_t> VulkanDevice::submit(CommandList& list, Swapchain* presenting) {
  auto* cl = dynamic_cast<VulkanCommandList*>(&list);
  if (cl == nullptr) return makeError(ErrorCode::InvalidArgument, "command list belongs to another backend");
  if (!cl->executable()) return makeError(ErrorCode::InvalidArgument, "command list was not ended");

  VulkanSwapchain* sc = nullptr;
  if (presenting != nullptr) {
    sc = dynamic_cast<VulkanSwapchain*>(presenting);
    if (sc == nullptr) return makeError(ErrorCode::InvalidArgument, "swapchain belongs to another backend");
    if (!sc->acquired()) return makeError(ErrorCode::InvalidArgument, "swapchain has no acquired image");
    if (sc->submittedForPresent()) return makeError(ErrorCode::InvalidArgument, "acquired image already submitted");
    if (sc->currentTexture()->state() != TextureState::Present) {
      return makeError(ErrorCode::InvalidArgument, "swapchain image must be in Present state at the end of the list");
    }
  }

  VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cbi.commandBuffer = cl->handle();

  std::uint64_t serial = 0;
  {
    std::lock_guard lock(core_->queueMutex);
    serial = core_->lastSubmitted.load() + 1;

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    VkSemaphoreSubmitInfo signals[2]{{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
    signals[0].semaphore = core_->timeline;
    signals[0].value = serial;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = signals;
    if (sc != nullptr) {
      waitInfo.semaphore = sc->acquireSemaphore();
      waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
      si.waitSemaphoreInfoCount = 1;
      si.pWaitSemaphoreInfos = &waitInfo;
      signals[1].semaphore = sc->renderFinishedSemaphore();
      signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
      si.signalSemaphoreInfoCount = 2;
    }
    MINGOL_VK_TRY(vkQueueSubmit2(core_->queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit2");
    core_->lastSubmitted.store(serial);  // only advanced once the submit was accepted
  }
  cl->markSubmitted(serial);
  if (sc != nullptr) sc->markSubmitted(serial);
  core_->collectGarbage();
  return serial;
}

Result<void> VulkanDevice::waitIdle() {
  {
    std::lock_guard lock(core_->queueMutex);
    MINGOL_VK_TRY(vkDeviceWaitIdle(core_->device), "vkDeviceWaitIdle");
  }
  core_->collectGarbage();
  return {};
}

}  // namespace

}  // namespace mingol::rhi::vk

namespace mingol::rhi {

Result<std::unique_ptr<Device>> createVulkanDevice(const DeviceDesc& desc) {
  MINGOL_TRY_ASSIGN(auto device, vk::VulkanDevice::create(desc));
  return std::unique_ptr<Device>(std::move(device));
}

}  // namespace mingol::rhi
