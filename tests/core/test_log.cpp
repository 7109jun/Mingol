// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#include "mingol/core/fs.hpp"
#include "mingol/core/log.hpp"
#include "mingol_test.hpp"

using namespace mingol;

namespace {

// Replaces the global logger configuration for one test and restores it afterwards.
struct LoggerScope {
  LogLevel previous = Logger::get().level();
  LoggerScope() { Logger::get().clearSinks(); }
  ~LoggerScope() {
    Logger::get().clearSinks();
    Logger::get().addSink(std::make_shared<ConsoleSink>());
    Logger::get().setLevel(previous);
  }
};

}  // namespace

TEST(Log, LevelFilteringAndFormatting) {
  LoggerScope scope;
  auto ring = std::make_shared<RingBufferSink>(16);
  Logger::get().addSink(ring);
  Logger::get().setLevel(LogLevel::Warn);

  MINGOL_LOG_INFO("test", "filtered out {}", 1);
  MINGOL_LOG_WARN("test", "kept {} {}", "a", 2);
  MINGOL_LOG_ERROR("core", "boom");

  const auto lines = ring->snapshot();
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].find("[WARN ] [test] kept a 2") != std::string::npos);
  CHECK(lines[1].find("[ERROR] [core] boom") != std::string::npos);
}

TEST(Log, RingBufferKeepsNewest) {
  LoggerScope scope;
  auto ring = std::make_shared<RingBufferSink>(3);
  Logger::get().addSink(ring);
  Logger::get().setLevel(LogLevel::Trace);
  for (int i = 0; i < 10; ++i) MINGOL_LOG_DEBUG("t", "line {}", i);
  const auto lines = ring->snapshot();
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].find("line 7") != std::string::npos);
  CHECK(lines[2].find("line 9") != std::string::npos);
}

TEST(Log, ArgumentsNotEvaluatedWhenFiltered) {
  LoggerScope scope;
  Logger::get().setLevel(LogLevel::Error);
  int evaluated = 0;
  auto expensive = [&] {
    ++evaluated;
    return 1;
  };
  MINGOL_LOG_DEBUG("t", "{}", expensive());
  CHECK_EQ(evaluated, 0);
}

TEST(Log, FileSinkWritesLines) {
  const auto path = std::filesystem::temp_directory_path() / "mingol_log_test.txt";
  std::filesystem::remove(path);
  {
    LoggerScope scope;
    auto file = std::make_shared<FileSink>(path, false);
    REQUIRE(file->isOpen());
    Logger::get().addSink(file);
    Logger::get().setLevel(LogLevel::Info);
    MINGOL_LOG_INFO("io", "hello file");
    Logger::get().flush();
    Logger::get().clearSinks();
  }
  auto text = fs::readTextFile(path);
  REQUIRE(text.ok());
  CHECK(text.value().find("[INFO ] [io] hello file") != std::string::npos);
  std::filesystem::remove(path);
}

TEST(Log, FileSinkReportsOpenFailure) {
  FileSink bad(std::filesystem::temp_directory_path() / "no_such_dir_mingol" / "x.log");
  CHECK(!bad.isOpen());
  LogRecord rec{LogLevel::Info, "t", "m", std::chrono::system_clock::now(), std::this_thread::get_id()};
  bad.write(rec);  // must be a safe no-op
}

TEST(Log, ConcurrentLoggingKeepsEveryLine) {
  LoggerScope scope;
  auto ring = std::make_shared<RingBufferSink>(100000);
  Logger::get().addSink(ring);
  Logger::get().setLevel(LogLevel::Info);

  constexpr int kThreads = 4, kPerThread = 500;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t] {
      for (int i = 0; i < kPerThread; ++i) MINGOL_LOG_INFO("mt", "thread {} msg {}", t, i);
    });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(ring->count(), static_cast<std::size_t>(kThreads * kPerThread));
}
