// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>

#include "mingol/platform/window.hpp"

namespace mingol::platform {

Result<std::unique_ptr<Window>> createHeadlessWindow(const WindowDesc& desc);
#if defined(MINGOL_HAVE_GLFW)
Result<std::unique_ptr<Window>> createGlfwWindow(const WindowDesc& desc);
#endif

}  // namespace mingol::platform
