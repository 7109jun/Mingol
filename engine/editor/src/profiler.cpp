// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/editor/profiler.hpp"

#include <algorithm>
#include <numeric>

namespace mingol::editor {

namespace {
void push(std::vector<float>& h, float v) {
  h.push_back(v);
  if (h.size() > Profiler::kHistory) h.erase(h.begin());
}
}  // namespace

Profiler::Scope& Profiler::scope(std::string_view name) {
  for (Scope& s : scopes_)
    if (s.name == name) return s;
  scopes_.push_back({std::string(name), 0, {}});
  return scopes_.back();
}

void Profiler::beginFrame() {
  frameStart_ = std::chrono::steady_clock::now();
  inFrame_ = true;
}

void Profiler::endFrame() {
  if (!inFrame_) return;
  inFrame_ = false;
  frameMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart_).count();
  push(frameHistory_, static_cast<float>(frameMs_));
  for (Scope& s : scopes_) {
    push(s.history, static_cast<float>(s.current));
    s.current = 0;
  }
  ++frames_;
}

void Profiler::add(std::string_view name, double ms) { scope(name).current += ms; }

void Profiler::setCounter(std::string_view name, double value) {
  for (Counter& c : counters_)
    if (c.name == name) {
      c.value = value;
      return;
    }
  counters_.push_back({std::string(name), value});
}

double Profiler::counter(std::string_view name) const {
  for (const Counter& c : counters_)
    if (c.name == name) return c.value;
  return 0;
}

double Profiler::fps() const {
  if (frameHistory_.empty()) return 0;
  const double avg = std::accumulate(frameHistory_.begin(), frameHistory_.end(), 0.0) / static_cast<double>(frameHistory_.size());
  return avg > 0 ? 1000.0 / avg : 0;
}

std::vector<ProfilerScopeStats> Profiler::scopes() const {
  std::vector<ProfilerScopeStats> out;
  for (const Scope& s : scopes_) {
    ProfilerScopeStats st;
    st.name = s.name;
    if (!s.history.empty()) {
      st.lastMs = s.history.back();
      st.avgMs = std::accumulate(s.history.begin(), s.history.end(), 0.0) / static_cast<double>(s.history.size());
      st.maxMs = *std::max_element(s.history.begin(), s.history.end());
    }
    out.push_back(std::move(st));
  }
  return out;
}

const std::vector<float>* Profiler::scopeHistory(std::string_view name) const {
  for (const Scope& s : scopes_)
    if (s.name == name) return &s.history;
  return nullptr;
}

}  // namespace mingol::editor
