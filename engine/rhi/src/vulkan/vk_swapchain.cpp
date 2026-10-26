// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>

#include "vk_objects.hpp"

#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#endif

namespace mingol::rhi::vk {

namespace {

Result<VkSurfaceKHR> createSurface(DeviceCore& core, const NativeWindow& w) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  switch (w.kind) {
    case NativeWindow::Kind::Headless: {
      auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
          vkGetInstanceProcAddr(core.instance, "vkCreateHeadlessSurfaceEXT"));
      if (create == nullptr) return makeError(ErrorCode::Unsupported, "VK_EXT_headless_surface is not available");
      VkHeadlessSurfaceCreateInfoEXT ci{VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
      MINGOL_VK_TRY(create(core.instance, &ci, nullptr, &surface), "vkCreateHeadlessSurfaceEXT");
      return surface;
    }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    case NativeWindow::Kind::Win32: {
      VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
      ci.hinstance = static_cast<HINSTANCE>(w.display);
      ci.hwnd = reinterpret_cast<HWND>(w.window);
      MINGOL_VK_TRY(vkCreateWin32SurfaceKHR(core.instance, &ci, nullptr, &surface), "vkCreateWin32SurfaceKHR");
      return surface;
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    case NativeWindow::Kind::Xlib: {
      VkXlibSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
      ci.dpy = static_cast<Display*>(w.display);
      ci.window = static_cast<Window>(w.window);
      MINGOL_VK_TRY(vkCreateXlibSurfaceKHR(core.instance, &ci, nullptr, &surface), "vkCreateXlibSurfaceKHR");
      return surface;
    }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
    case NativeWindow::Kind::Xcb: {
      VkXcbSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
      ci.connection = static_cast<xcb_connection_t*>(w.display);
      ci.window = static_cast<xcb_window_t>(w.window);
      MINGOL_VK_TRY(vkCreateXcbSurfaceKHR(core.instance, &ci, nullptr, &surface), "vkCreateXcbSurfaceKHR");
      return surface;
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    case NativeWindow::Kind::Wayland: {
      VkWaylandSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
      ci.display = static_cast<wl_display*>(w.display);
      ci.surface = reinterpret_cast<wl_surface*>(w.window);
      MINGOL_VK_TRY(vkCreateWaylandSurfaceKHR(core.instance, &ci, nullptr, &surface), "vkCreateWaylandSurfaceKHR");
      return surface;
    }
#endif
    default:
      break;
  }
  return makeError(ErrorCode::Unsupported, "window system kind {} is not supported on this platform", static_cast<int>(w.kind));
}

}  // namespace

Result<std::unique_ptr<VulkanSwapchain>> VulkanSwapchain::create(std::shared_ptr<DeviceCore> core, const SwapchainDesc& desc) {
  if (!core->swapchainSupported) return makeError(ErrorCode::Unsupported, "device has no VK_KHR_swapchain");
  auto sc = std::unique_ptr<VulkanSwapchain>(new VulkanSwapchain(core));
  sc->desc_ = desc;

  MINGOL_TRY_ASSIGN(VkSurfaceKHR surface, createSurface(*core, desc.window));
  sc->surface_ = surface;  // destroyed by ~VulkanSwapchain even if build() fails below

  VkBool32 supported = VK_FALSE;
  MINGOL_VK_TRY(vkGetPhysicalDeviceSurfaceSupportKHR(core->physical, core->queueFamily, surface, &supported),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
  if (!supported) return makeError(ErrorCode::Unsupported, "the graphics queue family cannot present to this surface");

  std::uint32_t w = desc.width != 0 ? desc.width : desc.window.width;
  std::uint32_t h = desc.height != 0 ? desc.height : desc.window.height;
  MINGOL_TRY(sc->build(w, h));
  return sc;
}

VulkanSwapchain::~VulkanSwapchain() {
  {
    // Presentation may still be in flight; the queue must be idle before anything is destroyed.
    std::lock_guard lock(core_->queueMutex);
    vkQueueWaitIdle(core_->queue);
  }
  destroyImagesAndSync();
  if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(core_->device, swapchain_, nullptr);
  if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(core_->instance, surface_, nullptr);
}

void VulkanSwapchain::destroyImagesAndSync() {
  images_.clear();  // textures destroy their own views
  for (VkSemaphore s : acquireSems_) vkDestroySemaphore(core_->device, s, nullptr);
  for (VkSemaphore s : renderDone_) vkDestroySemaphore(core_->device, s, nullptr);
  acquireSems_.clear();
  renderDone_.clear();
  acquireSlotSerial_.clear();
  acquired_ = false;
  readyToPresent_ = false;
}

Result<void> VulkanSwapchain::build(std::uint32_t width, std::uint32_t height) {
  VkSurfaceCapabilitiesKHR caps{};
  MINGOL_VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core_->physical, surface_, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

  VkExtent2D extent = caps.currentExtent;
  if (extent.width == 0xFFFFFFFFu) {  // surface lets us choose
    extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  if (extent.width == 0 || extent.height == 0) {
    return makeError(ErrorCode::InvalidArgument, "swapchain extent is zero (window minimised?)");
  }

  std::uint32_t fmtCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(core_->physical, surface_, &fmtCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fmtCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(core_->physical, surface_, &fmtCount, formats.data());
  VkSurfaceFormatKHR chosen{};
  bool found = false;
  for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
    for (const VkSurfaceFormatKHR& f : formats) {
      if (f.format == preferred && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = f;
        found = true;
        break;
      }
    }
    if (found) break;
  }
  if (!found) return makeError(ErrorCode::Unsupported, "surface offers no supported 8-bit colour format");

  std::uint32_t pmCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(core_->physical, surface_, &pmCount, nullptr);
  std::vector<VkPresentModeKHR> modes(pmCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(core_->physical, surface_, &pmCount, modes.data());
  auto hasMode = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always available
  if (!desc_.vsync) {
    if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR)) mode = VK_PRESENT_MODE_MAILBOX_KHR;
    else if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR)) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  }

  std::uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount != 0) imageCount = std::min(imageCount, caps.maxImageCount);

  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  TextureUsage texUsage = TextureUsage::ColorAttachment;
  for (auto [vkFlag, flag] : {std::pair{VK_IMAGE_USAGE_TRANSFER_DST_BIT, TextureUsage::TransferDst},
                              std::pair{VK_IMAGE_USAGE_TRANSFER_SRC_BIT, TextureUsage::TransferSrc}}) {
    if (caps.supportedUsageFlags & vkFlag) {
      usage |= vkFlag;
      texUsage |= flag;
    }
  }

  // Waiting for the queue makes replacing the old swapchain and its semaphores safe.
  {
    std::lock_guard lock(core_->queueMutex);
    vkQueueWaitIdle(core_->queue);
  }
  destroyImagesAndSync();

  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = imageCount;
  ci.imageFormat = chosen.format;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = extent;
  ci.imageArrayLayers = 1;
  ci.imageUsage = usage;
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                          : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = swapchain_;
  VkSwapchainKHR created = VK_NULL_HANDLE;
  const VkResult r = vkCreateSwapchainKHR(core_->device, &ci, nullptr, &created);
  if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(core_->device, swapchain_, nullptr);
  swapchain_ = created;
  if (r != VK_SUCCESS) return vkError(r, "vkCreateSwapchainKHR");

  format_ = fromVk(chosen.format);
  width_ = extent.width;
  height_ = extent.height;

  std::uint32_t actualCount = 0;
  vkGetSwapchainImagesKHR(core_->device, swapchain_, &actualCount, nullptr);
  std::vector<VkImage> images(actualCount);
  vkGetSwapchainImagesKHR(core_->device, swapchain_, &actualCount, images.data());

  for (std::uint32_t i = 0; i < actualCount; ++i) {
    TextureDesc td;
    td.width = width_;
    td.height = height_;
    td.format = format_;
    td.usage = texUsage;
    td.debugName = "swapchain image " + std::to_string(i);
    auto tex = std::make_shared<VulkanTexture>(td, core_);
    tex->image = images[i];
    tex->ownsImage = false;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = images[i];
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = chosen.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    MINGOL_VK_TRY(vkCreateImageView(core_->device, &vci, nullptr, &tex->view), "vkCreateImageView(swapchain)");
    images_.push_back(std::move(tex));

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore done = VK_NULL_HANDLE;
    MINGOL_VK_TRY(vkCreateSemaphore(core_->device, &sci, nullptr, &done), "vkCreateSemaphore");
    renderDone_.push_back(done);
    VkSemaphore acq = VK_NULL_HANDLE;
    MINGOL_VK_TRY(vkCreateSemaphore(core_->device, &sci, nullptr, &acq), "vkCreateSemaphore");
    acquireSems_.push_back(acq);
    acquireSlotSerial_.push_back(0);
  }
  acquireSlot_ = 0;
  return {};
}

Result<AcquiredImage> VulkanSwapchain::acquire() {
  if (acquired_) return makeError(ErrorCode::InvalidArgument, "previous image was not presented");

  const std::size_t slot = (acquireSlot_ + 1) % acquireSems_.size();
  // The semaphore may still be waited on by an earlier submission; make sure that finished.
  MINGOL_TRY(core_->waitSerial(acquireSlotSerial_[slot]));

  std::uint32_t index = 0;
  const VkResult r = vkAcquireNextImageKHR(core_->device, swapchain_, UINT64_MAX, acquireSems_[slot], VK_NULL_HANDLE, &index);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) return makeError(ErrorCode::OutOfDate, "swapchain is out of date");
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return vkError(r, "vkAcquireNextImageKHR");

  acquireSlot_ = slot;
  index_ = index;
  acquired_ = true;
  readyToPresent_ = false;
  images_[index_]->setState(TextureState::Undefined);  // contents are discarded on acquire
  return AcquiredImage{images_[index_].get(), index_};
}

Result<void> VulkanSwapchain::present() {
  if (!acquired_ || !readyToPresent_) {
    return makeError(ErrorCode::InvalidArgument, "present() needs an acquired image that was submitted");
  }
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  const VkSemaphore wait = renderDone_[index_];
  pi.pWaitSemaphores = &wait;
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &index_;

  VkResult r;
  {
    std::lock_guard lock(core_->queueMutex);
    r = vkQueuePresentKHR(core_->queue, &pi);
  }
  acquired_ = false;
  readyToPresent_ = false;
  if (r == VK_ERROR_OUT_OF_DATE_KHR) return makeError(ErrorCode::OutOfDate, "swapchain is out of date");
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return vkError(r, "vkQueuePresentKHR");
  return {};
}

Result<void> VulkanSwapchain::resize(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0) return makeError(ErrorCode::InvalidArgument, "cannot resize swapchain to zero size");
  return build(width, height);
}

}  // namespace mingol::rhi::vk
