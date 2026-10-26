// SPDX-License-Identifier: GPL-2.0-only
// Input state machine, key tables, text encoding, event queue: pure logic, no window needed.
#include <set>
#include <string>

#include "mingol/platform/event.hpp"
#include "mingol/platform/input.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::platform;

TEST(Keys, NamesAreUniqueAndRoundTrip) {
  std::set<std::string> seen;
  for (std::size_t i = 1; i < kKeyCount; ++i) {
    const Key k = static_cast<Key>(i);
    const std::string name = keyName(k);
    CHECK_MSG(name != "Unknown", "key " + std::to_string(i) + " has no name");
    CHECK_MSG(seen.insert(name).second, "duplicate key name " + name);
    const auto back = keyFromName(name);
    CHECK(back.has_value());
    if (back) CHECK(*back == k);
  }
  CHECK_EQ(seen.size(), kKeyCount - 1);
  CHECK(!keyFromName("NoSuchKey").has_value());
  CHECK(!keyFromName("").has_value());
  CHECK(!keyFromName("a").has_value());  // case-sensitive
  CHECK(keyFromName("A").value() == Key::A);
  CHECK(keyFromName("Space").value() == Key::Space);
  CHECK(keyFromName("0").value() == Key::Num0);
  CHECK(keyFromName("\\").value() == Key::Backslash);
  CHECK_EQ(std::string(keyName(Key::Unknown)), std::string("Unknown"));
  CHECK_EQ(std::string(keyName(static_cast<Key>(60000))), std::string("Unknown"));
}

TEST(Input, PressHoldRelease) {
  InputState in;
  in.beginFrame();
  CHECK(!in.keyDown(Key::W));
  in.onKey(Key::W, KeyAction::Press, Modifiers::None);
  CHECK(in.keyDown(Key::W));
  CHECK(in.keyPressed(Key::W));
  CHECK(!in.keyReleased(Key::W));

  in.beginFrame();  // held into the next frame: down, but no longer "pressed"
  CHECK(in.keyDown(Key::W));
  CHECK(!in.keyPressed(Key::W));

  in.onKey(Key::W, KeyAction::Repeat, Modifiers::None);  // auto-repeat must not re-trigger pressed
  CHECK(!in.keyPressed(Key::W));
  CHECK(in.keyDown(Key::W));

  in.onKey(Key::W, KeyAction::Release, Modifiers::None);
  CHECK(!in.keyDown(Key::W));
  CHECK(in.keyReleased(Key::W));

  in.beginFrame();
  CHECK(!in.keyReleased(Key::W));
}

TEST(Input, TapWithinOneFrameIsNotLost) {
  InputState in;
  in.beginFrame();
  in.onKey(Key::Space, KeyAction::Press, Modifiers::None);
  in.onKey(Key::Space, KeyAction::Release, Modifiers::None);
  CHECK(!in.keyDown(Key::Space));
  CHECK(in.keyPressed(Key::Space));
  CHECK(in.keyReleased(Key::Space));
}

TEST(Input, DuplicatePressDoesNotCreateSecondPress) {
  InputState in;
  in.beginFrame();
  in.onKey(Key::A, KeyAction::Press, Modifiers::None);
  in.beginFrame();
  in.onKey(Key::A, KeyAction::Press, Modifiers::None);  // already down (lost release): not a new press
  CHECK(!in.keyPressed(Key::A));
  CHECK(in.keyDown(Key::A));
}

TEST(Input, UnknownKeyAndStrayReleaseAreIgnored) {
  InputState in;
  in.beginFrame();
  in.onKey(Key::Unknown, KeyAction::Press, Modifiers::None);
  in.onKey(Key::B, KeyAction::Release, Modifiers::None);  // never pressed
  CHECK(!in.keyReleased(Key::B));
  CHECK(!in.keyDown(Key::Unknown));
  CHECK(!in.keyDown(static_cast<Key>(60000)));  // out of range query is safe
}

TEST(Input, ModifiersFollowEvents) {
  InputState in;
  in.onKey(Key::LeftShift, KeyAction::Press, Modifiers::Shift);
  CHECK(hasFlag(in.modifiers(), Modifiers::Shift));
  in.onKey(Key::A, KeyAction::Press, Modifiers::Shift | Modifiers::Control);
  CHECK(hasFlag(in.modifiers(), Modifiers::Control));
  in.onKey(Key::A, KeyAction::Release, Modifiers::None);
  CHECK(!anyFlag(in.modifiers()));
}

TEST(Input, FocusLossReleasesEverything) {
  InputState in;
  in.beginFrame();
  in.onKey(Key::W, KeyAction::Press, Modifiers::None);
  in.onKey(Key::LeftShift, KeyAction::Press, Modifiers::Shift);
  in.onMouseButton(MouseButton::Left, KeyAction::Press, Modifiers::None);
  in.beginFrame();
  in.onFocusLost();
  CHECK(!in.keyDown(Key::W));
  CHECK(in.keyReleased(Key::W));
  CHECK(!in.keyDown(Key::LeftShift));
  CHECK(!in.mouseDown(MouseButton::Left));
  CHECK(in.mouseReleased(MouseButton::Left));
  CHECK(!anyFlag(in.modifiers()));
  in.beginFrame();
  CHECK(!in.keyReleased(Key::W));
}

TEST(Input, MouseDeltaAndScroll) {
  InputState in;
  in.beginFrame();
  in.onMouseMove(100, 50);  // first sample establishes the position; no jump from (0,0)
  CHECK_NEAR(in.mouseDelta().x, 0.0f, 1e-6);
  CHECK_NEAR(in.mouseDelta().y, 0.0f, 1e-6);
  CHECK_NEAR(in.mousePosition().x, 100.0f, 1e-6);

  in.beginFrame();
  in.onMouseMove(110, 45);
  in.onMouseMove(130, 60);
  CHECK_NEAR(in.mouseDelta().x, 30.0f, 1e-5);
  CHECK_NEAR(in.mouseDelta().y, 10.0f, 1e-5);
  CHECK_NEAR(in.mousePosition().x, 130.0f, 1e-5);
  in.onScroll(0, 1);
  in.onScroll(0.5f, 2);
  CHECK_NEAR(in.scroll().x, 0.5f, 1e-6);
  CHECK_NEAR(in.scroll().y, 3.0f, 1e-6);

  in.beginFrame();
  CHECK_NEAR(in.mouseDelta().x, 0.0f, 1e-6);
  CHECK_NEAR(in.scroll().y, 0.0f, 1e-6);
  CHECK_NEAR(in.mousePosition().x, 130.0f, 1e-5);  // position persists
}

TEST(Input, MouseButtons) {
  InputState in;
  in.beginFrame();
  in.onMouseButton(MouseButton::Right, KeyAction::Press, Modifiers::None);
  CHECK(in.mouseDown(MouseButton::Right) && in.mousePressed(MouseButton::Right));
  CHECK(!in.mouseDown(MouseButton::Left));
  in.beginFrame();
  in.onMouseButton(MouseButton::Right, KeyAction::Release, Modifiers::None);
  CHECK(!in.mouseDown(MouseButton::Right) && in.mouseReleased(MouseButton::Right));
  CHECK(!in.mouseDown(static_cast<MouseButton>(200)));
}

TEST(Text, Utf8Encoding) {
  std::string s;
  appendUtf8(s, U'A');
  CHECK_EQ(s, std::string("A"));
  s.clear();
  appendUtf8(s, 0xE9);  // é
  CHECK_EQ(s, std::string("\xC3\xA9"));
  s.clear();
  appendUtf8(s, 0xD55C);  // 한
  CHECK_EQ(s, std::string("\xED\x95\x9C"));
  s.clear();
  appendUtf8(s, 0x1F600);  // emoji, 4 bytes
  CHECK_EQ(s, std::string("\xF0\x9F\x98\x80"));
  s.clear();
  appendUtf8(s, 0x10FFFF);
  CHECK_EQ(s, std::string("\xF4\x8F\xBF\xBF"));
  // Invalid scalar values become U+FFFD instead of malformed UTF-8.
  for (const char32_t bad : {char32_t{0xD800}, char32_t{0xDFFF}, char32_t{0x110000}, char32_t{0xFFFFFFFF}}) {
    s.clear();
    appendUtf8(s, bad);
    CHECK_EQ(s, std::string("\xEF\xBF\xBD"));
  }
  // Boundaries of each length class.
  s.clear();
  appendUtf8(s, 0x7F);
  appendUtf8(s, 0x80);
  appendUtf8(s, 0x7FF);
  appendUtf8(s, 0x800);
  appendUtf8(s, 0xFFFF);
  appendUtf8(s, 0x10000);
  CHECK_EQ(s.size(), 1u + 2u + 2u + 3u + 3u + 4u);
}

TEST(Text, CharInputFiltersControlCodes) {
  InputState in;
  in.beginFrame();
  in.onChar(U'h');
  in.onChar(0x0A);    // newline arrives as a key event, not text
  in.onChar(0x00);
  in.onChar(0x7F);
  in.onChar(0x85);
  in.onChar(0xD55C);
  CHECK_EQ(in.text(), std::string("h\xED\x95\x9C"));
  in.beginFrame();
  CHECK(in.text().empty());
}

TEST(Actions, BindQueryAndAxis) {
  InputState in;
  ActionMap map;
  map.bind("Jump", Key::Space);
  map.bind("Jump", Key::Space);  // duplicate bind is harmless
  map.bind("Jump", MouseButton::Right);
  map.bind("Left", Key::A);
  map.bind("Right", Key::D);

  in.beginFrame();
  CHECK(!map.down(in, "Jump"));
  CHECK(!map.down(in, "Undefined"));
  in.onKey(Key::Space, KeyAction::Press, Modifiers::None);
  CHECK(map.down(in, "Jump") && map.pressed(in, "Jump"));
  in.beginFrame();
  in.onKey(Key::Space, KeyAction::Release, Modifiers::None);
  CHECK(map.released(in, "Jump"));
  in.beginFrame();
  in.onMouseButton(MouseButton::Right, KeyAction::Press, Modifiers::None);
  CHECK(map.down(in, "Jump"));  // any bound input triggers the action

  CHECK_NEAR(map.axis(in, "Left", "Right"), 0.0f, 1e-6);
  in.onKey(Key::D, KeyAction::Press, Modifiers::None);
  CHECK_NEAR(map.axis(in, "Left", "Right"), 1.0f, 1e-6);
  in.onKey(Key::A, KeyAction::Press, Modifiers::None);
  CHECK_NEAR(map.axis(in, "Left", "Right"), 0.0f, 1e-6);  // opposite keys cancel
  in.onKey(Key::D, KeyAction::Release, Modifiers::None);
  CHECK_NEAR(map.axis(in, "Left", "Right"), -1.0f, 1e-6);

  map.unbindAll("Jump");
  CHECK(!map.down(in, "Jump"));
  map.clear();
  CHECK(!map.down(in, "Left"));
}

// ---- EventQueue ----------------------------------------------------------------------------------

namespace {
template <class T>
const T* as(const Event& e) {
  return std::get_if<T>(&e);
}
}  // namespace

TEST(EventQueue, FifoOrder) {
  EventQueue q;
  q.push(KeyEvent{Key::A, 0, KeyAction::Press, Modifiers::None});
  q.push(CharEvent{U'a'});
  q.push(KeyEvent{Key::A, 0, KeyAction::Release, Modifiers::None});
  Event e;
  REQUIRE(q.pop(e));
  CHECK(as<KeyEvent>(e) && as<KeyEvent>(e)->action == KeyAction::Press);
  REQUIRE(q.pop(e));
  CHECK(as<CharEvent>(e) != nullptr);
  REQUIRE(q.pop(e));
  CHECK(as<KeyEvent>(e) && as<KeyEvent>(e)->action == KeyAction::Release);
  CHECK(!q.pop(e));
  CHECK(q.empty());
}

TEST(EventQueue, CoalescesBursts) {
  EventQueue q;
  for (int i = 0; i < 1000; ++i) q.push(MouseMoveEvent{float(i), float(i)});
  CHECK_EQ(q.size(), 1u);
  q.push(ScrollEvent{0, 1});
  q.push(ScrollEvent{0, 2});
  CHECK_EQ(q.size(), 2u);
  q.push(ResizedEvent{10, 10});
  q.push(ResizedEvent{20, 30});
  q.push(FramebufferResizedEvent{20, 30});
  CHECK_EQ(q.size(), 4u);

  Event e;
  REQUIRE(q.pop(e));
  CHECK_NEAR(as<MouseMoveEvent>(e)->x, 999.0f, 1e-6);  // latest position wins
  REQUIRE(q.pop(e));
  CHECK_NEAR(as<ScrollEvent>(e)->dy, 3.0f, 1e-6);  // scrolls are summed, not lost
  REQUIRE(q.pop(e));
  CHECK_EQ(as<ResizedEvent>(e)->width, 20u);
  CHECK_EQ(q.droppedCount(), 0u);  // coalescing is not dropping
}

TEST(EventQueue, DoesNotCoalesceAcrossOtherEvents) {
  EventQueue q;
  q.push(MouseMoveEvent{1, 1});
  q.push(MouseButtonEvent{MouseButton::Left, KeyAction::Press, Modifiers::None});
  q.push(MouseMoveEvent{2, 2});  // the press happened at (1,1); order must be preserved
  CHECK_EQ(q.size(), 3u);
}

TEST(EventQueue, BoundedAndNeverDropsCriticalEvents) {
  EventQueue q(8);
  // Alternate move/press so nothing coalesces; the queue fills with droppable + critical events.
  for (int i = 0; i < 100; ++i) {
    q.push(MouseMoveEvent{float(i), 0});
    q.push(KeyEvent{Key::A, 0, i % 2 ? KeyAction::Release : KeyAction::Press, Modifiers::None});
  }
  std::size_t keys = 0;
  Event e;
  while (q.pop(e)) keys += as<KeyEvent>(e) ? 1 : 0;
  CHECK_EQ(keys, 100u);  // every key event survived
  CHECK(q.droppedCount() > 0);
}

TEST(EventQueue, CriticalEventsMayExceedCapacityRatherThanBeLost) {
  EventQueue q(4);
  for (int i = 0; i < 20; ++i) q.push(KeyEvent{Key::B, 0, KeyAction::Press, Modifiers::None});
  CHECK_EQ(q.size(), 20u);
  CHECK_EQ(q.droppedCount(), 0u);
}

TEST(EventQueue, ApplyToInput) {
  InputState in;
  in.beginFrame();
  applyToInput(in, KeyEvent{Key::E, 0, KeyAction::Press, Modifiers::Alt});
  applyToInput(in, CharEvent{U'e'});
  applyToInput(in, MouseMoveEvent{5, 6});
  applyToInput(in, ScrollEvent{0, 2});
  applyToInput(in, ResizedEvent{1, 1});  // ignored
  CHECK(in.keyDown(Key::E));
  CHECK(hasFlag(in.modifiers(), Modifiers::Alt));
  CHECK_EQ(in.text(), std::string("e"));
  CHECK_NEAR(in.mousePosition().y, 6.0f, 1e-6);
  CHECK_NEAR(in.scroll().y, 2.0f, 1e-6);
  applyToInput(in, FocusEvent{false});
  CHECK(!in.keyDown(Key::E));
  applyToInput(in, FocusEvent{true});  // gaining focus changes nothing
}
