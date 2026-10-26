// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/core/log.hpp"

#include <algorithm>

#include "mingol/core/platform.hpp"

#if defined(MINGOL_PLATFORM_WINDOWS)
#include <io.h>
#define MINGOL_ISATTY(fd) _isatty(fd)
#define MINGOL_FILENO(f) _fileno(f)
#else
#include <unistd.h>
#define MINGOL_ISATTY(fd) isatty(fd)
#define MINGOL_FILENO(f) fileno(f)
#endif

namespace mingol {

const char* toString(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    case LogLevel::Off: return "OFF  ";
  }
  return "?????";
}

std::string formatLogRecord(const LogRecord& record) {
  const auto ms = std::chrono::floor<std::chrono::milliseconds>(record.time);
  return std::format("{:%T} [{}] [{}] {}", ms, toString(record.level), record.category, record.message);
}

// ---- ConsoleSink -----------------------------------------------------------------------------

void ConsoleSink::write(const LogRecord& record) {
  static const bool color = MINGOL_ISATTY(MINGOL_FILENO(stderr)) != 0;
  const std::string line = formatLogRecord(record);
  if (color) {
    const char* code = "0";
    switch (record.level) {
      case LogLevel::Trace: code = "90"; break;
      case LogLevel::Debug: code = "36"; break;
      case LogLevel::Info: code = "0"; break;
      case LogLevel::Warn: code = "33"; break;
      case LogLevel::Error: code = "31"; break;
      case LogLevel::Fatal: code = "1;31"; break;
      case LogLevel::Off: break;
    }
    std::fprintf(stderr, "\x1b[%sm%s\x1b[0m\n", code, line.c_str());
  } else {
    std::fprintf(stderr, "%s\n", line.c_str());
  }
}

void ConsoleSink::flush() { std::fflush(stderr); }

// ---- FileSink --------------------------------------------------------------------------------

FileSink::FileSink(const std::filesystem::path& path, bool append) {
#if defined(MINGOL_PLATFORM_WINDOWS)
  file_ = _wfopen(path.c_str(), append ? L"ab" : L"wb");
#else
  file_ = std::fopen(path.c_str(), append ? "ab" : "wb");
#endif
}

FileSink::~FileSink() {
  if (file_ != nullptr) {
    std::fclose(file_);
  }
}

void FileSink::write(const LogRecord& record) {
  if (file_ == nullptr) {
    return;
  }
  const std::string line = formatLogRecord(record);
  std::fwrite(line.data(), 1, line.size(), file_);
  std::fputc('\n', file_);
}

void FileSink::flush() {
  if (file_ != nullptr) {
    std::fflush(file_);
  }
}

// ---- RingBufferSink --------------------------------------------------------------------------

void RingBufferSink::write(const LogRecord& record) {
  std::string line = formatLogRecord(record);
  std::lock_guard lock(mutex_);
  lines_.push_back(std::move(line));
  while (lines_.size() > capacity_) {
    lines_.pop_front();
  }
}

std::vector<std::string> RingBufferSink::snapshot() const {
  std::lock_guard lock(mutex_);
  return {lines_.begin(), lines_.end()};
}

std::size_t RingBufferSink::count() const {
  std::lock_guard lock(mutex_);
  return lines_.size();
}

// ---- Logger ----------------------------------------------------------------------------------

Logger::Logger() { sinks_.push_back(std::make_shared<ConsoleSink>()); }

Logger& Logger::get() {
  static Logger instance;
  return instance;
}

void Logger::addSink(std::shared_ptr<LogSink> sink) {
  if (!sink) {
    return;
  }
  std::lock_guard lock(mutex_);
  sinks_.push_back(std::move(sink));
}

void Logger::removeSink(const std::shared_ptr<LogSink>& sink) {
  std::lock_guard lock(mutex_);
  sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

void Logger::clearSinks() {
  std::lock_guard lock(mutex_);
  sinks_.clear();
}

void Logger::flush() {
  std::lock_guard lock(mutex_);
  for (auto& sink : sinks_) {
    sink->flush();
  }
}

void Logger::log(LogLevel level, std::string_view category, std::string_view message) {
  if (!enabled(level)) {
    return;
  }
  const LogRecord record{level, category, message, std::chrono::system_clock::now(), std::this_thread::get_id()};
  std::lock_guard lock(mutex_);
  for (auto& sink : sinks_) {
    sink->write(record);
  }
  if (level >= LogLevel::Error) {
    for (auto& sink : sinks_) {
      sink->flush();
    }
  }
}

}  // namespace mingol
