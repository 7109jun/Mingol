// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>

#include "window_backends.hpp"

namespace mingol::platform {

// A window with no OS presence. State changes requested through the API are reported through the
// same events a real backend would deliver, so code written against Window behaves identically.
class HeadlessWindow final : public Window {
 public:
  explicit HeadlessWindow(const WindowDesc& d) {
    size_ = {d.width, d.height};
    framebufferSize_ = size_;
    title_ = d.title;
    fullscreen_ = d.fullscreen;
  }

  const char* backendName() const noexcept override { return "headless"; }

  void setTitle(const std::string& title) override { title_ = title; }

  void setSize(std::uint32_t width, std::uint32_t height) override {
    if (width == 0 || height == 0) return;
    pushEvent(ResizedEvent{width, height});
    pushEvent(FramebufferResizedEvent{width, height});
  }

  void setCursorMode(CursorMode mode) override { cursorMode_ = mode; }
  void setCursorShape(CursorShape) override {}
  // Headless has no monitor: only the flag changes.
  void setFullscreen(bool fullscreen) override { fullscreen_ = fullscreen; }

  std::string clipboardText() const override { return clipboard_; }
  void setClipboardText(const std::string& utf8) override { clipboard_ = utf8; }

  NativeHandle nativeHandle() const noexcept override { return {}; }

 protected:
  void pumpOs() override {}

  void waitOs(double timeoutSeconds) override {
    std::unique_lock<std::mutex> lock(injectMutex_);
    const auto ready = [this] { return !injected_.empty(); };
    if (timeoutSeconds < 0.0) {
      injectCv_.wait(lock, ready);
    } else {
      injectCv_.wait_for(lock, std::chrono::duration<double>(timeoutSeconds), ready);
    }
  }

 private:
  std::string clipboard_;
};

Result<std::unique_ptr<Window>> createHeadlessWindow(const WindowDesc& desc) {
  return std::unique_ptr<Window>(std::make_unique<HeadlessWindow>(desc));
}

}  // namespace mingol::platform
