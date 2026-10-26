// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mingol {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal, Off };

const char* toString(LogLevel level) noexcept;

struct LogRecord {
  LogLevel level;
  std::string_view category;
  std::string_view message;
  std::chrono::system_clock::time_point time;
  std::thread::id thread;
};

// "12:34:56.789 [INFO ] [category] message" (UTC)
std::string formatLogRecord(const LogRecord& record);

class LogSink {
 public:
  virtual ~LogSink() = default;
  // Called with the logger mutex held; sinks need no locking of their own.
  virtual void write(const LogRecord& record) = 0;
  virtual void flush() {}
};

class ConsoleSink final : public LogSink {
 public:
  void write(const LogRecord& record) override;
  void flush() override;
};

class FileSink final : public LogSink {
 public:
  explicit FileSink(const std::filesystem::path& path, bool append = true);
  ~FileSink() override;
  FileSink(const FileSink&) = delete;
  FileSink& operator=(const FileSink&) = delete;

  bool isOpen() const noexcept { return file_ != nullptr; }
  void write(const LogRecord& record) override;
  void flush() override;

 private:
  std::FILE* file_ = nullptr;
};

// Keeps the most recent formatted lines in memory (used by the editor console and tests).
class RingBufferSink final : public LogSink {
 public:
  explicit RingBufferSink(std::size_t capacity) : capacity_(capacity) {}

  void write(const LogRecord& record) override;
  // Thread-safe snapshot copy (sinks are written under the logger lock, reads take their own).
  std::vector<std::string> snapshot() const;
  std::size_t count() const;

 private:
  mutable std::mutex mutex_;
  std::deque<std::string> lines_;
  std::size_t capacity_;
};

class Logger {
 public:
  // Process-wide logger. Starts with a ConsoleSink and level Info.
  static Logger& get();

  void setLevel(LogLevel level) noexcept { level_.store(level, std::memory_order_relaxed); }
  LogLevel level() const noexcept { return level_.load(std::memory_order_relaxed); }
  bool enabled(LogLevel level) const noexcept { return level >= this->level(); }

  void addSink(std::shared_ptr<LogSink> sink);
  void removeSink(const std::shared_ptr<LogSink>& sink);
  void clearSinks();
  void flush();

  void log(LogLevel level, std::string_view category, std::string_view message);

  template <class... Args>
  void logf(LogLevel level, std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
    log(level, category, std::format(fmt, std::forward<Args>(args)...));
  }

 private:
  Logger();

  std::mutex mutex_;
  std::vector<std::shared_ptr<LogSink>> sinks_;
  std::atomic<LogLevel> level_{LogLevel::Info};
};

}  // namespace mingol

#define MINGOL_LOG(level, category, ...)                                           \
  do {                                                                             \
    if (::mingol::Logger::get().enabled(level)) {                                  \
      ::mingol::Logger::get().logf((level), (category), __VA_ARGS__);              \
    }                                                                              \
  } while (false)

#define MINGOL_LOG_TRACE(category, ...) MINGOL_LOG(::mingol::LogLevel::Trace, category, __VA_ARGS__)
#define MINGOL_LOG_DEBUG(category, ...) MINGOL_LOG(::mingol::LogLevel::Debug, category, __VA_ARGS__)
#define MINGOL_LOG_INFO(category, ...) MINGOL_LOG(::mingol::LogLevel::Info, category, __VA_ARGS__)
#define MINGOL_LOG_WARN(category, ...) MINGOL_LOG(::mingol::LogLevel::Warn, category, __VA_ARGS__)
#define MINGOL_LOG_ERROR(category, ...) MINGOL_LOG(::mingol::LogLevel::Error, category, __VA_ARGS__)
#define MINGOL_LOG_FATAL(category, ...) MINGOL_LOG(::mingol::LogLevel::Fatal, category, __VA_ARGS__)
