// SPDX-License-Identifier: GPL-2.0-only
// Console model: captures engine log records (and script Log output, which goes through the same
// Logger) into a bounded, filterable list. Thread safe: records may come from any thread.
#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mingol/core/log.hpp"

namespace mingol::editor {

struct ConsoleEntry {
  LogLevel level = LogLevel::Info;
  std::string category;
  std::string message;
  double seconds = 0;  // since the console was created
  std::uint64_t serial = 0;
};

struct ConsoleFilter {
  LogLevel minLevel = LogLevel::Trace;
  std::string text;  // case-insensitive substring of category or message; empty = all
};

class ConsoleLog final : public LogSink {
 public:
  explicit ConsoleLog(std::size_t capacity = 5000);

  void write(const LogRecord& record) override;
  // Adds an entry directly (editor messages, script diagnostics).
  void add(LogLevel level, std::string category, std::string message);
  void clear();

  std::size_t size() const;
  std::uint64_t totalAdded() const;  // monotonically increasing; lets the UI detect new entries
  std::size_t count(LogLevel level) const;  // entries currently held at exactly `level`
  std::vector<ConsoleEntry> filtered(const ConsoleFilter& filter) const;

  // Registers this sink with the global Logger (idempotent). The destructor unregisters.
  void install(const std::shared_ptr<ConsoleLog>& self);
  static void uninstall(const std::shared_ptr<ConsoleLog>& self);

 private:
  mutable std::mutex mutex_;
  std::deque<ConsoleEntry> entries_;
  std::size_t capacity_;
  std::uint64_t serial_ = 0;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace mingol::editor
