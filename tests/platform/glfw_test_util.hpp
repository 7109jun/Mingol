// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>

#include "mingol/platform/window.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

inline bool haveDisplay() {
  const char* d = std::getenv("DISPLAY");
  const char* w = std::getenv("WAYLAND_DISPLAY");
  return (d != nullptr && *d != '\0') || (w != nullptr && *w != '\0');
}

// Pumps the window until `done()` or the timeout. Returns whether the condition was met.
inline bool pumpUntil(platform::Window& w, const std::function<bool()>& done, int timeoutMs = 3000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    w.waitEvents(0.02);
    if (done()) return true;
  }
  return done();
}

// Creates a real window, or skips the test when there is no display. With a display present a
// failure to create the window is a genuine test failure, never a skip.
#define GLFW_WINDOW_OR_SKIP(var, ...)                                                          \
  std::unique_ptr<::mingol::platform::Window> var;                                             \
  {                                                                                            \
    ::mingol::platform::WindowDesc desc__ = __VA_ARGS__;                                       \
    desc__.backend = ::mingol::platform::WindowBackend::Glfw;                                  \
    if (!::mingol::platform::Window::backendAvailable(desc__.backend))                         \
      SKIP_TEST("built without GLFW");                                                         \
    if (!::mingol::test::haveDisplay()) SKIP_TEST("no DISPLAY (run via xvfb-run)");            \
    auto created__ = ::mingol::platform::Window::create(desc__);                               \
    if (!created__.ok()) {                                                                     \
      ::mingol::test::fail(__FILE__, __LINE__, "window creation: " + created__.error().describe()); \
      return;                                                                                  \
    }                                                                                          \
    var = std::move(created__).value();                                                        \
  }

}  // namespace mingol::test
