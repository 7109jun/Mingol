// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "mingol/core/assert.hpp"

namespace mingol {

// Bump allocator over one block. Not thread-safe. Only trivially destructible types may be created.
class LinearAllocator {
 public:
  explicit LinearAllocator(std::size_t capacityBytes)
      : buffer_(new std::byte[capacityBytes]), capacity_(capacityBytes) {}

  LinearAllocator(const LinearAllocator&) = delete;
  LinearAllocator& operator=(const LinearAllocator&) = delete;

  // Returns nullptr when the request does not fit. `alignment` must be a power of two.
  void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept {
    MINGOL_VERIFY(alignment != 0 && (alignment & (alignment - 1)) == 0);
    const auto base = reinterpret_cast<std::uintptr_t>(buffer_.get());
    const std::uintptr_t cursor = base + offset_;
    const std::uintptr_t aligned = (cursor + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
    const std::size_t newOffset = static_cast<std::size_t>(aligned - base);
    if (newOffset > capacity_ || size > capacity_ - newOffset) {
      return nullptr;
    }
    offset_ = newOffset + size;
    return reinterpret_cast<void*>(aligned);
  }

  template <class T, class... Args>
  T* create(Args&&... args) {
    static_assert(std::is_trivially_destructible_v<T>, "LinearAllocator never runs destructors");
    void* p = allocate(sizeof(T), alignof(T));
    return p != nullptr ? new (p) T(std::forward<Args>(args)...) : nullptr;
  }

  void reset() noexcept { offset_ = 0; }
  std::size_t used() const noexcept { return offset_; }
  std::size_t capacity() const noexcept { return capacity_; }

 private:
  std::unique_ptr<std::byte[]> buffer_;
  std::size_t capacity_;
  std::size_t offset_ = 0;
};

// Generational handle: becomes invalid (never aliases a new object) once its slot is erased.
struct Handle {
  static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t index = kInvalidIndex;
  std::uint32_t generation = 0;

  constexpr bool valid() const noexcept { return index != kInvalidIndex; }
  friend constexpr bool operator==(Handle a, Handle b) noexcept {
    return a.index == b.index && a.generation == b.generation;
  }
  friend constexpr bool operator!=(Handle a, Handle b) noexcept { return !(a == b); }
};

// Owns objects and refers to them by Handle. Pointers from get() are invalidated by insert()
// (storage may grow) and by erase() of that element; Handles are not.
template <class T>
class SlotMap {
 public:
  Handle insert(T value) {
    if (freeHead_ != kNone) {
      const std::uint32_t index = freeHead_;
      Slot& slot = slots_[index];
      freeHead_ = slot.nextFree;
      slot.value.emplace(std::move(value));
      ++count_;
      return {index, slot.generation};
    }
    MINGOL_VERIFY(slots_.size() < Handle::kInvalidIndex);
    slots_.emplace_back();
    Slot& slot = slots_.back();
    slot.value.emplace(std::move(value));
    ++count_;
    return {static_cast<std::uint32_t>(slots_.size() - 1), slot.generation};
  }

  bool erase(Handle h) {
    Slot* slot = find(h);
    if (slot == nullptr) {
      return false;
    }
    slot->value.reset();
    ++slot->generation;
    slot->nextFree = freeHead_;
    freeHead_ = h.index;
    --count_;
    return true;
  }

  T* get(Handle h) noexcept {
    Slot* slot = find(h);
    return slot != nullptr ? &*slot->value : nullptr;
  }
  const T* get(Handle h) const noexcept {
    const Slot* slot = find(h);
    return slot != nullptr ? &*slot->value : nullptr;
  }

  bool contains(Handle h) const noexcept { return find(h) != nullptr; }
  std::size_t size() const noexcept { return count_; }

  // Destroys all elements and invalidates every outstanding handle.
  void clear() {
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
      Slot& slot = slots_[i];
      if (slot.value.has_value()) {
        slot.value.reset();
        ++slot.generation;
      }
      slot.nextFree = (i + 1 < slots_.size()) ? i + 1 : kNone;
    }
    freeHead_ = slots_.empty() ? kNone : 0;
    count_ = 0;
  }

  // f(Handle, T&); do not insert or erase from inside f.
  template <class F>
  void forEach(F&& f) {
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
      if (slots_[i].value.has_value()) {
        f(Handle{i, slots_[i].generation}, *slots_[i].value);
      }
    }
  }

 private:
  static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

  struct Slot {
    std::optional<T> value;
    std::uint32_t generation = 0;
    std::uint32_t nextFree = kNone;
  };

  Slot* find(Handle h) noexcept {
    if (h.index >= slots_.size()) return nullptr;
    Slot& slot = slots_[h.index];
    return (slot.value.has_value() && slot.generation == h.generation) ? &slot : nullptr;
  }
  const Slot* find(Handle h) const noexcept {
    if (h.index >= slots_.size()) return nullptr;
    const Slot& slot = slots_[h.index];
    return (slot.value.has_value() && slot.generation == h.generation) ? &slot : nullptr;
  }

  std::vector<Slot> slots_;
  std::uint32_t freeHead_ = kNone;
  std::size_t count_ = 0;
};

// Runs a callable on scope exit unless dismissed.
template <class F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F f) : f_(std::move(f)) {}
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& o) noexcept(std::is_nothrow_move_constructible_v<F>)
      : f_(std::move(o.f_)), active_(std::exchange(o.active_, false)) {}
  ~ScopeGuard() {
    if (active_) f_();
  }
  void dismiss() noexcept { active_ = false; }

 private:
  F f_;
  bool active_ = true;
};

}  // namespace mingol
