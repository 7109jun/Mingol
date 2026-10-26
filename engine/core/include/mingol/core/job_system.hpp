// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace mingol {

// Fixed-size worker pool. The destructor finishes all queued jobs, then joins the workers.
class JobSystem {
 public:
  // threadCount == 0 selects max(1, hardware_concurrency - 1).
  explicit JobSystem(unsigned threadCount = 0);
  ~JobSystem();

  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  unsigned threadCount() const noexcept { return static_cast<unsigned>(workers_.size()); }

  // Runs f on a worker. Exceptions propagate through the returned future.
  template <class F>
  [[nodiscard]] auto submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
    using R = std::invoke_result_t<std::decay_t<F>>;
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    std::future<R> future = task->get_future();
    enqueue([task]() { (*task)(); });
    return future;
  }

  // Splits [begin, end) into chunks of `grain` and calls body(chunkBegin, chunkEnd) for each,
  // using the workers and the calling thread. Blocks until all chunks are done. Safe to call from
  // inside a job (the caller always makes progress itself). Rethrows the first exception thrown.
  void parallelFor(std::size_t begin, std::size_t end, std::size_t grain,
                   const std::function<void(std::size_t, std::size_t)>& body);

  // Blocks until the queue is empty and no job is running.
  void waitIdle();

 private:
  void enqueue(std::function<void()> job);
  void workerLoop();

  std::vector<std::thread> workers_;
  std::deque<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable idle_;
  std::size_t active_ = 0;
  bool stopping_ = false;
};

}  // namespace mingol
