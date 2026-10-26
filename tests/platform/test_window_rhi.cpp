// SPDX-License-Identifier: GPL-2.0-only
// A real window (GLFW) presented through the Vulkan RHI's platform surface. The picture the X
// server shows is read back and compared, so this verifies actual presentation, not just that the
// calls returned success. Needs a display (xvfb-run) and a Vulkan driver with X11 WSI (lavapipe).
#include <thread>

#include "glfw_test_util.hpp"
#include "mingol/rhi/device.hpp"
#include "x11_probe.hpp"

using namespace mingol;
using namespace mingol::platform;
using mingol::test::pumpUntil;

namespace {

rhi::SwapchainDesc swapchainDescFor(const Window& w) {
  const NativeHandle h = w.nativeHandle();
  rhi::SwapchainDesc sd;
  sd.window.kind = h.kind == NativeHandle::Kind::Win32 ? rhi::NativeWindow::Kind::Win32
                                                       : rhi::NativeWindow::Kind::Xlib;
  sd.window.display = h.display;
  sd.window.window = h.window;
  sd.window.width = sd.width = w.framebufferSize().width;
  sd.window.height = sd.height = w.framebufferSize().height;
  sd.vsync = false;  // FIFO would make a software driver needlessly slow in tests
  return sd;
}

// Clears the next swapchain image to a colour and presents it. Returns the Error code on failure.
Result<void> presentColor(rhi::Device& dev, rhi::Swapchain& sc, rhi::CommandList& list, float r, float g, float b) {
  MINGOL_TRY_ASSIGN(rhi::AcquiredImage image, sc.acquire());
  MINGOL_TRY(list.begin());
  rhi::RenderPassDesc pass;
  rhi::ColorAttachment ca;
  ca.texture = image.texture;
  ca.clear[0] = r;
  ca.clear[1] = g;
  ca.clear[2] = b;
  ca.clear[3] = 1.0F;
  pass.colors.push_back(ca);
  list.beginRendering(pass);
  list.endRendering();
  list.transition(*image.texture, rhi::TextureState::Present);
  MINGOL_TRY(list.end());
  MINGOL_TRY_ASSIGN(std::uint64_t serial, dev.submit(list, &sc));
  MINGOL_TRY(sc.present());
  return dev.waitForSerial(serial);
}

bool near(int a, int b, int tol = 3) { return std::abs(a - b) <= tol; }

}  // namespace

TEST(WindowRhi, PresentToRealWindowAndReadBackPixels) {
  if (!test::x11ProbeAvailable()) SKIP_TEST("built without Xlib probe");
  WindowDesc d;
  d.width = 256;
  d.height = 192;
  d.resizable = true;
  GLFW_WINDOW_OR_SKIP(w, d);
  if (w->nativeHandle().kind != NativeHandle::Kind::Xlib) SKIP_TEST("pixel probe is X11-only");

  rhi::DeviceDesc dd;
  dd.enableValidation = true;
  auto devr = rhi::createVulkanDevice(dd);
  if (!devr.ok()) SKIP_TEST("no Vulkan device: " + devr.error().describe());
  rhi::Device& dev = *devr.value();

  auto scr = dev.createSwapchain(swapchainDescFor(*w));
  CHECK_MSG(scr.ok(), scr.ok() ? "" : scr.error().describe());
  if (!scr.ok()) return;
  rhi::Swapchain& sc = *scr.value();
  CHECK_EQ(sc.width(), w->framebufferSize().width);
  CHECK_EQ(sc.height(), w->framebufferSize().height);

  auto listr = dev.createCommandList();
  REQUIRE(listr.ok());
  rhi::CommandList& list = *listr.value();
  const NativeHandle h = w->nativeHandle();

  struct Probe {
    float r, g, b;
    int er, eg, eb;
  };
  for (const Probe p : {Probe{1, 0, 0, 255, 0, 0}, Probe{0, 1, 0, 0, 255, 0}, Probe{0, 0, 1, 0, 0, 255}}) {
    const auto r = presentColor(dev, sc, list, p.r, p.g, p.b);
    CHECK_MSG(r.ok(), r.ok() ? "" : r.error().describe());
    // The server may show the frame a moment after present() returns; poll until it matches.
    bool matched = false;
    std::uint8_t px[3] = {};
    for (int attempt = 0; attempt < 100 && !matched; ++attempt) {
      w->pollEvents();
      if (test::x11ReadPixel(h.display, h.window, 100, 80, px)) {
        matched = near(px[0], p.er) && near(px[1], p.eg) && near(px[2], p.eb);
      }
      if (!matched) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_MSG(matched, "window shows rgb(" + std::to_string(px[0]) + "," + std::to_string(px[1]) + "," +
                           std::to_string(px[2]) + ") expected (" + std::to_string(p.er) + "," +
                           std::to_string(p.eg) + "," + std::to_string(p.eb) + ")");
  }

  REQUIRE(dev.waitIdle().ok());
  CHECK_EQ(dev.validationErrorCount(), 0u);
}

TEST(WindowRhi, ResizeRecreatesSwapchainAndKeepsPresenting) {
  if (!test::x11ProbeAvailable()) SKIP_TEST("built without Xlib probe");
  WindowDesc d;
  d.width = 200;
  d.height = 150;
  GLFW_WINDOW_OR_SKIP(w, d);
  if (w->nativeHandle().kind != NativeHandle::Kind::Xlib) SKIP_TEST("pixel probe is X11-only");

  rhi::DeviceDesc dd;
  dd.enableValidation = true;
  auto devr = rhi::createVulkanDevice(dd);
  if (!devr.ok()) SKIP_TEST("no Vulkan device: " + devr.error().describe());
  rhi::Device& dev = *devr.value();
  auto scr = dev.createSwapchain(swapchainDescFor(*w));
  REQUIRE(scr.ok());
  rhi::Swapchain& sc = *scr.value();
  auto listr = dev.createCommandList();
  REQUIRE(listr.ok());

  // A render loop the way an application writes it: follow the framebuffer size, and treat
  // OutOfDate as "resize and retry" rather than as a failure.
  auto frame = [&](float r, float g, float b) {
    for (int attempt = 0; attempt < 3; ++attempt) {
      const Extent2 fb = w->framebufferSize();
      if (fb.width != sc.width() || fb.height != sc.height()) {
        if (fb.width == 0 || fb.height == 0) return true;  // minimised: nothing to draw
        if (!sc.resize(fb.width, fb.height).ok()) return false;
      }
      const auto res = presentColor(dev, sc, *listr.value(), r, g, b);
      if (res.ok()) return true;
      if (res.error().code != ErrorCode::OutOfDate) {
        test::fail(__FILE__, __LINE__, res.error().describe());
        return false;
      }
      w->pollEvents();
    }
    return false;
  };

  for (int i = 0; i < 5; ++i) CHECK(frame(0.2F, 0.2F, 0.2F));

  w->setSize(333, 222);
  REQUIRE(pumpUntil(*w, [&] { return w->framebufferSize().width == 333; }));
  for (int i = 0; i < 5; ++i) CHECK(frame(0.0F, 1.0F, 0.0F));
  CHECK_EQ(sc.width(), 333u);
  CHECK_EQ(sc.height(), 222u);

  w->setSize(120, 90);
  REQUIRE(pumpUntil(*w, [&] { return w->framebufferSize().width == 120; }));
  for (int i = 0; i < 5; ++i) CHECK(frame(1.0F, 0.0F, 0.0F));
  CHECK_EQ(sc.width(), 120u);

  const NativeHandle h = w->nativeHandle();
  bool matched = false;
  std::uint8_t px[3] = {};
  for (int attempt = 0; attempt < 100 && !matched; ++attempt) {
    w->pollEvents();
    if (test::x11ReadPixel(h.display, h.window, 60, 45, px)) matched = near(px[0], 255) && near(px[1], 0) && near(px[2], 0);
    if (!matched) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK_MSG(matched, "after resize the window shows rgb(" + std::to_string(px[0]) + "," + std::to_string(px[1]) +
                         "," + std::to_string(px[2]) + ")");

  REQUIRE(dev.waitIdle().ok());
  CHECK_EQ(dev.validationErrorCount(), 0u);
}
