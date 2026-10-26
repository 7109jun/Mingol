// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/job_system.hpp"

#include <algorithm>
#include <atomic>
#include <exception>

namespace mingol {

JobSystem::JobSystem(unsigned threadCount) {
  if (threadCount == 0) {
    const unsigned hw = std::thread::hardware_concurrency();
    threadCount = hw > 1 ? hw - 1 : 1;
  }
  workers_.reserve(threadCount);
  for (unsigned i = 0; i < threadCount; ++i) {
    workers_.emplace_back([this]() { workerLoop(); });
  }
}

JobSystem::~JobSystem() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  for (std::thread& t : workers_) {
    t.join();
  }
}

void JobSystem::enqueue(std::function<void()> job) {
  {
    std::lock_guard lock(mutex_);
    queue_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void JobSystem::workerLoop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        return;  // stopping and drained
      }
      job = std::move(queue_.front());
      queue_.pop_front();
      ++active_;
    }
    try {
      job();
    } catch (...) {
      // Jobs submitted via submit() capture exceptions in their future; anything else is dropped
      // rather than terminating the worker.
    }
    {
      std::lock_guard lock(mutex_);
      --active_;
      if (queue_.empty() && active_ == 0) {
        idle_.notify_all();
      }
    }
  }
}

void JobSystem::waitIdle() {
  std::unique_lock lock(mutex_);
  idle_.wait(lock, [this]() { return queue_.empty() && active_ == 0; });
}

void JobSystem::parallelFor(std::size_t begin, std::size_t end, std::size_t grain,
                            const std::function<void(std::size_t, std::size_t)>& body) {
  if (end <= begin) {
    return;
  }
  if (grain == 0) {
    grain = 1;
  }
  const std::size_t total = end - begin;
  const std::size_t chunks = (total + grain - 1) / grain;

  struct State {
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::mutex mutex;
    std::condition_variable cv;
    std::exception_ptr error;
  };
  auto state = std::make_shared<State>();
  // Late helpers never dereference `bodyPtr`: they only run it after claiming a chunk, and the
  // caller does not return before every chunk has been claimed and finished.
  const std::function<void(std::size_t, std::size_t)>* bodyPtr = &body;

  auto work = [state, bodyPtr, begin, end, grain, chunks]() {
    for (;;) {
      const std::size_t chunk = state->next.fetch_add(1);
      if (chunk >= chunks) {
        return;
      }
      const std::size_t b = begin + chunk * grain;
      const std::size_t e = std::min(end, b + grain);
      try {
        (*bodyPtr)(b, e);
      } catch (...) {
        std::lock_guard lock(state->mutex);
        if (!state->error) {
          state->error = std::current_exception();
        }
      }
      if (state->done.fetch_add(1) + 1 == chunks) {
        std::lock_guard lock(state->mutex);
        state->cv.notify_all();
      }
    }
  };

  const std::size_t helpers = std::min<std::size_t>(workers_.size(), chunks - 1);
  for (std::size_t i = 0; i < helpers; ++i) {
    enqueue(work);
  }
  work();  // the calling thread participates

  std::unique_lock lock(state->mutex);
  state->cv.wait(lock, [&]() { return state->done.load() == chunks; });
  if (state->error) {
    std::rethrow_exception(state->error);
  }
}

}  // namespace mingol
