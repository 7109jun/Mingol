// SPDX-License-Identifier: GPL-2.0-only
// Internal header shared by the Vulkan backend translation units.
#pragma once

#include <vulkan/vulkan.h>

// With VK_USE_PLATFORM_XLIB_KHR, vulkan.h pulls in <X11/Xlib.h>, whose macros collide with our
// enumerator names (BufferUsage::None, CompareOp::Always, ...). Drop them; the Xlib types we need
// (Display, Window) are typedefs and stay available.
#ifdef None
#undef None
#endif
#ifdef Always
#undef Always
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Success
#undef Success
#endif

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/log.hpp"
#include "mingol/rhi/device.hpp"

// Forward-declare VMA's handle types so only vk_device.cpp includes the (huge) header.
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;

namespace mingol::rhi::vk {

constexpr const char* kLogCat = "rhi";

const char* resultName(VkResult r) noexcept;

inline Error vkError(VkResult r, const char* what) {
  ErrorCode code = ErrorCode::Internal;
  if (r == VK_ERROR_OUT_OF_HOST_MEMORY || r == VK_ERROR_OUT_OF_DEVICE_MEMORY) code = ErrorCode::OutOfMemory;
  else if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) code = ErrorCode::OutOfDate;
  else if (r == VK_ERROR_FEATURE_NOT_PRESENT || r == VK_ERROR_EXTENSION_NOT_PRESENT ||
           r == VK_ERROR_INCOMPATIBLE_DRIVER || r == VK_ERROR_FORMAT_NOT_SUPPORTED)
    code = ErrorCode::Unsupported;
  else if (r == VK_TIMEOUT) code = ErrorCode::Timeout;
  return Error(code, std::string(what) + " failed: " + resultName(r));
}

#define MINGOL_VK_TRY(expr, what)                                       \
  do {                                                                  \
    const VkResult mingolVkResult_ = (expr);                            \
    if (mingolVkResult_ != VK_SUCCESS) return ::mingol::rhi::vk::vkError(mingolVkResult_, what); \
  } while (false)

VkFormat toVk(Format f) noexcept;
Format fromVk(VkFormat f) noexcept;

// Everything a resource needs to destroy itself later. Resources hold a shared_ptr to this so they
// may outlive the VulkanDevice object itself without dangling (they then destroy immediately,
// after the device has waited for idle in its destructor).
struct DeviceCore;

// Queue of destruction callbacks, each tagged with the submission serial after which it is safe.
class DeferredDeletes {
 public:
  void add(std::uint64_t serial, std::function<void()> fn);
  // Runs every entry whose serial <= completed.
  void collect(std::uint64_t completed);
  void flushAll();
  std::size_t pending() const;

 private:
  mutable std::mutex mutex_;
  std::deque<std::pair<std::uint64_t, std::function<void()>>> items_;
};

struct DeviceCore {
  DeviceCore() = default;
  DeviceCore(const DeviceCore&) = delete;
  DeviceCore& operator=(const DeviceCore&) = delete;
  ~DeviceCore();  // tears down device, allocator and instance once the last resource is gone

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queueFamily = 0;
  VmaAllocator allocator = nullptr;
  VkSemaphore timeline = VK_NULL_HANDLE;  // timeline semaphore: value N = submission N finished
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

  std::atomic<std::uint64_t> lastSubmitted{0};  // last serial handed out
  std::mutex queueMutex;                        // vkQueueSubmit / vkQueuePresent
  DeferredDeletes deferred;
  std::atomic<std::uint64_t> validationErrors{0};
  float maxAnisotropy = 1.0f;

  PFN_vkSetDebugUtilsObjectNameEXT setObjectName = nullptr;
  bool swapchainSupported = false;
  bool anisotropySupported = false;
  std::uint32_t maxImageDimension2D = 0;

  // Serial that has definitely completed on the GPU.
  std::uint64_t completedSerial() const;
  // Defers `fn` until all work submitted so far has completed.
  void deferDestroy(std::function<void()> fn);
  void collectGarbage() { deferred.collect(completedSerial()); }
  // Blocks until submission `serial` has finished on the GPU.
  Result<void> waitSerial(std::uint64_t serial);
};

}  // namespace mingol::rhi::vk
