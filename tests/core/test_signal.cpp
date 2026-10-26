// SPDX-License-Identifier: GPL-2.0-only
#include <memory>
#include <vector>

#include "mingol/core/signal.hpp"
#include "mingol_test.hpp"

using namespace mingol;

TEST(Signal, EmitCallsAllHandlersInOrder) {
  Signal<int> sig;
  std::vector<int> calls;
  sig.connect([&](int v) { calls.push_back(v); });
  sig.connect([&](int v) { calls.push_back(v * 10); });
  sig.emit(2);
  CHECK_EQ(calls.size(), std::size_t{2});
  CHECK_EQ(calls[0], 2);
  CHECK_EQ(calls[1], 20);
}

TEST(Signal, Disconnect) {
  Signal<> sig;
  int n = 0;
  Connection c = sig.connect([&] { ++n; });
  sig.emit();
  c.disconnect();
  c.disconnect();  // idempotent
  sig.emit();
  CHECK_EQ(n, 1);
  CHECK_EQ(sig.slotCount(), std::size_t{0});
}

TEST(Signal, ScopedConnectionDisconnectsOnDestruction) {
  Signal<> sig;
  int n = 0;
  {
    ScopedConnection sc = sig.connect([&] { ++n; });
    sig.emit();
  }
  sig.emit();
  CHECK_EQ(n, 1);
}

TEST(Signal, HandlerMayDisconnectItselfDuringEmit) {
  Signal<> sig;
  int n = 0;
  Connection self;
  self = sig.connect([&] {
    ++n;
    self.disconnect();
  });
  sig.emit();
  sig.emit();
  CHECK_EQ(n, 1);
}

TEST(Signal, HandlerConnectedDuringEmitRunsNextTime) {
  Signal<> sig;
  int outer = 0, inner = 0;
  bool added = false;
  sig.connect([&] {
    ++outer;
    if (!added) {
      added = true;
      sig.connect([&] { ++inner; });
    }
  });
  sig.emit();
  CHECK_EQ(inner, 0);
  sig.emit();
  CHECK_EQ(inner, 1);
  CHECK_EQ(outer, 2);
}

TEST(Signal, ConnectionOutlivesSignal) {
  Connection c;
  {
    Signal<int> sig;
    c = sig.connect([](int) {});
  }
  c.disconnect();  // must not crash
  CHECK(!c.connected());
}

TEST(Signal, HandlerMayDestroySignalOwner) {
  auto sig = std::make_unique<Signal<>>();
  int n = 0;
  sig->connect([&] {
    ++n;
    sig.reset();
  });
  sig->emit();
  CHECK_EQ(n, 1);
  CHECK(sig == nullptr);
}
