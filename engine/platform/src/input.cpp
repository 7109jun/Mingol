// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/platform/input.hpp"

#include <algorithm>

namespace mingol::platform {

namespace {

constexpr std::uint8_t kDown = 1;
constexpr std::uint8_t kPressed = 2;
constexpr std::uint8_t kReleased = 4;

constexpr const char* kKeyNames[] = {
    "Unknown",
#define MINGOL_KEY_NAME(id, glfw, name) name,
    MINGOL_KEY_LIST(MINGOL_KEY_NAME)
#undef MINGOL_KEY_NAME
};
static_assert(sizeof(kKeyNames) / sizeof(kKeyNames[0]) == kKeyCount, "key name table out of sync");

void press(std::uint8_t& s) {
  if (!(s & kDown)) s = static_cast<std::uint8_t>(s | kDown | kPressed);
}
void release(std::uint8_t& s) {
  if (s & kDown) s = static_cast<std::uint8_t>((s & ~kDown) | kReleased);
}

}  // namespace

const char* keyName(Key key) noexcept {
  const auto i = static_cast<std::size_t>(key);
  return i < kKeyCount ? kKeyNames[i] : "Unknown";
}

std::optional<Key> keyFromName(std::string_view name) noexcept {
  for (std::size_t i = 1; i < kKeyCount; ++i) {
    if (name == kKeyNames[i]) return static_cast<Key>(i);
  }
  return std::nullopt;
}

const char* mouseButtonName(MouseButton b) noexcept {
  switch (b) {
    case MouseButton::Left: return "Left";
    case MouseButton::Right: return "Right";
    case MouseButton::Middle: return "Middle";
    case MouseButton::Button4: return "Button4";
    case MouseButton::Button5: return "Button5";
    case MouseButton::Count: break;
  }
  return "Unknown";
}

void appendUtf8(std::string& out, char32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void InputState::beginFrame() {
  for (auto& s : keyState_) s = static_cast<std::uint8_t>(s & kDown);
  for (auto& s : mouseState_) s = static_cast<std::uint8_t>(s & kDown);
  delta_ = {};
  scroll_ = {};
  text_.clear();
}

void InputState::onKey(Key key, KeyAction action, Modifiers mods) {
  mods_ = mods;
  const auto i = static_cast<std::size_t>(key);
  if (key == Key::Unknown || i >= kKeyCount) return;
  if (action == KeyAction::Press) {
    press(keyState_[i]);
  } else if (action == KeyAction::Release) {
    release(keyState_[i]);
  }
}

void InputState::onMouseButton(MouseButton button, KeyAction action, Modifiers mods) {
  mods_ = mods;
  const auto i = static_cast<std::size_t>(button);
  if (i >= kMouseButtonCount) return;
  if (action == KeyAction::Press) {
    press(mouseState_[i]);
  } else if (action == KeyAction::Release) {
    release(mouseState_[i]);
  }
}

void InputState::onMouseMove(float x, float y) {
  if (havePos_) delta_ = delta_ + Vec2{x - pos_.x, y - pos_.y};
  pos_ = {x, y};
  havePos_ = true;
}

void InputState::onScroll(float dx, float dy) { scroll_ = scroll_ + Vec2{dx, dy}; }

void InputState::onChar(char32_t cp) {
  // C0/C1 control characters are not text; they arrive as key events.
  if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return;
  appendUtf8(text_, cp);
}

void InputState::onFocusLost() {
  for (auto& s : keyState_) release(s);
  for (auto& s : mouseState_) release(s);
  mods_ = Modifiers::None;
}

bool InputState::keyDown(Key k) const noexcept {
  const auto i = static_cast<std::size_t>(k);
  return i < kKeyCount && (keyState_[i] & kDown);
}
bool InputState::keyPressed(Key k) const noexcept {
  const auto i = static_cast<std::size_t>(k);
  return i < kKeyCount && (keyState_[i] & kPressed);
}
bool InputState::keyReleased(Key k) const noexcept {
  const auto i = static_cast<std::size_t>(k);
  return i < kKeyCount && (keyState_[i] & kReleased);
}
bool InputState::mouseDown(MouseButton b) const noexcept {
  const auto i = static_cast<std::size_t>(b);
  return i < kMouseButtonCount && (mouseState_[i] & kDown);
}
bool InputState::mousePressed(MouseButton b) const noexcept {
  const auto i = static_cast<std::size_t>(b);
  return i < kMouseButtonCount && (mouseState_[i] & kPressed);
}
bool InputState::mouseReleased(MouseButton b) const noexcept {
  const auto i = static_cast<std::size_t>(b);
  return i < kMouseButtonCount && (mouseState_[i] & kReleased);
}

void ActionMap::bind(std::string_view action, Key key) {
  auto& v = bindings_[std::string(action)];
  const Binding b{false, static_cast<std::uint16_t>(key)};
  const bool dup = std::any_of(v.begin(), v.end(), [&](const Binding& o) {
    return o.isMouse == b.isMouse && o.code == b.code;
  });
  if (!dup) v.push_back(b);
}

void ActionMap::bind(std::string_view action, MouseButton button) {
  auto& v = bindings_[std::string(action)];
  const Binding b{true, static_cast<std::uint16_t>(button)};
  const bool dup = std::any_of(v.begin(), v.end(), [&](const Binding& o) {
    return o.isMouse == b.isMouse && o.code == b.code;
  });
  if (!dup) v.push_back(b);
}

void ActionMap::unbindAll(std::string_view action) { bindings_.erase(std::string(action)); }
void ActionMap::clear() { bindings_.clear(); }

bool ActionMap::down(const InputState& in, std::string_view action) const {
  const auto it = bindings_.find(std::string(action));
  if (it == bindings_.end()) return false;
  return std::any_of(it->second.begin(), it->second.end(), [&](const Binding& b) {
    return b.isMouse ? in.mouseDown(static_cast<MouseButton>(b.code))
                     : in.keyDown(static_cast<Key>(b.code));
  });
}

bool ActionMap::pressed(const InputState& in, std::string_view action) const {
  const auto it = bindings_.find(std::string(action));
  if (it == bindings_.end()) return false;
  return std::any_of(it->second.begin(), it->second.end(), [&](const Binding& b) {
    return b.isMouse ? in.mousePressed(static_cast<MouseButton>(b.code))
                     : in.keyPressed(static_cast<Key>(b.code));
  });
}

bool ActionMap::released(const InputState& in, std::string_view action) const {
  const auto it = bindings_.find(std::string(action));
  if (it == bindings_.end()) return false;
  return std::any_of(it->second.begin(), it->second.end(), [&](const Binding& b) {
    return b.isMouse ? in.mouseReleased(static_cast<MouseButton>(b.code))
                     : in.keyReleased(static_cast<Key>(b.code));
  });
}

float ActionMap::axis(const InputState& in, std::string_view negative, std::string_view positive) const {
  return (down(in, positive) ? 1.0F : 0.0F) - (down(in, negative) ? 1.0F : 0.0F);
}

}  // namespace mingol::platform
