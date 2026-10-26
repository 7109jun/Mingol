// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/platform/window.hpp"

#include <chrono>
#include <type_traits>

#include "window_backends.hpp"

namespace mingol::platform {

Window::~Window() = default;

bool Window::backendAvailable(WindowBackend backend) noexcept {
  switch (backend) {
    case WindowBackend::Headless: return true;
    case WindowBackend::Glfw:
#if defined(MINGOL_HAVE_GLFW)
      return true;
#else
      return false;
#endif
  }
  return false;
}

Result<std::unique_ptr<Window>> Window::create(const WindowDesc& desc) {
  if (desc.width == 0 || desc.height == 0) {
    return makeError(ErrorCode::InvalidArgument, "window size must be non-zero ({}x{})", desc.width,
                     desc.height);
  }
  switch (desc.backend) {
    case WindowBackend::Headless: return createHeadlessWindow(desc);
    case WindowBackend::Glfw:
#if defined(MINGOL_HAVE_GLFW)
      return createGlfwWindow(desc);
#else
      return makeError(ErrorCode::Unsupported, "this build of Mingol was configured without GLFW");
#endif
  }
  return makeError(ErrorCode::InvalidArgument, "unknown window backend");
}

void Window::injectEvent(Event e) {
  {
    const std::lock_guard<std::mutex> lock(injectMutex_);
    injected_.push_back(std::move(e));
  }
  injectCv_.notify_all();
  wakeOs();
}

void Window::pollEvents() {
  MINGOL_VERIFY_MSG(!dispatching_, "Window::pollEvents() called from an event handler");
  input_.beginFrame();
  pumpOs();
  std::vector<Event> injected;
  {
    const std::lock_guard<std::mutex> lock(injectMutex_);
    injected.swap(injected_);
  }
  for (auto& e : injected) queue_.push(std::move(e));
  dispatch();
}

void Window::waitEvents(double timeoutSeconds) {
  waitOs(timeoutSeconds);
  pollEvents();
}

void Window::dispatch() {
  dispatching_ = true;
  Event e;
  while (queue_.pop(e)) {
    // Cached state first so handlers see the new values.
    std::visit(
        [this](const auto& ev) {
          using T = std::decay_t<decltype(ev)>;
          if constexpr (std::is_same_v<T, CloseRequestedEvent>) {
            shouldClose_ = true;
          } else if constexpr (std::is_same_v<T, ResizedEvent>) {
            size_ = {ev.width, ev.height};
          } else if constexpr (std::is_same_v<T, FramebufferResizedEvent>) {
            framebufferSize_ = {ev.width, ev.height};
          } else if constexpr (std::is_same_v<T, FocusEvent>) {
            focused_ = ev.focused;
          } else if constexpr (std::is_same_v<T, IconifyEvent>) {
            iconified_ = ev.iconified;
          }
        },
        e);
    events_.emit(e);
    applyToInput(input_, e);
  }
  dispatching_ = false;
}

}  // namespace mingol::platform
