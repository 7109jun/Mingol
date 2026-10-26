// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"
#include "mingol/core/signal.hpp"
#include "mingol/platform/event.hpp"
#include "mingol/platform/input.hpp"

namespace mingol::platform {

struct Extent2 {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  friend bool operator==(Extent2 a, Extent2 b) { return a.width == b.width && a.height == b.height; }
};

enum class WindowBackend : std::uint8_t {
  Glfw,      // real OS window (needs the engine built with GLFW and a display)
  Headless,  // no OS window; events come from injectEvent(). For tests, servers and tooling.
};

enum class CursorMode : std::uint8_t { Normal, Hidden, Disabled /* captured, unbounded motion */ };
enum class CursorShape : std::uint8_t { Arrow, IBeam, Crosshair, Hand, ResizeH, ResizeV };

struct WindowDesc {
  std::string title = "Mingol";
  std::uint32_t width = 1280;   // logical size (client area)
  std::uint32_t height = 720;
  bool resizable = true;
  bool visible = true;
  bool decorated = true;
  bool fullscreen = false;  // primary monitor, current video mode
  WindowBackend backend = WindowBackend::Glfw;
};

// Native handle for creating a presentation surface. Which fields are meaningful depends on kind.
// Lifetime: valid while the Window lives; destroy swapchains before the window.
struct NativeHandle {
  enum class Kind : std::uint8_t { None, Win32, Xlib, Wayland };
  Kind kind = Kind::None;
  void* display = nullptr;       // Display* / wl_display* / HINSTANCE
  std::uintptr_t window = 0;     // X11 Window / wl_surface* / HWND
};

// A window plus its event pump and input state.
//
// Threading: every member except injectEvent() must be called from the thread that created the
// window, which for the GLFW backend must be the process main thread (OS requirement). Several
// windows may coexist. injectEvent() is thread-safe.
class Window {
 public:
  // Fails with Unsupported when the backend was not compiled in, Internal/Io when the OS refuses
  // (for example no display server).
  static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
  static bool backendAvailable(WindowBackend backend) noexcept;

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  virtual ~Window();

  virtual const char* backendName() const noexcept = 0;

  // One frame of input: resets per-frame input state, pumps the OS (or the injected events), then
  // dispatches every pending event in order to `events` handlers and the input state.
  void pollEvents();
  // Like pollEvents() but first blocks until an event arrives or `timeoutSeconds` passes
  // (negative = no timeout). Another thread can wake it with injectEvent().
  void waitEvents(double timeoutSeconds = -1.0);

  // Queues a synthetic event, processed by the next pollEvents()/waitEvents(). Thread-safe.
  void injectEvent(Event e);

  bool shouldClose() const noexcept { return shouldClose_; }
  void requestClose() noexcept { shouldClose_ = true; }

  const InputState& input() const noexcept { return input_; }
  // All events, in order, as they are dispatched by pollEvents(). Handlers run before the input
  // state is updated for that event.
  Signal<const Event&>& events() noexcept { return events_; }

  // Logical client-area size, framebuffer size in pixels (differs on high-DPI), content scale.
  Extent2 size() const noexcept { return size_; }
  Extent2 framebufferSize() const noexcept { return framebufferSize_; }
  virtual Vec2 contentScale() const noexcept { return {1.0F, 1.0F}; }
  bool focused() const noexcept { return focused_; }
  bool iconified() const noexcept { return iconified_; }
  const std::string& title() const noexcept { return title_; }
  CursorMode cursorMode() const noexcept { return cursorMode_; }
  bool fullscreen() const noexcept { return fullscreen_; }

  virtual void setTitle(const std::string& title) = 0;
  // Requests a new logical size; the actual size change is reported through ResizedEvent.
  virtual void setSize(std::uint32_t width, std::uint32_t height) = 0;
  virtual void setCursorMode(CursorMode mode) = 0;
  virtual void setCursorShape(CursorShape shape) = 0;
  virtual void setFullscreen(bool fullscreen) = 0;
  virtual std::string clipboardText() const = 0;
  virtual void setClipboardText(const std::string& utf8) = 0;
  virtual NativeHandle nativeHandle() const noexcept = 0;

 protected:
  Window() = default;

  // Backend hooks. pumpOs() must push OS events with pushEvent(); waitOs() blocks like pumpOs().
  virtual void pumpOs() = 0;
  virtual void waitOs(double timeoutSeconds) = 0;
  virtual void wakeOs() {}  // make a blocked waitOs() return (thread-safe)

  void pushEvent(Event e) { queue_.push(std::move(e)); }

  Extent2 size_{};
  Extent2 framebufferSize_{};
  bool focused_ = true;
  bool iconified_ = false;
  bool fullscreen_ = false;
  std::string title_;
  CursorMode cursorMode_ = CursorMode::Normal;

 private:
  void dispatch();

  InputState input_;
  Signal<const Event&> events_;
  EventQueue queue_;
  bool shouldClose_ = false;
  bool dispatching_ = false;

  std::mutex injectMutex_;
  std::condition_variable injectCv_;
  std::vector<Event> injected_;

  friend class HeadlessWindow;
};

}  // namespace mingol::platform
