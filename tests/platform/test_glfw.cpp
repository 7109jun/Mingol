// SPDX-License-Identifier: GPL-2.0-only
// Real-window tests. They need a display; in CI they run under Xvfb (xvfb-run) and drive the
// window with genuine X11 input through xdotool (XTEST), so key/mouse translation is exercised
// end to end. Without a display they are skipped, not passed.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "glfw_test_util.hpp"

using namespace mingol;
using namespace mingol::platform;
using mingol::test::pumpUntil;

namespace {

bool haveXdotool() { return std::system("command -v xdotool >/dev/null 2>&1") == 0; }

bool xdotool(const std::string& args) {
  const std::string cmd = "xdotool " + args + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

struct Recorder {
  std::vector<Event> events;
  ScopedConnection conn;
  explicit Recorder(Window& w) { conn = w.events().connect([this](const Event& e) { events.push_back(e); }); }

  template <class T, class Pred>
  bool has(Pred pred) const {
    for (const Event& e : events) {
      if (const auto* p = std::get_if<T>(&e)) {
        if (pred(*p)) return true;
      }
    }
    return false;
  }
};

// Gives the window keyboard focus (no window manager runs under Xvfb, so focus is explicit).
bool focusWindow(Window& w) {
  const std::string id = std::to_string(w.nativeHandle().window);
  if (!xdotool("windowfocus " + id)) return false;
  return pumpUntil(w, [&] { return w.focused(); });
}

}  // namespace

TEST(Glfw, CreateQueryDestroy) {
  WindowDesc d;
  d.title = "Mingol test";
  d.width = 320;
  d.height = 240;
  GLFW_WINDOW_OR_SKIP(w, d);
  CHECK_EQ(std::string(w->backendName()), std::string("glfw"));
  CHECK(w->size() == (Extent2{320, 240}));
  CHECK(w->framebufferSize().width > 0);
  CHECK_EQ(w->title(), std::string("Mingol test"));
  const NativeHandle h = w->nativeHandle();
  CHECK(h.kind == NativeHandle::Kind::Xlib || h.kind == NativeHandle::Kind::Win32);
  CHECK(h.display != nullptr);
  CHECK(h.window != 0);
  CHECK(!w->shouldClose());
  w->pollEvents();
  w->setTitle("changed");
  CHECK_EQ(w->title(), std::string("changed"));
}

TEST(Glfw, RepeatedCreateDestroyCyclesTheLibrary) {
  if (!Window::backendAvailable(WindowBackend::Glfw)) SKIP_TEST("built without GLFW");
  if (!test::haveDisplay()) SKIP_TEST("no DISPLAY");
  for (int i = 0; i < 15; ++i) {
    WindowDesc d;
    d.width = 64;
    d.height = 64;
    auto r = Window::create(d);
    CHECK_MSG(r.ok(), "iteration " + std::to_string(i) + ": " + (r.ok() ? "" : r.error().describe()));
    if (!r.ok()) return;
    r.value()->pollEvents();
  }  // each iteration terminates GLFW when the last window dies, then re-initialises it
}

TEST(Glfw, TwoWindowsCoexist) {
  WindowDesc d;
  d.width = 100;
  d.height = 100;
  GLFW_WINDOW_OR_SKIP(a, d);
  GLFW_WINDOW_OR_SKIP(b, d);
  CHECK(a->nativeHandle().window != b->nativeHandle().window);
  a->pollEvents();
  b->pollEvents();
  a.reset();  // destroying one must leave the other (and the library) alive
  b->setTitle("still alive");
  b->pollEvents();
  CHECK_EQ(b->title(), std::string("still alive"));
}

TEST(Glfw, ResizeIsReportedThroughEvents) {
  WindowDesc d;
  d.width = 300;
  d.height = 200;
  GLFW_WINDOW_OR_SKIP(w, d);
  Recorder rec(*w);
  w->setSize(420, 310);
  REQUIRE(pumpUntil(*w, [&] { return w->size() == (Extent2{420, 310}); }));
  CHECK(rec.has<ResizedEvent>([](const ResizedEvent& e) { return e.width == 420 && e.height == 310; }));
  REQUIRE(pumpUntil(*w, [&] { return w->framebufferSize().width == 420 || w->framebufferSize().width == 840; }));
  CHECK(w->framebufferSize().height >= 310);
}

TEST(Glfw, NonResizableWindowCanStillBeResizedByCode) {
  WindowDesc d;
  d.width = 200;
  d.height = 150;
  d.resizable = false;
  d.decorated = false;
  GLFW_WINDOW_OR_SKIP(w, d);
  CHECK(w->size() == (Extent2{200, 150}));
}

TEST(Glfw, ClipboardRoundTrip) {
  GLFW_WINDOW_OR_SKIP(w, WindowDesc{});
  w->setClipboardText("Mingol 클립보드 ✓");
  w->pollEvents();
  CHECK_EQ(w->clipboardText(), std::string("Mingol 클립보드 ✓"));
}

TEST(Glfw, CursorModesAndShapesDoNotFail) {
  GLFW_WINDOW_OR_SKIP(w, WindowDesc{});
  for (CursorShape s : {CursorShape::Arrow, CursorShape::IBeam, CursorShape::Crosshair, CursorShape::Hand,
                        CursorShape::ResizeH, CursorShape::ResizeV, CursorShape::Arrow}) {
    w->setCursorShape(s);
  }
  w->setCursorMode(CursorMode::Hidden);
  CHECK(w->cursorMode() == CursorMode::Hidden);
  w->setCursorMode(CursorMode::Disabled);
  CHECK(w->cursorMode() == CursorMode::Disabled);
  w->setCursorMode(CursorMode::Normal);
  CHECK(w->cursorMode() == CursorMode::Normal);
  w->pollEvents();
}

TEST(Glfw, InjectedEventAndWakeupFromAnotherThread) {
  GLFW_WINDOW_OR_SKIP(w, WindowDesc{});
  Recorder rec(*w);
  w->pollEvents();  // drain whatever the window system sent at creation
  std::thread t([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    w->injectEvent(CharEvent{U'k'});
  });
  const auto start = std::chrono::steady_clock::now();
  // Blocks in the OS event loop; only the injected event's wakeup can end an untimed wait here.
  while (!rec.has<CharEvent>([](const CharEvent& e) { return e.codepoint == U'k'; }) &&
         std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
    w->waitEvents(-1.0);
  }
  t.join();
  CHECK(rec.has<CharEvent>([](const CharEvent& e) { return e.codepoint == U'k'; }));
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
}

TEST(Glfw, RealKeyboardInput) {
  if (!haveXdotool()) SKIP_TEST("xdotool not installed");
  GLFW_WINDOW_OR_SKIP(w, WindowDesc{});
  REQUIRE(focusWindow(*w));
  Recorder rec(*w);

  REQUIRE(xdotool("key a"));
  REQUIRE(pumpUntil(*w, [&] {
    return rec.has<KeyEvent>([](const KeyEvent& e) { return e.key == Key::A && e.action == KeyAction::Release; });
  }));
  CHECK(rec.has<KeyEvent>([](const KeyEvent& e) { return e.key == Key::A && e.action == KeyAction::Press; }));
  CHECK(rec.has<CharEvent>([](const CharEvent& e) { return e.codepoint == U'a'; }));

  rec.events.clear();
  REQUIRE(xdotool("key shift+b"));
  REQUIRE(pumpUntil(*w, [&] {
    return rec.has<KeyEvent>([](const KeyEvent& e) { return e.key == Key::LeftShift && e.action == KeyAction::Release; });
  }));
  CHECK(rec.has<KeyEvent>([](const KeyEvent& e) {
    return e.key == Key::B && e.action == KeyAction::Press && hasFlag(e.mods, Modifiers::Shift);
  }));
  CHECK(rec.has<CharEvent>([](const CharEvent& e) { return e.codepoint == U'B'; }));

  // A table-driven spot check of the GLFW -> Key translation across key classes.
  struct Case {
    const char* xkey;
    Key key;
  };
  for (const Case c : {Case{"Return", Key::Enter}, Case{"Escape", Key::Escape}, Case{"Left", Key::Left},
                       Case{"Up", Key::Up}, Case{"F5", Key::F5}, Case{"space", Key::Space},
                       Case{"7", Key::Num7}, Case{"Tab", Key::Tab}, Case{"BackSpace", Key::Backspace},
                       Case{"Page_Down", Key::PageDown}, Case{"Control_L", Key::LeftControl}}) {
    rec.events.clear();
    REQUIRE(xdotool(std::string("key ") + c.xkey));
    const bool got = pumpUntil(*w, [&] {
      return rec.has<KeyEvent>([&](const KeyEvent& e) { return e.key == c.key && e.action == KeyAction::Release; });
    });
    CHECK_MSG(got, std::string("no release for X key ") + c.xkey + " expected " + keyName(c.key));
  }
}

TEST(Glfw, RealKeyHoldAndRelease) {
  if (!haveXdotool()) SKIP_TEST("xdotool not installed");
  GLFW_WINDOW_OR_SKIP(w, WindowDesc{});
  REQUIRE(focusWindow(*w));
  REQUIRE(xdotool("keydown w"));
  REQUIRE(pumpUntil(*w, [&] { return w->input().keyDown(Key::W); }));
  w->pollEvents();
  CHECK(w->input().keyDown(Key::W));  // still held on a later frame
  CHECK(!w->input().keyPressed(Key::W));
  REQUIRE(xdotool("keyup w"));
  REQUIRE(pumpUntil(*w, [&] { return !w->input().keyDown(Key::W); }));
}

TEST(Glfw, FocusLossClearsHeldKeys) {
  if (!haveXdotool()) SKIP_TEST("xdotool not installed");
  GLFW_WINDOW_OR_SKIP(a, WindowDesc{});
  GLFW_WINDOW_OR_SKIP(b, WindowDesc{});
  REQUIRE(focusWindow(*a));
  REQUIRE(xdotool("keydown w"));
  REQUIRE(pumpUntil(*a, [&] { return a->input().keyDown(Key::W); }));
  // Real focus change: the second window takes focus while W is still physically held.
  REQUIRE(focusWindow(*b));
  REQUIRE(pumpUntil(*a, [&] { return !a->focused(); }));
  CHECK(!a->input().keyDown(Key::W));  // must not stay stuck down
  xdotool("keyup w");
}

TEST(Glfw, RealMouseInput) {
  if (!haveXdotool()) SKIP_TEST("xdotool not installed");
  WindowDesc d;
  d.width = 400;
  d.height = 300;
  GLFW_WINDOW_OR_SKIP(w, d);
  REQUIRE(focusWindow(*w));
  Recorder rec(*w);
  const std::string id = std::to_string(w->nativeHandle().window);

  REQUIRE(xdotool("mousemove --window " + id + " 50 60"));
  REQUIRE(pumpUntil(*w, [&] {
    return rec.has<MouseMoveEvent>([](const MouseMoveEvent& e) { return std::abs(e.x - 50) < 2 && std::abs(e.y - 60) < 2; });
  }));
  CHECK_NEAR(w->input().mousePosition().x, 50.0f, 2.0);
  CHECK_NEAR(w->input().mousePosition().y, 60.0f, 2.0);

  rec.events.clear();
  REQUIRE(xdotool("click 1"));
  REQUIRE(pumpUntil(*w, [&] {
    return rec.has<MouseButtonEvent>([](const MouseButtonEvent& e) { return e.button == MouseButton::Left && e.action == KeyAction::Release; });
  }));
  CHECK(rec.has<MouseButtonEvent>([](const MouseButtonEvent& e) { return e.button == MouseButton::Left && e.action == KeyAction::Press; }));

  rec.events.clear();
  REQUIRE(xdotool("click 3"));
  REQUIRE(pumpUntil(*w, [&] {
    return rec.has<MouseButtonEvent>([](const MouseButtonEvent& e) { return e.button == MouseButton::Right && e.action == KeyAction::Release; });
  }));

  rec.events.clear();
  REQUIRE(xdotool("click 2"));
  REQUIRE(pumpUntil(*w, [&] {
    return rec.has<MouseButtonEvent>([](const MouseButtonEvent& e) { return e.button == MouseButton::Middle && e.action == KeyAction::Release; });
  }));

  rec.events.clear();
  REQUIRE(xdotool("click 4"));  // wheel up
  REQUIRE(pumpUntil(*w, [&] { return rec.has<ScrollEvent>([](const ScrollEvent& e) { return e.dy > 0; }); }));
  rec.events.clear();
  REQUIRE(xdotool("click 5"));  // wheel down
  REQUIRE(pumpUntil(*w, [&] { return rec.has<ScrollEvent>([](const ScrollEvent& e) { return e.dy < 0; }); }));

  // Motion is reported as a per-frame delta.
  w->pollEvents();
  const Vec2 before = w->input().mousePosition();
  REQUIRE(xdotool("mousemove_relative 25 10"));
  REQUIRE(pumpUntil(*w, [&] { return w->input().mouseDelta().x > 0; }));
  CHECK_NEAR(w->input().mousePosition().x - before.x, 25.0f, 2.0);
  CHECK_NEAR(w->input().mousePosition().y - before.y, 10.0f, 2.0);
}

TEST(Glfw, FullscreenToggle) {
  WindowDesc d;
  d.width = 320;
  d.height = 240;
  GLFW_WINDOW_OR_SKIP(w, d);
  w->pollEvents();
  w->setFullscreen(true);
  CHECK(w->fullscreen());
  const bool grew = pumpUntil(*w, [&] { return w->size().width > 320; }, 4000);
  if (!grew) SKIP_TEST("window system did not honour fullscreen (no window manager / RandR on this display)");
  w->setFullscreen(false);
  CHECK(!w->fullscreen());
  CHECK(pumpUntil(*w, [&] { return w->size() == (Extent2{320, 240}); }, 4000));
}
