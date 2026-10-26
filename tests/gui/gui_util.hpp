// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

#include "mingol/gui/context.hpp"
#include "mingol/gui/raster.hpp"

namespace guitest {

using namespace mingol;
using namespace mingol::gui;

// Drives a Context frame by frame with synthetic platform events.
struct Harness {
  Context ctx;
  Vec2 size{480, 360};
  double t = 0;
  int frames = 0;
  Image last;
  bool render = false;

  explicit Harness(int fontScale = 1) { ctx.style.fontScale = fontScale; }

  void move(float x, float y) { ctx.feed(platform::MouseMoveEvent{x, y}); }
  void down(int b = 0) {
    ctx.feed(platform::MouseButtonEvent{static_cast<platform::MouseButton>(b), platform::KeyAction::Press, mods});
  }
  void up(int b = 0) {
    ctx.feed(platform::MouseButtonEvent{static_cast<platform::MouseButton>(b), platform::KeyAction::Release, mods});
  }
  void wheel(float dy) { ctx.feed(platform::ScrollEvent{0, dy}); }
  void key(platform::Key k, platform::Modifiers m = platform::Modifiers::None, bool repeat = false) {
    ctx.feed(platform::KeyEvent{k, 0, repeat ? platform::KeyAction::Repeat : platform::KeyAction::Press, m});
    ctx.feed(platform::KeyEvent{k, 0, platform::KeyAction::Release, m});
  }
  void type(const std::string& utf8) {
    for (char32_t cp : font_decode(utf8)) ctx.feed(platform::CharEvent{cp});
  }
  static std::vector<char32_t> font_decode(const std::string& s);

  template <class F>
  void frame(F&& build) {
    ctx.beginFrame(size, t);
    build();
    ctx.endFrame();
    t += 1.0 / 60.0;
    ++frames;
    if (render) {
      last = Image(static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y), {20, 20, 22});
      rasterize(ctx.drawList(), last);
    }
  }
  platform::Modifiers mods = platform::Modifiers::None;
};

}  // namespace guitest
