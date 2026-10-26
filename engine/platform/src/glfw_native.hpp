// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "mingol/platform/window.hpp"

struct GLFWwindow;

namespace mingol::platform {
// Kept in its own translation unit: glfw3native.h pulls in X11/Win32 headers whose macros
// (None, Always, ...) collide with our identifiers.
NativeHandle queryNativeHandle(GLFWwindow* window);
}  // namespace mingol::platform
