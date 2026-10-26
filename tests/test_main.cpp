// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>
#include <cstring>
#include <exception>

#include "mingol_test.hpp"

namespace mingol::test {

std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

namespace {
int gCurrentFailures = 0;
bool gCurrentSkipped = false;
std::string gSkipReason;
}  // namespace

void skip(const std::string& reason) {
  gCurrentSkipped = true;
  gSkipReason = reason;
}

void fail(const char* file, int line, const std::string& message) {
  ++gCurrentFailures;
  std::fprintf(stderr, "    %s:%d: %s\n", file, line, message.c_str());
}

}  // namespace mingol::test

// Usage: <test-exe> [--list] [substring-filter]
int main(int argc, char** argv) {
  using namespace mingol::test;
  const char* filter = nullptr;
  bool listOnly = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--list") == 0) {
      listOnly = true;
    } else {
      filter = argv[i];
    }
  }

  int ran = 0;
  int failed = 0;
  int skipped = 0;
  const auto start = std::chrono::steady_clock::now();
  for (const Case& c : registry()) {
    const std::string full = std::string(c.suite) + "." + c.name;
    if (filter != nullptr && full.find(filter) == std::string::npos) continue;
    if (listOnly) {
      std::printf("%s\n", full.c_str());
      continue;
    }
    gCurrentFailures = 0;
    gCurrentSkipped = false;
    try {
      c.fn();
    } catch (const std::exception& e) {
      fail(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
    } catch (...) {
      fail(__FILE__, __LINE__, "uncaught non-standard exception");
    }
    ++ran;
    if (gCurrentFailures == 0 && gCurrentSkipped) {
      std::printf("[SKIP] %s (%s)\n", full.c_str(), gSkipReason.c_str());
      ++skipped;
    } else if (gCurrentFailures == 0) {
      std::printf("[ OK ] %s\n", full.c_str());
    } else {
      std::printf("[FAIL] %s (%d failed checks)\n", full.c_str(), gCurrentFailures);
      ++failed;
    }
    std::fflush(stdout);
  }
  if (listOnly) return 0;

  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::printf("\n%d test(s) run, %d failed, %d skipped (%.1f ms)\n", ran, failed, skipped, ms);
  std::fflush(stdout);
  if (ran == 0) {
    std::printf("no tests matched\n");
    return 2;
  }
  return failed == 0 ? 0 : 1;
}
