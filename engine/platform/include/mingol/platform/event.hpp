// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <variant>
#include <vector>

#include "mingol/platform/input.hpp"

namespace mingol::platform {

struct CloseRequestedEvent {};
struct ResizedEvent {  // logical window size
  std::uint32_t width, height;
};
struct FramebufferResizedEvent {  // pixels; this is what swapchains follow
  std::uint32_t width, height;
};
struct FocusEvent {
  bool focused;
};
struct IconifyEvent {
  bool iconified;
};
struct KeyEvent {
  Key key;
  int scancode;  // platform specific; stable only on one machine
  KeyAction action;
  Modifiers mods;
};
struct CharEvent {
  char32_t codepoint;
};
struct MouseButtonEvent {
  MouseButton button;
  KeyAction action;  // Press or Release
  Modifiers mods;
};
struct MouseMoveEvent {
  float x, y;
};
struct ScrollEvent {
  float dx, dy;
};
struct CursorEnterEvent {
  bool entered;
};
struct FilesDroppedEvent {
  std::vector<std::string> paths;  // UTF-8
};

using Event = std::variant<CloseRequestedEvent, ResizedEvent, FramebufferResizedEvent, FocusEvent,
                           IconifyEvent, KeyEvent, CharEvent, MouseButtonEvent, MouseMoveEvent,
                           ScrollEvent, CursorEnterEvent, FilesDroppedEvent>;

// Bounded FIFO. Bursts of high-rate events are coalesced instead of growing without bound:
//  - consecutive MouseMoveEvents keep only the latest position,
//  - consecutive Resized/FramebufferResized events keep only the latest size,
//  - consecutive ScrollEvents are summed.
// If the queue is still full after coalescing the *new* event is dropped and counted; a dropped
// Release event would leave input stuck, so key/button/focus events are never dropped: the oldest
// droppable event (move/scroll/char/enter) is evicted first.
class EventQueue {
 public:
  explicit EventQueue(std::size_t capacity = 4096) : capacity_(capacity) {}

  void push(Event e);
  bool pop(Event& out);
  std::size_t size() const noexcept { return q_.size(); }
  bool empty() const noexcept { return q_.empty(); }
  void clear() noexcept { q_.clear(); }
  std::size_t droppedCount() const noexcept { return dropped_; }

 private:
  std::deque<Event> q_;
  std::size_t capacity_;
  std::size_t dropped_ = 0;
};

// Applies an event to an InputState (non-input events are ignored).
void applyToInput(InputState& input, const Event& e);

}  // namespace mingol::platform
