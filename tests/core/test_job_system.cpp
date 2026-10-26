// SPDX-License-Identifier: GPL-2.0-only
#include <atomic>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "mingol/core/job_system.hpp"
#include "mingol_test.hpp"

using namespace mingol;

TEST(JobSystem, SubmitReturnsResult) {
  JobSystem jobs(2);
  auto f = jobs.submit([] { return 6 * 7; });
  CHECK_EQ(f.get(), 42);
}

TEST(JobSystem, ExceptionsPropagateThroughFuture) {
  JobSystem jobs(2);
  auto f = jobs.submit([]() -> int { throw std::runtime_error("job failed"); });
  bool caught = false;
  try {
    f.get();
  } catch (const std::runtime_error& e) {
    caught = std::string(e.what()) == "job failed";
  }
  CHECK(caught);
}

TEST(JobSystem, ManyJobsAllRunAndWaitIdle) {
  JobSystem jobs(4);
  std::atomic<int> counter{0};
  std::vector<std::future<void>> futures;
  for (int i = 0; i < 1000; ++i) futures.push_back(jobs.submit([&] { counter.fetch_add(1); }));
  jobs.waitIdle();
  CHECK_EQ(counter.load(), 1000);
  for (auto& f : futures) f.get();
}

TEST(JobSystem, ParallelForCoversRangeExactlyOnce) {
  JobSystem jobs(3);
  std::vector<std::atomic<int>> hits(10007);
  jobs.parallelFor(0, hits.size(), 64, [&](std::size_t b, std::size_t e) {
    for (std::size_t i = b; i < e; ++i) hits[i].fetch_add(1);
  });
  bool allOnce = true;
  for (auto& h : hits) allOnce = allOnce && h.load() == 1;
  CHECK(allOnce);
}

TEST(JobSystem, ParallelForEmptyAndZeroGrain) {
  JobSystem jobs(2);
  int calls = 0;
  jobs.parallelFor(5, 5, 8, [&](std::size_t, std::size_t) { ++calls; });
  CHECK_EQ(calls, 0);
  std::atomic<int> sum{0};
  jobs.parallelFor(0, 10, 0, [&](std::size_t b, std::size_t e) { sum.fetch_add(static_cast<int>(e - b)); });
  CHECK_EQ(sum.load(), 10);
}

TEST(JobSystem, ParallelForRethrowsFirstException) {
  JobSystem jobs(2);
  bool caught = false;
  try {
    jobs.parallelFor(0, 100, 10, [](std::size_t b, std::size_t) {
      if (b == 30) throw std::runtime_error("chunk failed");
    });
  } catch (const std::runtime_error&) {
    caught = true;
  }
  CHECK(caught);
  // The pool must remain usable after an exception.
  CHECK_EQ(jobs.submit([] { return 1; }).get(), 1);
}

TEST(JobSystem, NestedParallelForDoesNotDeadlockOnSingleWorker) {
  JobSystem jobs(1);
  std::atomic<int> total{0};
  jobs.parallelFor(0, 8, 1, [&](std::size_t, std::size_t) {
    jobs.parallelFor(0, 8, 1, [&](std::size_t b, std::size_t e) { total.fetch_add(static_cast<int>(e - b)); });
  });
  CHECK_EQ(total.load(), 64);
}

TEST(JobSystem, DestructorDrainsQueue) {
  std::atomic<int> counter{0};
  {
    JobSystem jobs(2);
    for (int i = 0; i < 200; ++i) {
      auto f = jobs.submit([&] { counter.fetch_add(1); });
      (void)f;
    }
  }
  CHECK_EQ(counter.load(), 200);
}
