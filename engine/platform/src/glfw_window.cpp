// SPDX-License-Identifier: GPL-2.0-only
#include <GLFW/glfw3.h>

#include <array>
#include <mutex>
#include <string>

#include "glfw_native.hpp"
#include "mingol/core/log.hpp"
#include "window_backends.hpp"

namespace mingol::platform {

namespace {

// ---- GLFW library lifetime (process-wide, reference counted) --------------------------------

std::mutex gGlfwMutex;
int gGlfwRefs = 0;
std::string gGlfwLastError;

void glfwErrorCallback(int code, const char* description) {
  gGlfwLastError = std::format("GLFW error {}: {}", code, description != nullptr ? description : "");
  MINGOL_LOG_WARN("platform", "{}", gGlfwLastError);
}

Result<bool> acquireGlfw() {
  const std::lock_guard<std::mutex> lock(gGlfwMutex);
  if (gGlfwRefs == 0) {
    gGlfwLastError.clear();
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE) {
      return makeError(ErrorCode::Unsupported, "glfwInit failed ({})",
                       gGlfwLastError.empty() ? "no display server?" : gGlfwLastError);
    }
  }
  ++gGlfwRefs;
  return true;
}

void releaseGlfw() {
  const std::lock_guard<std::mutex> lock(gGlfwMutex);
  if (--gGlfwRefs == 0) glfwTerminate();
}

// ---- translation ------------------------------------------------------------------------------

Key translateKey(int k) {
  static const std::array<Key, GLFW_KEY_LAST + 1> table = [] {
    std::array<Key, GLFW_KEY_LAST + 1> t{};
#define MINGOL_GLFW_KEY(id, g, name) t[GLFW_KEY_##g] = Key::id;
    MINGOL_KEY_LIST(MINGOL_GLFW_KEY)
#undef MINGOL_GLFW_KEY
    return t;
  }();
  return (k >= 0 && k <= GLFW_KEY_LAST) ? table[static_cast<std::size_t>(k)] : Key::Unknown;
}

Modifiers translateMods(int m) {
  Modifiers r = Modifiers::None;
  if (m & GLFW_MOD_SHIFT) r |= Modifiers::Shift;
  if (m & GLFW_MOD_CONTROL) r |= Modifiers::Control;
  if (m & GLFW_MOD_ALT) r |= Modifiers::Alt;
  if (m & GLFW_MOD_SUPER) r |= Modifiers::Super;
  if (m & GLFW_MOD_CAPS_LOCK) r |= Modifiers::CapsLock;
  if (m & GLFW_MOD_NUM_LOCK) r |= Modifiers::NumLock;
  return r;
}

KeyAction translateAction(int a) {
  switch (a) {
    case GLFW_PRESS: return KeyAction::Press;
    case GLFW_REPEAT: return KeyAction::Repeat;
    default: return KeyAction::Release;
  }
}

std::uint32_t clampSize(int v) { return v > 0 ? static_cast<std::uint32_t>(v) : 0U; }

}  // namespace

class GlfwWindow final : public Window {
 public:
  ~GlfwWindow() override {
    for (GLFWcursor* c : cursors_) {
      if (c != nullptr) glfwDestroyCursor(c);
    }
    if (win_ != nullptr) glfwDestroyWindow(win_);
    releaseGlfw();
  }

  static Result<std::unique_ptr<Window>> create(const WindowDesc& d) {
    MINGOL_TRY(acquireGlfw());
    std::unique_ptr<GlfwWindow> w(new GlfwWindow());  // owns the GLFW reference from here on
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan: no GL context
    glfwWindowHint(GLFW_RESIZABLE, d.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, d.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, d.decorated ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor* monitor = nullptr;
    int width = static_cast<int>(d.width);
    int height = static_cast<int>(d.height);
    if (d.fullscreen) {
      monitor = glfwGetPrimaryMonitor();
      if (monitor == nullptr) return makeError(ErrorCode::Unsupported, "no monitor available for fullscreen");
      const GLFWvidmode* mode = glfwGetVideoMode(monitor);
      if (mode != nullptr) {
        width = mode->width;
        height = mode->height;
      }
    }
    gGlfwLastError.clear();
    w->win_ = glfwCreateWindow(width, height, d.title.c_str(), monitor, nullptr);
    if (w->win_ == nullptr) {
      return makeError(ErrorCode::Io, "glfwCreateWindow failed ({})", gGlfwLastError);
    }
    if (queryNativeHandle(w->win_).display == nullptr) {
      return makeError(ErrorCode::Unsupported,
                       "GLFW is running on a windowing platform that this build cannot expose to Vulkan "
                       "(only X11 and Win32 are wired up)");
    }
    glfwSetWindowUserPointer(w->win_, w.get());
    w->installCallbacks();

    w->title_ = d.title;
    w->fullscreen_ = d.fullscreen;
    w->queryState();
    return std::unique_ptr<Window>(std::move(w));
  }

  const char* backendName() const noexcept override { return "glfw"; }

  Vec2 contentScale() const noexcept override {
    float x = 1.0F;
    float y = 1.0F;
    glfwGetWindowContentScale(win_, &x, &y);
    return {x, y};
  }

  void setTitle(const std::string& title) override {
    title_ = title;
    glfwSetWindowTitle(win_, title.c_str());
  }

  void setSize(std::uint32_t width, std::uint32_t height) override {
    if (width == 0 || height == 0 || fullscreen_) return;
    glfwSetWindowSize(win_, static_cast<int>(width), static_cast<int>(height));
  }

  void setCursorMode(CursorMode mode) override {
    cursorMode_ = mode;
    const int value = mode == CursorMode::Normal   ? GLFW_CURSOR_NORMAL
                      : mode == CursorMode::Hidden ? GLFW_CURSOR_HIDDEN
                                                   : GLFW_CURSOR_DISABLED;
    glfwSetInputMode(win_, GLFW_CURSOR, value);
    if (mode == CursorMode::Disabled && glfwRawMouseMotionSupported() == GLFW_TRUE) {
      glfwSetInputMode(win_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
  }

  void setCursorShape(CursorShape shape) override {
    const auto i = static_cast<std::size_t>(shape);
    if (cursors_[i] == nullptr) {
      static constexpr int kShapes[] = {GLFW_ARROW_CURSOR, GLFW_IBEAM_CURSOR, GLFW_CROSSHAIR_CURSOR,
                                        GLFW_HAND_CURSOR, GLFW_HRESIZE_CURSOR, GLFW_VRESIZE_CURSOR};
      cursors_[i] = glfwCreateStandardCursor(kShapes[i]);
    }
    glfwSetCursor(win_, cursors_[i]);  // null (creation failed) restores the default cursor
  }

  void setFullscreen(bool fullscreen) override {
    if (fullscreen == fullscreen_) return;
    if (fullscreen) {
      GLFWmonitor* monitor = glfwGetPrimaryMonitor();
      const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
      if (mode == nullptr) return;
      glfwGetWindowPos(win_, &savedX_, &savedY_);
      glfwGetWindowSize(win_, &savedW_, &savedH_);
      glfwSetWindowMonitor(win_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
      glfwSetWindowMonitor(win_, nullptr, savedX_, savedY_, savedW_ > 0 ? savedW_ : 1280,
                           savedH_ > 0 ? savedH_ : 720, 0);
    }
    fullscreen_ = fullscreen;
  }

  std::string clipboardText() const override {
    const char* s = glfwGetClipboardString(win_);
    return s != nullptr ? std::string(s) : std::string();
  }

  void setClipboardText(const std::string& utf8) override { glfwSetClipboardString(win_, utf8.c_str()); }

  NativeHandle nativeHandle() const noexcept override { return queryNativeHandle(win_); }

 protected:
  void pumpOs() override { glfwPollEvents(); }

  void waitOs(double timeoutSeconds) override {
    if (timeoutSeconds < 0.0) {
      glfwWaitEvents();
    } else {
      glfwWaitEventsTimeout(timeoutSeconds);
    }
  }

  void wakeOs() override { glfwPostEmptyEvent(); }  // documented thread-safe

 private:
  GlfwWindow() = default;

  void queryState() {
    int w = 0;
    int h = 0;
    glfwGetWindowSize(win_, &w, &h);
    size_ = {clampSize(w), clampSize(h)};
    glfwGetFramebufferSize(win_, &w, &h);
    framebufferSize_ = {clampSize(w), clampSize(h)};
    focused_ = glfwGetWindowAttrib(win_, GLFW_FOCUSED) == GLFW_TRUE;
    iconified_ = glfwGetWindowAttrib(win_, GLFW_ICONIFIED) == GLFW_TRUE;
  }

  static GlfwWindow* self(GLFWwindow* w) { return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w)); }

  void installCallbacks() {
    glfwSetWindowCloseCallback(win_, [](GLFWwindow* w) {
      // Let the application decide: the window stays open until it destroys it.
      glfwSetWindowShouldClose(w, GLFW_FALSE);
      self(w)->pushEvent(CloseRequestedEvent{});
    });
    glfwSetWindowSizeCallback(win_, [](GLFWwindow* w, int width, int height) {
      self(w)->pushEvent(ResizedEvent{clampSize(width), clampSize(height)});
    });
    glfwSetFramebufferSizeCallback(win_, [](GLFWwindow* w, int width, int height) {
      self(w)->pushEvent(FramebufferResizedEvent{clampSize(width), clampSize(height)});
    });
    glfwSetWindowFocusCallback(win_, [](GLFWwindow* w, int focused) {
      self(w)->pushEvent(FocusEvent{focused == GLFW_TRUE});
    });
    glfwSetWindowIconifyCallback(win_, [](GLFWwindow* w, int iconified) {
      self(w)->pushEvent(IconifyEvent{iconified == GLFW_TRUE});
    });
    glfwSetKeyCallback(win_, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
      self(w)->pushEvent(KeyEvent{translateKey(key), scancode, translateAction(action), translateMods(mods)});
    });
    glfwSetCharCallback(win_, [](GLFWwindow* w, unsigned int cp) {
      self(w)->pushEvent(CharEvent{static_cast<char32_t>(cp)});
    });
    glfwSetMouseButtonCallback(win_, [](GLFWwindow* w, int button, int action, int mods) {
      if (button < 0 || button >= static_cast<int>(kMouseButtonCount)) return;  // buttons 6-8 unsupported
      self(w)->pushEvent(MouseButtonEvent{static_cast<MouseButton>(button), translateAction(action),
                                          translateMods(mods)});
    });
    glfwSetCursorPosCallback(win_, [](GLFWwindow* w, double x, double y) {
      self(w)->pushEvent(MouseMoveEvent{static_cast<float>(x), static_cast<float>(y)});
    });
    glfwSetScrollCallback(win_, [](GLFWwindow* w, double dx, double dy) {
      self(w)->pushEvent(ScrollEvent{static_cast<float>(dx), static_cast<float>(dy)});
    });
    glfwSetCursorEnterCallback(win_, [](GLFWwindow* w, int entered) {
      self(w)->pushEvent(CursorEnterEvent{entered == GLFW_TRUE});
    });
    glfwSetDropCallback(win_, [](GLFWwindow* w, int count, const char** paths) {
      FilesDroppedEvent e;
      for (int i = 0; i < count; ++i) e.paths.emplace_back(paths[i]);
      self(w)->pushEvent(std::move(e));
    });
  }

  GLFWwindow* win_ = nullptr;
  std::array<GLFWcursor*, 6> cursors_{};
  int savedX_ = 0, savedY_ = 0, savedW_ = 0, savedH_ = 0;
};

Result<std::unique_ptr<Window>> createGlfwWindow(const WindowDesc& desc) { return GlfwWindow::create(desc); }

}  // namespace mingol::platform
