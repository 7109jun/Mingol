// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "mingol/editor/document.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

// A fresh scratch directory removed at scope end.
struct TempDir {
  std::filesystem::path path;
  TempDir() {
    static std::atomic<int> counter{0};
    path = std::filesystem::temp_directory_path() / ("mingol_editor_test_" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

inline void writeText(const std::filesystem::path& p, const std::string& text) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << text;
}

inline const char* kHeroScript = R"(
class Hero extends Script {
  public int hp = 10;
  public float speed = 2.5;
  public int ticks = 0;
  override void onUpdate(float dt) { ticks++; }
}
)";

}  // namespace mingol::test
