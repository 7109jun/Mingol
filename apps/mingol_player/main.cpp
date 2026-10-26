// SPDX-License-Identifier: GPL-2.0-only
// mingol-player: runs a game from a project (.mindev), a packed archive (.mingol), or - when no path is
// given - from the archive appended to its own executable.
//
//   mingol-player [path] [--key-file f | --key-env VAR] [--scene name] [--headless] [--frames N]
//                 [--screenshot out.ppm] [--width W] [--height H]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

#include "mingol/package/archive.hpp"
#include "mingol/platform/window.hpp"
#include "mingol/player/game_app.hpp"
#include "mingol/renderer/renderer.hpp"

using namespace mingol;

namespace {

struct Args {
  std::string path, keyFile, keyEnv, scene, screenshot;
  bool headless = false;
  long frames = -1;
  std::uint32_t width = 1280, height = 720;
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
    if (s == "--key-file") { if (!next(a.keyFile)) return false; }
    else if (s == "--key-env") { if (!next(a.keyEnv)) return false; }
    else if (s == "--scene") { if (!next(a.scene)) return false; }
    else if (s == "--screenshot") { if (!next(a.screenshot)) return false; }
    else if (s == "--headless") a.headless = true;
    else if (s == "--frames") { if (!next(v)) return false; a.frames = std::atol(v.c_str()); }
    else if (s == "--width") { if (!next(v)) return false; a.width = static_cast<std::uint32_t>(std::atoi(v.c_str())); }
    else if (s == "--height") { if (!next(v)) return false; a.height = static_cast<std::uint32_t>(std::atoi(v.c_str())); }
    else if (!s.empty() && s[0] != '-' && a.path.empty()) a.path = s;
    else return false;
  }
  return a.width >= 64 && a.height >= 64;
}

int fail(const char* what, const Error& e) {
  std::fprintf(stderr, "mingol-player: %s: %s\n", what, e.describe().c_str());
  return 1;
}

bool endsWith(const std::string& s, const std::string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) {
    std::fprintf(stderr,
                 "usage: mingol-player [project.mindev | game.mingol] [--key-file f | --key-env VAR] [--scene name]\n"
                 "                     [--headless] [--frames N] [--screenshot out.ppm] [--width W] [--height H]\n");
    return 2;
  }

  std::optional<package::ArchiveKey> key;
  std::string hex;
  if (!args.keyFile.empty()) {
    std::ifstream f(args.keyFile);
    if (!f) { std::fprintf(stderr, "mingol-player: cannot read key file %s\n", args.keyFile.c_str()); return 1; }
    f >> hex;
  } else if (!args.keyEnv.empty()) {
    const char* e = std::getenv(args.keyEnv.c_str());
    if (e == nullptr) { std::fprintf(stderr, "mingol-player: environment variable %s is not set\n", args.keyEnv.c_str()); return 1; }
    hex = e;
  }
  if (!hex.empty()) {
    auto k = package::ArchiveKey::fromHex(hex);
    if (!k.ok()) return fail("key", k.error());
    key = k.value();
  }

  Result<std::unique_ptr<player::ContentSource>> src = makeError(ErrorCode::NotFound, "nothing to run");
  if (args.path.empty()) {
    if (!package::ArchiveReader::hasEmbeddedArchive(argv[0])) {
      std::fprintf(stderr, "mingol-player: no game given and no archive embedded in this executable\n");
      return 2;
    }
    src = player::openEmbeddedSource(argv[0], key);
  } else if (endsWith(args.path, ".mindev")) {
    src = player::openDirectorySource(args.path);
  } else {
    src = player::openArchiveSource(args.path, key);
  }
  if (!src.ok()) return fail("cannot open the game", src.error());

  platform::WindowDesc wd;
  wd.title = "Mingol";
  wd.width = args.width;
  wd.height = args.height;
  wd.backend = args.headless ? platform::WindowBackend::Headless : platform::WindowBackend::Glfw;
  auto window = platform::Window::create(wd);
  if (!window.ok()) return fail("cannot create a window (use --headless without a display)", window.error());
  platform::Window& win = *window.value();

  rhi::DeviceDesc dd;
  dd.appName = "Mingol Player";
  auto device = rhi::createVulkanDevice(dd);
  if (!device.ok()) return fail("no usable Vulkan device", device.error());
  auto rnd = renderer::Renderer::create(*device.value());
  if (!rnd.ok()) return fail("renderer", rnd.error());

  player::GameAppDesc gd;
  gd.device = device.value().get();
  gd.renderer = rnd.value().get();
  gd.scene = args.scene;
  gd.runtime.openAudioDevice = !args.headless;
  auto appr = player::GameApp::create(std::move(src).value(), gd);
  if (!appr.ok()) return fail("cannot load the game", appr.error());
  player::GameApp& app = *appr.value();
  for (const std::string& w : app.warnings()) std::fprintf(stderr, "warning: %s\n", w.c_str());
  win.setTitle(app.project().name.empty() ? "Mingol" : app.project().name);

  rhi::SwapchainDesc sd;
  sd.width = win.framebufferSize().width;
  sd.height = win.framebufferSize().height;
  sd.window.width = sd.width;
  sd.window.height = sd.height;
  if (!args.headless) {
    const platform::NativeHandle h = win.nativeHandle();
    switch (h.kind) {
      case platform::NativeHandle::Kind::Win32: sd.window.kind = rhi::NativeWindow::Kind::Win32; break;
      case platform::NativeHandle::Kind::Wayland: sd.window.kind = rhi::NativeWindow::Kind::Wayland; break;
      default: sd.window.kind = rhi::NativeWindow::Kind::Xlib; break;
    }
    sd.window.display = h.display;
    sd.window.window = h.window;
  }
  sd.vsync = !args.headless;
  auto swap = device.value()->createSwapchain(sd);
  if (!swap.ok()) return fail("swapchain", swap.error());
  std::unique_ptr<rhi::Swapchain> sc = std::move(swap).value();

  bool quit = false, needResize = false;
  auto conn = win.events().connect([&](const platform::Event& e) {
    if (std::holds_alternative<platform::CloseRequestedEvent>(e)) quit = true;
    if (std::holds_alternative<platform::FramebufferResizedEvent>(e)) needResize = true;
  });

  auto last = std::chrono::steady_clock::now();
  long frame = 0;
  while (!quit) {
    win.pollEvents();
    const auto now = std::chrono::steady_clock::now();
    // Headless runs use a fixed frame time so results do not depend on machine speed.
    const float dt = args.headless ? 1.0F / 60.0F : std::chrono::duration<float>(now - last).count();
    last = now;
    const auto fb = win.framebufferSize();
    if (win.iconified() || fb.width == 0 || fb.height == 0) { win.waitEvents(0.05); continue; }
    if (needResize || fb.width != sc->width() || fb.height != sc->height()) {
      (void)device.value()->waitIdle();
      if (auto r = sc->resize(fb.width, fb.height); !r.ok()) return fail("resize", r.error());
      needResize = false;
    }
    app.tick(dt);
    auto img = sc->acquire();
    if (!img.ok()) {
      if (img.error().code == ErrorCode::OutOfDate) { needResize = true; continue; }
      return fail("acquire", img.error());
    }
    if (auto r = app.render(*img.value().texture, sc.get()); !r.ok()) return fail("render", r.error());
    if (auto r = sc->present(); !r.ok() && r.error().code != ErrorCode::OutOfDate) return fail("present", r.error());
    ++frame;
    if (args.frames >= 0 && frame >= args.frames) break;
  }

  int rc = 0;
  if (!args.screenshot.empty()) {
    rhi::TextureDesc td;
    td.width = win.framebufferSize().width;
    td.height = win.framebufferSize().height;
    td.format = rhi::Format::RGBA8Unorm;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc;
    auto tex = device.value()->createTexture(td);
    if (!tex.ok()) return fail("screenshot target", tex.error());
    if (auto r = app.render(*tex.value()); !r.ok()) return fail("screenshot render", r.error());
    auto bytes = device.value()->readbackTexture(*tex.value());
    if (!bytes.ok()) return fail("screenshot readback", bytes.error());
    if (FILE* f = std::fopen(args.screenshot.c_str(), "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", td.width, td.height);
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
