// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/platform/event.hpp"

#include <algorithm>
#include <type_traits>

namespace mingol::platform {

namespace {

bool droppable(const Event& e) {
  return std::holds_alternative<MouseMoveEvent>(e) || std::holds_alternative<ScrollEvent>(e) ||
         std::holds_alternative<CursorEnterEvent>(e);
}

// Merges `incoming` into `back` when they are the same coalescable kind.
bool coalesce(Event& back, const Event& incoming) {
  if (auto* a = std::get_if<MouseMoveEvent>(&back)) {
    if (const auto* b = std::get_if<MouseMoveEvent>(&incoming)) {
      *a = *b;
      return true;
    }
  } else if (auto* r = std::get_if<ResizedEvent>(&back)) {
    if (const auto* b = std::get_if<ResizedEvent>(&incoming)) {
      *r = *b;
      return true;
    }
  } else if (auto* f = std::get_if<FramebufferResizedEvent>(&back)) {
    if (const auto* b = std::get_if<FramebufferResizedEvent>(&incoming)) {
      *f = *b;
      return true;
    }
  } else if (auto* s = std::get_if<ScrollEvent>(&back)) {
    if (const auto* b = std::get_if<ScrollEvent>(&incoming)) {
      s->dx += b->dx;
      s->dy += b->dy;
      return true;
    }
  }
  return false;
}

}  // namespace

void EventQueue::push(Event e) {
  if (!q_.empty() && coalesce(q_.back(), e)) return;
  if (q_.size() >= capacity_) {
    if (droppable(e)) {
      ++dropped_;
      return;
    }
    // A critical event (key release, focus, close, ...) must survive: make room by evicting the
    // oldest droppable one. If there is none the queue grows past capacity rather than lose it.
    const auto it = std::find_if(q_.begin(), q_.end(), droppable);
    if (it != q_.end()) {
      q_.erase(it);
      ++dropped_;
    }
  }
  q_.push_back(std::move(e));
}

bool EventQueue::pop(Event& out) {
  if (q_.empty()) return false;
  out = std::move(q_.front());
  q_.pop_front();
  return true;
}

void applyToInput(InputState& in, const Event& e) {
  std::visit(
      [&](const auto& ev) {
        using T = std::decay_t<decltype(ev)>;
        if constexpr (std::is_same_v<T, KeyEvent>) {
          in.onKey(ev.key, ev.action, ev.mods);
        } else if constexpr (std::is_same_v<T, CharEvent>) {
          in.onChar(ev.codepoint);
        } else if constexpr (std::is_same_v<T, MouseButtonEvent>) {
          in.onMouseButton(ev.button, ev.action, ev.mods);
        } else if constexpr (std::is_same_v<T, MouseMoveEvent>) {
          in.onMouseMove(ev.x, ev.y);
        } else if constexpr (std::is_same_v<T, ScrollEvent>) {
          in.onScroll(ev.dx, ev.dy);
        } else if constexpr (std::is_same_v<T, FocusEvent>) {
          if (!ev.focused) in.onFocusLost();
        }
      },
      e);
}

}  // namespace mingol::platform
