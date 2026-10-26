// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <memory>
#include <string>

#include "mingol/core/memory.hpp"
#include "mingol_test.hpp"

using namespace mingol;

TEST(LinearAllocator, AlignmentAndExhaustion) {
  LinearAllocator a(256);
  void* p1 = a.allocate(1, 1);
  void* p2 = a.allocate(8, 64);
  REQUIRE(p1 != nullptr);
  REQUIRE(p2 != nullptr);
  CHECK_EQ(reinterpret_cast<std::uintptr_t>(p2) % 64, 0u);
  CHECK(a.allocate(1000) == nullptr);  // does not fit
  CHECK(a.allocate(static_cast<std::size_t>(-1)) == nullptr);  // overflow-safe
  const std::size_t used = a.used();
  CHECK(a.allocate(1, 1) != nullptr);
  CHECK(a.used() > used);
  a.reset();
  CHECK_EQ(a.used(), std::size_t{0});
}

TEST(LinearAllocator, CreateTrivialObject) {
  struct Vec {
    float x, y, z;
  };
  LinearAllocator a(128);
  Vec* v = a.create<Vec>(Vec{1, 2, 3});
  REQUIRE(v != nullptr);
  CHECK_NEAR(v->y, 2.0, 0.0);
  CHECK_EQ(reinterpret_cast<std::uintptr_t>(v) % alignof(Vec), 0u);
}

TEST(SlotMap, InsertGetErase) {
  SlotMap<std::string> map;
  const Handle a = map.insert("alpha");
  const Handle b = map.insert("beta");
  CHECK_EQ(map.size(), std::size_t{2});
  REQUIRE(map.get(a) != nullptr);
  CHECK(*map.get(a) == "alpha");
  CHECK(*map.get(b) == "beta");

  CHECK(map.erase(a));
  CHECK(!map.erase(a));  // double erase is rejected
  CHECK(map.get(a) == nullptr);
  CHECK_EQ(map.size(), std::size_t{1});
}

TEST(SlotMap, StaleHandleDoesNotAliasReusedSlot) {
  SlotMap<int> map;
  const Handle first = map.insert(1);
  map.erase(first);
  const Handle second = map.insert(2);  // reuses the slot
  CHECK_EQ(first.index, second.index);
  CHECK(first != second);
  CHECK(map.get(first) == nullptr);
  REQUIRE(map.get(second) != nullptr);
  CHECK_EQ(*map.get(second), 2);
}

TEST(SlotMap, InvalidHandlesAreRejected) {
  SlotMap<int> map;
  CHECK(map.get(Handle{}) == nullptr);
  CHECK(!map.erase(Handle{}));
  CHECK(map.get(Handle{99, 0}) == nullptr);
}

TEST(SlotMap, ClearInvalidatesAndDestroys) {
  static int alive = 0;
  struct Counted {
    Counted() { ++alive; }
    Counted(const Counted&) { ++alive; }
    Counted(Counted&&) noexcept { ++alive; }
    ~Counted() { --alive; }
  };
  {
    SlotMap<Counted> map;
    std::vector<Handle> hs;
    for (int i = 0; i < 10; ++i) hs.push_back(map.insert(Counted{}));
    CHECK_EQ(alive, 10);
    map.clear();
    CHECK_EQ(alive, 0);
    for (Handle h : hs) CHECK(!map.contains(h));
    const Handle again = map.insert(Counted{});
    CHECK(map.contains(again));
  }
  CHECK_EQ(alive, 0);
}

TEST(SlotMap, ForEachVisitsLiveElements) {
  SlotMap<int> map;
  const Handle a = map.insert(1);
  map.insert(2);
  map.insert(3);
  map.erase(a);
  int sum = 0;
  map.forEach([&](Handle, int& v) { sum += v; });
  CHECK_EQ(sum, 5);
}

TEST(ScopeGuard, RunsOnExitUnlessDismissed) {
  int count = 0;
  {
    ScopeGuard g([&] { ++count; });
  }
  CHECK_EQ(count, 1);
  {
    ScopeGuard g([&] { ++count; });
    g.dismiss();
  }
  CHECK_EQ(count, 1);
}
