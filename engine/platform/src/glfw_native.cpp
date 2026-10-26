// SPDX-License-Identifier: GPL-2.0-only
#include "glfw_native.hpp"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace mingol::platform {

NativeHandle queryNativeHandle(GLFWwindow* window) {
  NativeHandle h;
#if defined(_WIN32)
  h.kind = NativeHandle::Kind::Win32;
  h.display = GetModuleHandleW(nullptr);
  h.window = reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(window));
#else
  // Only X11 is wired up (GLFW 3.3 is X11-only on Linux; a GLFW 3.4 build running natively on
  // Wayland returns a null display here, which the caller reports as unsupported). Wayland
  // accessors are not implemented because they could not be built or tested in this environment.
  void* display = glfwGetX11Display();
  if (display != nullptr) {
    h.kind = NativeHandle::Kind::Xlib;
    h.display = display;
    h.window = static_cast<std::uintptr_t>(glfwGetX11Window(window));
  }
#endif
  return h;
}

}  // namespace mingol::platform
