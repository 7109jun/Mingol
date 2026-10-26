// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

namespace mingol {

// Token returned by Signal::connect. Copyable; disconnect() is idempotent.
class Connection {
 public:
  Connection() = default;
  explicit Connection(std::function<void()> disconnector) : disconnect_(std::move(disconnector)) {}

  void disconnect() {
    if (disconnect_) {
      auto fn = std::move(disconnect_);
      disconnect_ = nullptr;
      fn();
    }
  }
  bool connected() const noexcept { return static_cast<bool>(disconnect_); }

 private:
  std::function<void()> disconnect_;
};

// Disconnects when destroyed. Move-only.
class ScopedConnection {
 public:
  ScopedConnection() = default;
  ScopedConnection(Connection c) : c_(std::move(c)) {}  // NOLINT: implicit by design
  ScopedConnection(const ScopedConnection&) = delete;
  ScopedConnection& operator=(const ScopedConnection&) = delete;
  ScopedConnection(ScopedConnection&& o) noexcept : c_(std::exchange(o.c_, Connection{})) {}
  ScopedConnection& operator=(ScopedConnection&& o) noexcept {
    if (this != &o) {
      c_.disconnect();
      c_ = std::exchange(o.c_, Connection{});
    }
    return *this;
  }
  ~ScopedConnection() { c_.disconnect(); }

  void disconnect() { c_.disconnect(); }

 private:
  Connection c_;
};

// Single-threaded multicast delegate. Handlers may connect or disconnect (including themselves)
// while an emit is in progress; handlers connected during an emit first run on the next emit.
// Connections remain safe to disconnect after the Signal has been destroyed.
template <class... Args>
class Signal {
 public:
  Signal() : core_(std::make_shared<Core>()) {}

  Connection connect(std::function<void(Args...)> fn) {
    const std::uint64_t id = core_->nextId++;
    core_->slots.push_back(Slot{id, std::move(fn), true});
    std::weak_ptr<Core> weak = core_;
    return Connection([weak, id]() {
      if (auto core = weak.lock()) {
        core->remove(id);
      }
    });
  }

  void emit(Args... args) {
    const std::shared_ptr<Core> core = core_;  // keep alive even if a handler destroys the Signal
    ++core->emitting;
    const std::size_t count = core->slots.size();  // handlers added during emit are skipped
    for (std::size_t i = 0; i < count; ++i) {
      Slot& slot = core->slots[i];  // deque: stable across push_back
      if (slot.alive) {
        slot.fn(args...);
      }
    }
    if (--core->emitting == 0 && core->dirty) {
      std::erase_if(core->slots, [](const Slot& s) { return !s.alive; });
      core->dirty = false;
    }
  }

  std::size_t slotCount() const {
    std::size_t n = 0;
    for (const Slot& s : core_->slots) n += s.alive ? 1 : 0;
    return n;
  }

 private:
  struct Slot {
    std::uint64_t id;
    std::function<void(Args...)> fn;
    bool alive;
  };

  struct Core {
    std::deque<Slot> slots;
    std::uint64_t nextId = 1;
    int emitting = 0;
    bool dirty = false;

    void remove(std::uint64_t id) {
      for (Slot& s : slots) {
        if (s.id == id && s.alive) {
          s.alive = false;  // keep the function object alive while a handler may be running
          dirty = true;
          break;
        }
      }
      if (emitting == 0 && dirty) {
        std::erase_if(slots, [](const Slot& s) { return !s.alive; });
        dirty = false;
      }
    }
  };

  std::shared_ptr<Core> core_;
};

}  // namespace mingol
