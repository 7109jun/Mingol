// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mingol/core/flags.hpp"
#include "mingol/core/math.hpp"
#include "mingol/platform/key_list.hpp"

namespace mingol::platform {

enum class Key : std::uint16_t {
  Unknown = 0,
#define MINGOL_KEY_ENUM(id, glfw, name) id,
  MINGOL_KEY_LIST(MINGOL_KEY_ENUM)
#undef MINGOL_KEY_ENUM
  Count
};

inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);

// Stable script/serialisation name ("A", "Space", "LeftShift"). "Unknown" for Key::Unknown.
const char* keyName(Key key) noexcept;
// Case-sensitive inverse of keyName. nullopt when unknown.
std::optional<Key> keyFromName(std::string_view name) noexcept;

enum class MouseButton : std::uint8_t { Left, Right, Middle, Button4, Button5, Count };
inline constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);
const char* mouseButtonName(MouseButton b) noexcept;

enum class Modifiers : std::uint8_t {
  None = 0,
  Shift = 1 << 0,
  Control = 1 << 1,
  Alt = 1 << 2,
  Super = 1 << 3,
  CapsLock = 1 << 4,
  NumLock = 1 << 5,
};
MINGOL_ENUM_FLAGS(Modifiers)

enum class KeyAction : std::uint8_t { Release, Press, Repeat };

// Appends one Unicode scalar value as UTF-8. Surrogates and values above U+10FFFF are replaced by
// U+FFFD so the output is always valid UTF-8.
void appendUtf8(std::string& out, char32_t codepoint);

// Polled input state. Fed with events (see Window::pollEvents); all "pressed/released/delta"
// queries describe what happened since the last beginFrame().
class InputState {
 public:
  // Call once per frame before pumping events.
  void beginFrame();

  // Event feeding. Window does this for you; they are public so tests, replays and the editor's
  // input routing can drive the same state machine.
  void onKey(Key key, KeyAction action, Modifiers mods);
  void onMouseButton(MouseButton button, KeyAction action, Modifiers mods);
  void onMouseMove(float x, float y);
  void onScroll(float dx, float dy);
  void onChar(char32_t codepoint);
  // Window lost focus: every held key/button is released (otherwise they would stay stuck down
  // because the release goes to another window). Released keys count as "released this frame".
  void onFocusLost();

  bool keyDown(Key key) const noexcept;
  bool keyPressed(Key key) const noexcept;   // went down this frame (key repeat does not count)
  bool keyReleased(Key key) const noexcept;  // went up this frame
  bool mouseDown(MouseButton b) const noexcept;
  bool mousePressed(MouseButton b) const noexcept;
  bool mouseReleased(MouseButton b) const noexcept;

  Vec2 mousePosition() const noexcept { return pos_; }  // window coordinates, origin top-left
  Vec2 mouseDelta() const noexcept { return delta_; }   // movement since beginFrame()
  Vec2 scroll() const noexcept { return scroll_; }      // accumulated since beginFrame()
  Modifiers modifiers() const noexcept { return mods_; }
  // UTF-8 text typed since beginFrame() (IME/layout aware, unlike key presses).
  const std::string& text() const noexcept { return text_; }

 private:
  std::array<std::uint8_t, kKeyCount> keyState_{};  // bit0 down, bit1 pressed, bit2 released
  std::array<std::uint8_t, kMouseButtonCount> mouseState_{};
  Vec2 pos_{};
  Vec2 delta_{};
  Vec2 scroll_{};
  Modifiers mods_ = Modifiers::None;
  std::string text_;
  bool havePos_ = false;
};

// Maps named actions ("Jump", "Fire") to physical inputs so gameplay code does not hardcode keys.
class ActionMap {
 public:
  void bind(std::string_view action, Key key);
  void bind(std::string_view action, MouseButton button);
  void unbindAll(std::string_view action);
  void clear();

  bool down(const InputState& in, std::string_view action) const;
  bool pressed(const InputState& in, std::string_view action) const;
  bool released(const InputState& in, std::string_view action) const;

  // -1..1 from two actions, e.g. axis(in, "Left", "Right"); both down cancel out.
  float axis(const InputState& in, std::string_view negative, std::string_view positive) const;

 private:
  struct Binding {
    bool isMouse = false;
    std::uint16_t code = 0;
  };
  std::unordered_map<std::string, std::vector<Binding>> bindings_;
};

}  // namespace mingol::platform
