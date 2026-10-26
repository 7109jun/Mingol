// SPDX-License-Identifier: GPL-2.0-only
// Window behaviour on the headless backend (deterministic, runs everywhere), plus the same
// contract on the real GLFW backend when a display is available.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

#include "mingol/platform/window.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::platform;

namespace {

std::unique_ptr<Window> makeHeadless(std::uint32_t w = 320, std::uint32_t h = 200) {
  WindowDesc d;
  d.width = w;
  d.height = h;
  d.title = "headless";
  d.backend = WindowBackend::Headless;
  auto r = Window::create(d);
  if (!r.ok()) {
    test::fail(__FILE__, __LINE__, r.error().describe());
    return nullptr;
  }
  return std::move(r).value();
}

}  // namespace

TEST(Headless, CreateAndQuery) {
  auto w = makeHeadless(640, 480);
  REQUIRE(w != nullptr);
  CHECK_EQ(std::string(w->backendName()), std::string("headless"));
  CHECK(w->size() == (Extent2{640, 480}));
  CHECK(w->framebufferSize() == (Extent2{640, 480}));
  CHECK_EQ(w->title(), std::string("headless"));
  CHECK(!w->shouldClose());
  CHECK(w->focused());
  CHECK(w->nativeHandle().kind == NativeHandle::Kind::None);
  CHECK_NEAR(w->contentScale().x, 1.0f, 1e-6);
}

TEST(Headless, RejectsZeroSize) {
  WindowDesc d;
  d.backend = WindowBackend::Headless;
  d.width = 0;
  CHECK(!Window::create(d).ok());
  d.width = 10;
  d.height = 0;
  const auto r = Window::create(d);
  CHECK(!r.ok());
  if (!r.ok()) CHECK(r.error().code == ErrorCode::InvalidArgument);
}

TEST(Headless, BackendAvailability) {
  CHECK(Window::backendAvailable(WindowBackend::Headless));
#if !defined(MINGOL_HAVE_GLFW)
  CHECK(!Window::backendAvailable(WindowBackend::Glfw));
  WindowDesc d;
  d.backend = WindowBackend::Glfw;
  const auto r = Window::create(d);
  CHECK(!r.ok());
  if (!r.ok()) CHECK(r.error().code == ErrorCode::Unsupported);
#endif
}

TEST(Headless, InjectedEventsDispatchInOrderAndUpdateState) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  std::vector<std::string> order;
  Extent2 sizeSeenByHandler{};
  auto conn = w->events().connect([&](const Event& e) {
    if (std::holds_alternative<KeyEvent>(e)) order.push_back("key");
    if (std::holds_alternative<CharEvent>(e)) order.push_back("char");
    if (std::holds_alternative<ResizedEvent>(e)) {
      order.push_back("resize");
      sizeSeenByHandler = w->size();  // cached state is updated before handlers run
    }
  });
  w->injectEvent(KeyEvent{Key::H, 0, KeyAction::Press, Modifiers::None});
  w->injectEvent(CharEvent{U'h'});
  w->injectEvent(ResizedEvent{800, 600});
  CHECK(w->size() == (Extent2{320, 200}));  // nothing happens until polled
  w->pollEvents();
  CHECK_EQ(order.size(), 3u);
  if (order.size() == 3) {
    CHECK_EQ(order[0], std::string("key"));
    CHECK_EQ(order[1], std::string("char"));
    CHECK_EQ(order[2], std::string("resize"));
  }
  CHECK(sizeSeenByHandler == (Extent2{800, 600}));
  CHECK(w->input().keyDown(Key::H));
  CHECK(w->input().keyPressed(Key::H));
  CHECK_EQ(w->input().text(), std::string("h"));

  w->pollEvents();  // next frame: per-frame state resets, held state stays
  CHECK(w->input().keyDown(Key::H));
  CHECK(!w->input().keyPressed(Key::H));
  CHECK(w->input().text().empty());
}

TEST(Headless, CloseRequestAndFocus) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  w->injectEvent(KeyEvent{Key::W, 0, KeyAction::Press, Modifiers::None});
  w->pollEvents();
  CHECK(w->input().keyDown(Key::W));
  w->injectEvent(FocusEvent{false});
  w->pollEvents();
  CHECK(!w->focused());
  CHECK(!w->input().keyDown(Key::W));  // focus loss must not leave keys stuck
  w->injectEvent(IconifyEvent{true});
  w->injectEvent(CloseRequestedEvent{});
  w->pollEvents();
  CHECK(w->iconified());
  CHECK(w->shouldClose());
}

TEST(Headless, SetSizeAndTitleAndCursorAndClipboard) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  w->setSize(500, 400);
  CHECK(w->size() == (Extent2{320, 200}));  // reported through events, like a real window
  w->pollEvents();
  CHECK(w->size() == (Extent2{500, 400}));
  CHECK(w->framebufferSize() == (Extent2{500, 400}));
  w->setSize(0, 10);  // ignored
  w->pollEvents();
  CHECK(w->size() == (Extent2{500, 400}));

  w->setTitle("renamed");
  CHECK_EQ(w->title(), std::string("renamed"));
  w->setCursorMode(CursorMode::Disabled);
  CHECK(w->cursorMode() == CursorMode::Disabled);
  w->setCursorShape(CursorShape::Hand);
  w->setFullscreen(true);
  CHECK(w->fullscreen());

  CHECK(w->clipboardText().empty());
  w->setClipboardText("한글 text");
  CHECK_EQ(w->clipboardText(), std::string("한글 text"));
}

TEST(Headless, HandlerMayDisconnectItself) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  int calls = 0;
  Connection c;
  c = w->events().connect([&](const Event&) {
    ++calls;
    c.disconnect();
  });
  w->injectEvent(CharEvent{U'a'});
  w->injectEvent(CharEvent{U'b'});
  w->pollEvents();
  CHECK_EQ(calls, 1);
}

TEST(Headless, WaitEventsTimesOut) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  const auto start = std::chrono::steady_clock::now();
  w->waitEvents(0.05);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  CHECK(ms >= 40);
  CHECK(ms < 2000);
}

TEST(Headless, WaitEventsReturnsImmediatelyWhenEventsPending) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  w->injectEvent(CharEvent{U'x'});
  const auto start = std::chrono::steady_clock::now();
  w->waitEvents(10.0);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  CHECK(ms < 1000);
  CHECK_EQ(w->input().text(), std::string("x"));
}

TEST(Headless, WaitEventsWokenByOtherThread) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  std::thread t([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    w->injectEvent(CharEvent{U'z'});
  });
  const auto start = std::chrono::steady_clock::now();
  w->waitEvents(-1.0);  // would hang forever if the wakeup were lost
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  t.join();
  CHECK(ms < 5000);
  CHECK_EQ(w->input().text(), std::string("z"));
}

TEST(Headless, ConcurrentInjectionLosesNothing) {
  auto w = makeHeadless();
  REQUIRE(w != nullptr);
  constexpr int kThreads = 4;
  constexpr int kPerThread = 500;
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      while (!go.load()) std::this_thread::yield();
      for (int i = 0; i < kPerThread; ++i) w->injectEvent(CharEvent{U'q'});
    });
  }
  std::size_t chars = 0;
  auto conn = w->events().connect([&](const Event& e) { chars += std::holds_alternative<CharEvent>(e) ? 1 : 0; });
  go = true;
  // Poll while producers run, then drain; events arrive in arbitrary batches but none may vanish.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (chars < static_cast<std::size_t>(kThreads * kPerThread) &&
         std::chrono::steady_clock::now() < deadline) {
    w->waitEvents(0.01);
  }
  for (auto& th : threads) th.join();
  w->pollEvents();
  CHECK_EQ(chars, static_cast<std::size_t>(kThreads * kPerThread));
}

TEST(Headless, ManyWindowsAreIndependent) {
  auto a = makeHeadless();
  auto b = makeHeadless();
  REQUIRE(a != nullptr && b != nullptr);
  a->injectEvent(KeyEvent{Key::A, 0, KeyAction::Press, Modifiers::None});
  a->pollEvents();
  b->pollEvents();
  CHECK(a->input().keyDown(Key::A));
  CHECK(!b->input().keyDown(Key::A));
}
