// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdlib>
#include <random>
#include <string>

#include "editor_util.hpp"
#include "mingol/editor/editor_app.hpp"
#include "mingol/gui/raster.hpp"

namespace mingol::test {

using mingol::editor::EditorApp;

// Drives an EditorApp with synthetic events, one 60 Hz frame at a time.
struct AppDriver {
  std::unique_ptr<EditorApp> app;
  Vec2 size{1280, 720};
  double t = 0;
  platform::Modifiers mods = platform::Modifiers::None;

  explicit AppDriver(const editor::EditorAppDesc& desc = {}) {
    auto a = EditorApp::create(desc);
    if (!a.ok()) { fail(__FILE__, __LINE__, "EditorApp::create: " + a.error().describe()); return; }
    app = std::move(a).value();
    frame(3);
  }
  void frame(int n = 1) {
    for (int i = 0; i < n; ++i) {
      app->frame(size, t, 1.0F / 60.0F);
      t += 1.0 / 60.0;
    }
  }
  void move(Vec2 p) { app->feed(platform::MouseMoveEvent{p.x, p.y}); }
  void down(int b = 0) { app->feed(platform::MouseButtonEvent{static_cast<platform::MouseButton>(b), platform::KeyAction::Press, mods}); }
  void up(int b = 0) { app->feed(platform::MouseButtonEvent{static_cast<platform::MouseButton>(b), platform::KeyAction::Release, mods}); }
  void key(platform::Key k, platform::Modifiers m = platform::Modifiers::None) {
    app->feed(platform::KeyEvent{k, 0, platform::KeyAction::Press, m});
    app->feed(platform::KeyEvent{k, 0, platform::KeyAction::Release, m});
  }
  void type(const std::string& s) { for (char c : s) app->feed(platform::CharEvent{static_cast<char32_t>(c)}); }
  void wheel(float dy) { app->feed(platform::ScrollEvent{0, dy}); }

  static Vec2 centre(const gui::Rect& r) { return {(r.x0 + r.x1) * 0.5F, (r.y0 + r.y1) * 0.5F}; }
  std::optional<gui::Rect> rect(const std::string& key) { return app->trackedRect(key); }

  // Moves onto the tracked element, presses and releases (one frame between each step so hover/press registers).
  bool click(const std::string& key, int button = 0) {
    const auto r = rect(key);
    if (!r) { fail(__FILE__, __LINE__, "no tracked element '" + key + "'"); return false; }
    clickAt(centre(*r), button);
    return true;
  }
  void clickAt(Vec2 p, int button = 0) {
    move(p);
    frame();
    down(button);
    frame();
    up(button);
    frame(2);
  }
  // Drag from a to b with intermediate steps.
  void drag(Vec2 a, Vec2 b, int button = 0, bool release = true) {
    move(a);
    frame();
    down(button);
    frame();
    for (int i = 1; i <= 6; ++i) {
      move(a + (b - a) * (static_cast<float>(i) / 6.0F));
      frame();
    }
    if (release) {
      up(button);
      frame(2);
    }
  }
  void screenshot(const char* name) {
    const char* dir = std::getenv("MINGOL_GUI_SCREENSHOT_DIR");
    if (dir == nullptr) return;
    gui::Image img(static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y), {20, 20, 22});
    gui::rasterize(app->gui().drawList(), img);
    gui::writePpm(img, (std::string(dir) + "/" + name + ".ppm").c_str());
  }
};

}  // namespace mingol::test
