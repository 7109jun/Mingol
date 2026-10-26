// SPDX-License-Identifier: GPL-2.0-only
// Frame profiler: named CPU scopes accumulated per frame into ring buffers for the UI graphs.
// Single-threaded (call from the thread that runs the editor loop).
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mingol::editor {

struct ProfilerScopeStats {
  std::string name;
  double lastMs = 0;
  double avgMs = 0;  // over the history window
  double maxMs = 0;
};

class Profiler {
 public:
  static constexpr std::size_t kHistory = 240;

  void beginFrame();
  void endFrame();  // closes the frame; pushes frame time and every scope total into the history

  // Adds time to a named scope for the current frame (also used by ScopedTimer).
  void add(std::string_view scope, double ms);
  void setCounter(std::string_view name, double value);  // arbitrary per-frame numbers (draw calls, objects)

  struct Scoped {
    Scoped(Profiler& p, std::string_view name) : p_(p), name_(name), start_(std::chrono::steady_clock::now()) {}
    ~Scoped() { p_.add(name_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count()); }
    Scoped(const Scoped&) = delete;
    Scoped& operator=(const Scoped&) = delete;

   private:
    Profiler& p_;
    std::string name_;
    std::chrono::steady_clock::time_point start_;
  };

  double frameMs() const { return frameMs_; }
  double fps() const;  // from the average frame time
  const std::vector<float>& frameHistory() const { return frameHistory_; }  // oldest first, up to kHistory
  std::vector<ProfilerScopeStats> scopes() const;
  const std::vector<float>* scopeHistory(std::string_view name) const;
  double counter(std::string_view name) const;
  std::uint64_t frames() const { return frames_; }

 private:
  struct Scope {
    std::string name;
    double current = 0;
    std::vector<float> history;
  };
  struct Counter {
    std::string name;
    double value = 0;
  };
  Scope& scope(std::string_view name);

  std::chrono::steady_clock::time_point frameStart_{};
  bool inFrame_ = false;
  double frameMs_ = 0;
  std::vector<float> frameHistory_;
  std::vector<Scope> scopes_;
  std::vector<Counter> counters_;
  std::uint64_t frames_ = 0;
};

}  // namespace mingol::editor
