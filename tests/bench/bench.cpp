// SPDX-License-Identifier: GPL-2.0-only
// mingol-bench: wall-clock micro benchmarks of the pieces a game exercises every frame or at load time.
// Numbers depend entirely on the machine; on a CPU-only host the render figure measures lavapipe.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>

#include "editor_util.hpp"
#include "mingol/package/archive.hpp"
#include "mingol/player/game_app.hpp"
#include "mingol/scripting/script_component.hpp"

using namespace mingol;
using Clock = std::chrono::steady_clock;

namespace {

double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

void archiveBench() {
  std::vector<std::uint8_t> data(16u << 20);
  std::mt19937 rng(1);
  for (auto& b : data) b = static_cast<std::uint8_t>(rng());
  const double mib = static_cast<double>(data.size()) / (1 << 20);
  for (const bool enc : {false, true}) {
    package::ArchiveWriter w;
    (void)w.add("blob.bin", data);
    package::WriteOptions wo;
    if (enc) wo.key = package::ArchiveKey::generate().value();
    auto t0 = Clock::now();
    auto bytes = w.serialize(wo);
    auto t1 = Clock::now();
    auto r = package::ArchiveReader::openMemory(bytes.value(), wo.key);
    auto got = r.value().read("blob.bin");
    auto t2 = Clock::now();
    std::printf("archive %-9s write %7.1f MiB/s   read+verify %7.1f MiB/s%s\n", enc ? "encrypted" : "plain", mib / (ms(t0, t1) / 1000), mib / (ms(t1, t2) / 1000),
                got.ok() && got.value() == data ? "" : "  (MISMATCH)");
  }
}

void scriptBench() {
  test::TempDir dir;
  test::writeText(dir.path / "scripts" / "hero.mins", test::kHeroScript);
  auto d = editor::EditorDocument::create(nullptr, nullptr);
  if (!d.ok() || !d.value()->createProject(dir.path, "Bench").ok()) { std::puts("script bench: setup failed"); return; }
  auto& doc = *d.value();
  constexpr int kObjects = 2000;
  for (int i = 0; i < kObjects; ++i) {
    auto* go = doc.createEmpty("O" + std::to_string(i));
    if (!doc.addScript(go, "Hero").ok()) { std::puts("script bench: addScript failed"); return; }
  }
  if (!doc.play().ok()) { std::puts("script bench: play failed"); return; }
  for (int i = 0; i < 10; ++i) doc.tick(1.0F / 60.0F);
  constexpr int kFrames = 200;
  auto t0 = Clock::now();
  for (int i = 0; i < kFrames; ++i) doc.tick(1.0F / 60.0F);
  auto t1 = Clock::now();
  std::printf("scripts %d objects with onUpdate: %.3f ms/frame (%.0f ns per script call)\n", kObjects, ms(t0, t1) / kFrames, ms(t0, t1) * 1e6 / kFrames / kObjects);
  (void)doc.stop();
}

void renderBench() {
  rhi::DeviceDesc dd;
  auto dev = rhi::createVulkanDevice(dd);
  if (!dev.ok()) { std::printf("render: skipped (%s)\n", dev.error().describe().c_str()); return; }
  auto rnd = renderer::Renderer::create(*dev.value());
  if (!rnd.ok()) { std::puts("render: renderer failed"); return; }
  test::TempDir dir;
  test::writeText(dir.path / "scripts" / "hero.mins", test::kHeroScript);
  auto d = editor::EditorDocument::create(dev.value().get(), rnd.value().get());
  if (!d.ok() || !d.value()->createProject(dir.path, "Bench").ok()) { std::puts("render: setup failed"); return; }
  constexpr int kBoxes = 100;
  for (int i = 0; i < kBoxes; ++i) {
    auto b = d.value()->createPrimitive(editor::PrimitiveKind::Box);
    if (!b.ok()) { std::puts("render: box failed"); return; }
    b.value()->transform().setLocalPosition({static_cast<float>(i % 10) - 4.5F, static_cast<float>(i / 10) - 4.5F, 0});
  }
  rhi::TextureDesc td;
  td.width = 640;
  td.height = 360;
  td.format = rhi::Format::RGBA8Srgb;
  td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc;
  auto tex = dev.value()->createTexture(td);
  if (!tex.ok()) return;
  auto* cam = d.value()->scene().find("Camera");
  if (cam != nullptr) cam->transform().setLocalPosition({0, 0, 14});
  auto frame = [&]() -> bool {
    auto rs = scene::buildRenderScene(d.value()->scene(), 640.0F / 360.0F);
    if (!rs.ok()) return false;
    auto s = rnd.value()->render(rs.value(), *tex.value());
    if (!s.ok()) return false;
    return dev.value()->waitForSerial(s.value()).ok();
  };
  if (!frame()) { std::puts("render: frame failed"); return; }
  constexpr int kFrames = 20;
  auto t0 = Clock::now();
  for (int i = 0; i < kFrames; ++i) if (!frame()) { std::puts("render: frame failed"); return; }
  auto t1 = Clock::now();
  std::printf("render  %d boxes 640x360 on %s: %.2f ms/frame\n", kBoxes, dev.value()->info().name.c_str(), ms(t0, t1) / kFrames);
}

}  // namespace

int main() {
  archiveBench();
  scriptBench();
  renderBench();
  return 0;
}
