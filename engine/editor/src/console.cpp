// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/console.hpp"

#include <algorithm>
#include <cctype>

namespace mingol::editor {

namespace {
bool containsNoCase(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
  return it != hay.end();
}
}  // namespace

ConsoleLog::ConsoleLog(std::size_t capacity) : capacity_(std::max<std::size_t>(capacity, 1)), start_(std::chrono::steady_clock::now()) {}

void ConsoleLog::add(LogLevel level, std::string category, std::string message) {
  std::lock_guard lock(mutex_);
  ConsoleEntry e;
  e.level = level;
  e.category = std::move(category);
  e.message = std::move(message);
  e.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
  e.serial = ++serial_;
  entries_.push_back(std::move(e));
  while (entries_.size() > capacity_) entries_.pop_front();
}

void ConsoleLog::write(const LogRecord& record) {
  add(record.level, std::string(record.category), std::string(record.message));
}

void ConsoleLog::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
}

std::size_t ConsoleLog::size() const {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

std::uint64_t ConsoleLog::totalAdded() const {
  std::lock_guard lock(mutex_);
  return serial_;
}

std::size_t ConsoleLog::count(LogLevel level) const {
  std::lock_guard lock(mutex_);
  return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [&](const ConsoleEntry& e) { return e.level == level; }));
}

std::vector<ConsoleEntry> ConsoleLog::filtered(const ConsoleFilter& f) const {
  std::lock_guard lock(mutex_);
  std::vector<ConsoleEntry> out;
  for (const ConsoleEntry& e : entries_) {
    if (e.level < f.minLevel) continue;
    if (!f.text.empty() && !containsNoCase(e.message, f.text) && !containsNoCase(e.category, f.text)) continue;
    out.push_back(e);
  }
  return out;
}

void ConsoleLog::install(const std::shared_ptr<ConsoleLog>& self) {
  Logger::get().removeSink(self);  // idempotent
  Logger::get().addSink(self);
}
void ConsoleLog::uninstall(const std::shared_ptr<ConsoleLog>& self) { Logger::get().removeSink(self); }

}  // namespace mingol::editor
