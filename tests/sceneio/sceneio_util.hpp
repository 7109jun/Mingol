// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <memory>
#include <string>

#include "mingol/runtime/audio_components.hpp"
#include "mingol/runtime/physics_components.hpp"
#include "mingol/scene/components.hpp"
#include "mingol/sceneio/scene_io.hpp"
#include "mingol/scripting/script_component.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

inline std::shared_ptr<const renderer::Skeleton> testSkeleton() {
  std::vector<renderer::Joint> j(2);
  j[0].name = "root";
  j[1].name = "tip";
  j[1].parent = 0;
  j[1].bindLocal.translation = {0, 1, 0};
  auto s = renderer::Skeleton::create(std::move(j));
  return std::make_shared<renderer::Skeleton>(std::move(s).value());
}
inline std::shared_ptr<const renderer::AnimationClip> testClip(const char* name, float x) {
  renderer::Channel ch;
  ch.joint = 0;
  ch.translation.times = {0.0F, 1.0F};
  ch.translation.values = {{0, 0, 0}, {x, 0, 0}};
  auto c = renderer::AnimationClip::create(name, 1.0F, {ch});
  return std::make_shared<renderer::AnimationClip>(std::move(c).value());
}

// Describes the first differing line of two texts ("" when equal).
inline std::string firstDiff(const std::string& a, const std::string& b) {
  std::size_t i = 0, line = 1, ls = 0;
  while (i < a.size() && i < b.size() && a[i] == b[i]) { if (a[i] == '\n') { ++line; ls = i + 1; } ++i; }
  if (i == a.size() && i == b.size()) return {};
  auto le = [&](const std::string& t) { const auto e = t.find('\n', ls); return t.substr(ls, e == std::string::npos ? e : e - ls); };
  return "line " + std::to_string(line) + ": '" + le(a) + "' vs '" + le(b) + "'";
}

// Counts of everything a failed load must not leak.
struct Census {
  std::int64_t objects = scene::GameObject::liveCount();
  std::int64_t components = scene::Component::liveCount();
  bool operator==(const Census&) const = default;
};

}  // namespace mingol::test
