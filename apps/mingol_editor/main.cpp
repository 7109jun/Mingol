// SPDX-License-Identifier: GPL-2.0-only
// mingol-editor: the editor executable. Window (GLFW) + Vulkan swapchain + GuiRenderer around EditorApp.
//
//   mingol-editor [--project <file.mindev>] [--headless] [--frames N] [--screenshot out.ppm]
//                 [--width W] [--height H] [--player <mingol-player executable>]
//
// --headless uses no OS window (offscreen swapchain); with --frames N the editor exits after N frames
// (used by CI to verify the whole GPU path). --screenshot renders the last GUI frame into a PPM file.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mingol/core/log.hpp"
#include "mingol/editor/editor_app.hpp"
#include "mingol/gui/gui_renderer.hpp"
#include "mingol/platform/window.hpp"
#include "mingol/renderer/renderer.hpp"

using namespace mingol;

namespace {

struct Args {
  std::string project, screenshot, player;
  bool headless = false;
  long frames = -1;
  std::uint32_t width = 1440, height = 900;
};

bool parseArgs(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    auto next = [&](std::string& out) {
      if (i + 1 >= argc) return false;
      out = argv[++i];
      return true;
    };
    std::string v;
    if (s == "--project") { if (!next(a.project)) return false; }
    else if (s == "--screenshot") { if (!next(a.screenshot)) return false; }
    else if (s == "--player") { if (!next(a.player)) return false; }
    else if (s == "--headless") a.headless = true;
    else if (s == "--frames") { if (!next(v)) return false; a.frames = std::atol(v.c_str()); }
    else if (s == "--width") { if (!next(v)) return false; a.width = static_cast<std::uint32_t>(std::atoi(v.c_str())); }
    else if (s == "--height") { if (!next(v)) return false; a.height = static_cast<std::uint32_t>(std::atoi(v.c_str())); }
    else return false;
  }
  return a.width >= 320 && a.height >= 240;
}

rhi::SwapchainDesc swapchainDesc(const platform::Window& w, bool headless) {
  rhi::SwapchainDesc sd;
  sd.width = w.framebufferSize().width;
  sd.height = w.framebufferSize().height;
  sd.window.width = sd.width;
  sd.window.height = sd.height;
  if (!headless) {
    const platform::NativeHandle h = w.nativeHandle();
    switch (h.kind) {
      case platform::NativeHandle::Kind::Win32: sd.window.kind = rhi::NativeWindow::Kind::Win32; break;
      case platform::NativeHandle::Kind::Wayland: sd.window.kind = rhi::NativeWindow::Kind::Wayland; break;
      default: sd.window.kind = rhi::NativeWindow::Kind::Xlib; break;
    }
    sd.window.display = h.display;
    sd.window.window = h.window;
  }
  sd.vsync = !headless;
  return sd;
}

int fail(const char* what, const Error& e) {
  std::fprintf(stderr, "mingol-editor: %s: %s\n", what, e.describe().c_str());
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) {
    std::fprintf(stderr,
                 "usage: mingol-editor [--project file.mindev] [--headless] [--frames N] [--screenshot out.ppm] "
                 "[--width W] [--height H] [--player exe]\n");
    return 2;
  }

  platform::WindowDesc wd;
  wd.title = "Mingol Editor";
  wd.width = args.width;
  wd.height = args.height;
  wd.backend = args.headless ? platform::WindowBackend::Headless : platform::WindowBackend::Glfw;
  auto window = platform::Window::create(wd);
  if (!window.ok()) return fail("cannot create a window (use --headless without a display)", window.error());
  platform::Window& win = *window.value();

  rhi::DeviceDesc dd;
  dd.appName = "Mingol Editor";
  auto device = rhi::createVulkanDevice(dd);
  if (!device.ok()) return fail("no usable Vulkan device", device.error());
  auto rnd = renderer::Renderer::create(*device.value());
  if (!rnd.ok()) return fail("renderer", rnd.error());
  auto swap = device.value()->createSwapchain(swapchainDesc(win, args.headless));
  if (!swap.ok()) return fail("swapchain", swap.error());
  std::unique_ptr<rhi::Swapchain> sc = std::move(swap).value();

  gui::GuiRendererDesc gd;
  gd.targetFormat = sc->format();
  auto guiRenderer = gui::GuiRenderer::create(*device.value(), gd);
  if (!guiRenderer.ok()) return fail("gui renderer", guiRenderer.error());

  editor::EditorAppDesc ad;
  ad.device = device.value().get();
  ad.renderer = rnd.value().get();
  ad.playerExe = args.player;
  auto appr = editor::EditorApp::create(ad);
  if (!appr.ok()) return fail("editor", appr.error());
  editor::EditorApp& app = *appr.value();
  app.gui().clipboard.get = [&win] { return win.clipboardText(); };
  app.gui().clipboard.set = [&win](const std::string& s) { win.setClipboardText(s); };
  app.gui().setCursorShape = [&win](platform::CursorShape c) { win.setCursorShape(c); };

  bool needResize = false;
  auto conn = win.events().connect([&](const platform::Event& e) {
    // GLFW reports mouse positions in logical units; the GUI works in framebuffer pixels.
    const auto fb = win.framebufferSize();
    const auto lg = win.size();
    const float sx = lg.width > 0 ? static_cast<float>(fb.width) / static_cast<float>(lg.width) : 1.0F;
    const float sy = lg.height > 0 ? static_cast<float>(fb.height) / static_cast<float>(lg.height) : 1.0F;
    if (const auto* m = std::get_if<platform::MouseMoveEvent>(&e)) app.feed(platform::MouseMoveEvent{m->x * sx, m->y * sy});
    else app.feed(e);
    if (std::holds_alternative<platform::FramebufferResizedEvent>(e)) needResize = true;
  });

  if (!args.project.empty()) {
    if (auto r = app.document().openProject(args.project); !r.ok()) std::fprintf(stderr, "cannot open project: %s\n", r.error().describe().c_str());
  }

  std::uint64_t viewportGen = 0;
  auto last = std::chrono::steady_clock::now();
  long frame = 0;
  double time = 0;
  while (!app.quitRequested()) {
    win.pollEvents();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    time += dt;
    const auto fb = win.framebufferSize();
    if (win.iconified() || fb.width == 0 || fb.height == 0) {
      win.waitEvents(0.05);
      continue;
    }
    if (needResize || fb.width != sc->width() || fb.height != sc->height()) {
      (void)device.value()->waitIdle();
      if (auto r = sc->resize(fb.width, fb.height); !r.ok()) return fail("resize", r.error());
      needResize = false;
    }
    app.frame({static_cast<float>(fb.width), static_cast<float>(fb.height)}, time, dt);
    if (auto tex = app.renderViewport(); tex.ok() && tex.value() != nullptr && app.viewportTextureGeneration() != viewportGen) {
      (void)guiRenderer.value()->setTexture(gui::kTextureUserBase, tex.value(), true);
      viewportGen = app.viewportTextureGeneration();
    }
    auto img = sc->acquire();
    if (!img.ok()) {
      if (img.error().code == ErrorCode::OutOfDate) { needResize = true; continue; }
      return fail("acquire", img.error());
    }
    const float clear[4] = {0.1F, 0.1F, 0.11F, 1.0F};
    if (auto r = guiRenderer.value()->render(app.gui().drawList(), *img.value().texture, clear, sc.get()); !r.ok()) return fail("gui render", r.error());
    if (auto r = sc->present(); !r.ok() && r.error().code != ErrorCode::OutOfDate) return fail("present", r.error());
    ++frame;
    if (args.frames >= 0 && frame >= args.frames) {
      if (app.document().dirty()) app.document().commit("exit");
      break;
    }
  }

  int rc = 0;
  if (!args.screenshot.empty()) {
    // Render the last GUI frame once more into an offscreen RGBA8 target and read it back.
    const auto fb = win.framebufferSize();
    rhi::TextureDesc td;
    td.width = fb.width;
    td.height = fb.height;
    td.format = rhi::Format::RGBA8Unorm;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled;
    auto tex = device.value()->createTexture(td);
    gui::GuiRendererDesc sd;
    sd.targetFormat = rhi::Format::RGBA8Unorm;
    auto shot = gui::GuiRenderer::create(*device.value(), sd);
    if (!tex.ok() || !shot.ok()) return fail("screenshot target", tex.ok() ? shot.error() : tex.error());
    if (app.viewportTexture()) (void)shot.value()->setTexture(gui::kTextureUserBase, app.viewportTexture(), true);
    const float clear[4] = {0.1F, 0.1F, 0.11F, 1.0F};
    auto r = shot.value()->render(app.gui().drawList(), *tex.value(), clear, nullptr);
    if (!r.ok()) return fail("screenshot render", r.error());
    auto bytes = device.value()->readbackTexture(*tex.value());
    if (!bytes.ok()) return fail("screenshot readback", bytes.error());
    if (FILE* f = std::fopen(args.screenshot.c_str(), "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
      for (std::size_t i = 0; i + 3 < bytes.value().size(); i += 4) std::fwrite(&bytes.value()[i], 1, 3, f);
      std::fclose(f);
    } else {
      std::fprintf(stderr, "cannot write %s\n", args.screenshot.c_str());
      rc = 1;
    }
  }
  (void)device.value()->waitIdle();
  conn.disconnect();
  return rc;
}
