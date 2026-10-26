// SPDX-License-Identifier: GPL-2.0-only
// GPU backend vs. software rasterizer. Needs a Vulkan device (lavapipe is enough); skips otherwise.
#include <cmath>
#include <cstdlib>

#include "gui_util.hpp"
#include "mingol/gui/dock.hpp"
#include "mingol/gui/gui_renderer.hpp"
#include "mingol_test.hpp"

using namespace guitest;
namespace rhi = mingol::rhi;

namespace {
struct GpuEnv {
  std::unique_ptr<rhi::Device> dev;
  std::string skip;
};
GpuEnv& env() {
  static GpuEnv e = [] {
    GpuEnv r;
    rhi::DeviceDesc dd;
    dd.enableValidation = true;
    auto d = rhi::createVulkanDevice(dd);
    if (!d.ok()) r.skip = "no usable Vulkan device: " + d.error().describe();
    else r.dev = std::move(d).value();
    return r;
  }();
  return e;
}

std::shared_ptr<rhi::Texture> makeTarget(rhi::Device& dev, std::uint32_t w, std::uint32_t h, rhi::Format f = rhi::Format::RGBA8Unorm) {
  rhi::TextureDesc t;
  t.width = w;
  t.height = h;
  t.format = f;
  t.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled;
  auto r = dev.createTexture(t);
  if (!r.ok()) {
    mingol::test::fail(__FILE__, __LINE__, r.error().describe());
    return nullptr;
  }
  return std::move(r).value();
}

Image readback(rhi::Device& dev, rhi::Texture& t) {
  auto bytes = dev.readbackTexture(t);
  Image img(t.desc().width, t.desc().height);
  if (!bytes.ok()) {
    mingol::test::fail(__FILE__, __LINE__, bytes.error().describe());
    return img;
  }
  img.rgba = bytes.value();
  return img;
}

// Counts pixels whose RGB differs by more than `tol` in any channel.
int diffPixels(const Image& a, const Image& b, int tol, int* maxDelta = nullptr) {
  int n = 0, md = 0;
  for (std::uint32_t y = 0; y < a.height; ++y)
    for (std::uint32_t x = 0; x < a.width; ++x) {
      const Color p = a.at(x, y), q = b.at(x, y);
      const int d = std::max({std::abs(p.r - q.r), std::abs(p.g - q.g), std::abs(p.b - q.b)});
      md = std::max(md, d);
      if (d > tol) ++n;
    }
  if (maxDelta != nullptr) *maxDelta = md;
  return n;
}
}  // namespace

#define GPU_OR_SKIP()                                 \
  if (!env().dev) SKIP_TEST(env().skip);              \
  rhi::Device& dev = *env().dev;                      \
  [[maybe_unused]] const std::uint64_t validationBase = dev.validationErrorCount()

#define CHECK_NO_VALIDATION_ERRORS() CHECK_EQ(dev.validationErrorCount(), validationBase)

namespace {
void buildUi(Harness& h, mingol::gui::DockLayout& dock) {
  float pos[3] = {1, 2.5F, -3};
  static std::string name = "Cube";
  static bool b = true;
  static int sel = 1;
  const std::vector<std::string> items = {"Opaque", "Masked", "Blend"};
  h.ctx.beginMainMenuBar({0, 0, 640, 24});
  h.ctx.beginMenu("File");
  h.ctx.endMainMenuBar();
  auto slots = mingol::gui::dockSpace(h.ctx, dock, {0, 24, 640, 480});
  for (const auto& s : slots) {
    h.ctx.beginPanel(s.name, s.content);
    h.ctx.inputText("Name", name);
    h.ctx.dragFloat3("Position", pos);
    h.ctx.combo("Mode", sel, items);
    h.ctx.checkbox("Cast shadows", b);
    h.ctx.progressBar(0.4F, "40%");
    if (h.ctx.treeNode("Root", mingol::gui::TreeFlags::DefaultOpen)) {
      h.ctx.treeNode("Leaf", mingol::gui::TreeFlags::Leaf | mingol::gui::TreeFlags::Selected);
      h.ctx.treePop();
    }
    h.ctx.endPanel();
  }
}
}  // namespace

TEST(GuiGpu, FullUiMatchesSoftwareRasterizer) {
  GPU_OR_SKIP();
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 2});
  REQUIRE(gr.ok());
  Harness h(2);
  h.size = {640, 480};
  mingol::gui::DockLayout dock;
  dock.addPanel("Inspector");
  (void)dock.dockRelative("Hierarchy", "Inspector", mingol::gui::DockZone::Left, 0.4F);
  for (int i = 0; i < 3; ++i) h.frame([&] { buildUi(h, dock); });
  CHECK(h.ctx.errors().empty());

  const float clear[4] = {20 / 255.0F, 20 / 255.0F, 22 / 255.0F, 1};
  auto target = makeTarget(dev, 640, 480);
  REQUIRE(target != nullptr);
  auto serial = gr.value()->render(h.ctx.drawList(), *target, clear);
  REQUIRE(serial.ok());
  const Image gpu = readback(dev, *target);
  Image cpu(640, 480, {20, 20, 22, 255});
  mingol::gui::rasterize(h.ctx.drawList(), cpu);
  int maxDelta = 0;
  const int bad = diffPixels(gpu, cpu, 3, &maxDelta);
  const int total = 640 * 480;
  // Edge pixels of rounded shapes may differ (different rasterisation rules for diagonals);
  // everything axis-aligned (text, panels, frames) must match exactly.
  CHECK_MSG(bad * 1000 < total * 3, "differing pixels: " + std::to_string(bad) + " of " + std::to_string(total) + ", max delta " + std::to_string(maxDelta));
  if (const char* dir = std::getenv("MINGOL_GUI_SCREENSHOT_DIR")) {
    mingol::gui::writePpm(gpu, (std::string(dir) + "/gui_gpu.ppm").c_str());
    mingol::gui::writePpm(cpu, (std::string(dir) + "/gui_cpu.ppm").c_str());
  }
  CHECK_NO_VALIDATION_ERRORS();
}

TEST(GuiGpu, AxisAlignedRectsAndTextMatchExactly) {
  GPU_OR_SKIP();
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 2});
  REQUIRE(gr.ok());
  mingol::gui::DrawList l;
  l.addRect({3, 3, 40, 20}, {200, 40, 40});
  l.addRect({10, 10, 60, 30}, {40, 200, 40, 128});
  l.pushClip({20, 5, 50, 25});
  l.addRect({0, 0, 100, 100}, {0, 0, 255, 90});
  l.popClip();
  l.addText({4, 40}, "Hello, GPU! 0123", {250, 250, 250}, 1);
  l.addText({4, 52}, "Scale two", {250, 220, 100}, 2);
  auto target = makeTarget(dev, 128, 80);
  REQUIRE(target != nullptr);
  const float clear[4] = {0, 0, 0, 1};
  REQUIRE(gr.value()->render(l, *target, clear).ok());
  const Image gpu = readback(dev, *target);
  Image cpu(128, 80, {0, 0, 0, 255});
  mingol::gui::rasterize(l, cpu);
  int maxDelta = 0;
  const int bad = diffPixels(gpu, cpu, 2, &maxDelta);
  CHECK_MSG(bad == 0, "differing pixels: " + std::to_string(bad) + ", max delta " + std::to_string(maxDelta));
  CHECK_NO_VALIDATION_ERRORS();
}

TEST(GuiGpu, LoadKeepsPreviousContentAndEmptyListIsFine) {
  GPU_OR_SKIP();
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 2});
  REQUIRE(gr.ok());
  auto target = makeTarget(dev, 32, 32);
  REQUIRE(target != nullptr);
  const float red[4] = {1, 0, 0, 1};
  mingol::gui::DrawList empty;
  REQUIRE(gr.value()->render(empty, *target, red).ok());
  mingol::gui::DrawList l;
  l.addRect({8, 8, 16, 16}, {0, 255, 0});
  REQUIRE(gr.value()->render(l, *target, nullptr).ok());  // load: red stays outside the rect
  const Image img = readback(dev, *target);
  CHECK(img.at(0, 0) == (Color{255, 0, 0, 255}));
  CHECK(img.at(10, 10) == (Color{0, 255, 0, 255}));
  CHECK_NO_VALIDATION_ERRORS();
}

TEST(GuiGpu, UserTextureAndUnknownTextureSkipped) {
  GPU_OR_SKIP();
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 2});
  REQUIRE(gr.ok());
  rhi::TextureDesc td;
  td.width = 2;
  td.height = 1;
  td.format = rhi::Format::RGBA8Unorm;
  td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
  const std::uint8_t px[8] = {255, 0, 0, 255, 0, 0, 255, 255};
  auto tex = dev.createTextureWithData(td, px, false);
  REQUIRE(tex.ok());
  CHECK(!gr.value()->setTexture(3, tex.value()).ok());  // reserved id
  REQUIRE(gr.value()->setTexture(mingol::gui::kTextureUserBase, tex.value(), false).ok());
  mingol::gui::DrawList l;
  l.addImage(mingol::gui::kTextureUserBase, {0, 0, 8, 4});
  l.addImage(mingol::gui::kTextureUserBase + 1, {0, 4, 8, 8});  // never registered
  auto target = makeTarget(dev, 8, 8);
  REQUIRE(target != nullptr);
  const float clear[4] = {0, 0, 0, 1};
  REQUIRE(gr.value()->render(l, *target, clear).ok());
  const Image img = readback(dev, *target);
  CHECK(img.at(1, 1) == (Color{255, 0, 0, 255}));
  CHECK(img.at(6, 1) == (Color{0, 0, 255, 255}));
  CHECK(img.at(2, 6) == (Color{0, 0, 0, 255}));
  CHECK_EQ(gr.value()->skippedCommands(), 1U);
  // replace and remove while frames may still be in flight
  for (int i = 0; i < 6; ++i) {
    REQUIRE(gr.value()->setTexture(mingol::gui::kTextureUserBase, tex.value(), true).ok());
    REQUIRE(gr.value()->render(l, *target, clear).ok());
  }
  REQUIRE(gr.value()->setTexture(mingol::gui::kTextureUserBase, nullptr).ok());
  REQUIRE(gr.value()->render(l, *target, clear).ok());
  CHECK_NO_VALIDATION_ERRORS();
}

TEST(GuiGpu, SrgbTargetGetsLinearisedColors) {
  GPU_OR_SKIP();
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Srgb, 2});
  REQUIRE(gr.ok());
  mingol::gui::DrawList l;
  l.addRect({0, 0, 8, 8}, {128, 128, 128});
  auto target = makeTarget(dev, 8, 8, rhi::Format::RGBA8Srgb);
  REQUIRE(target != nullptr);
  const float clear[4] = {0, 0, 0, 1};
  REQUIRE(gr.value()->render(l, *target, clear).ok());
  const Image img = readback(dev, *target);
  // the hardware re-encodes to sRGB, so the stored value is the original 128 (within rounding)
  CHECK(std::abs(static_cast<int>(img.at(4, 4).r) - 128) <= 2);
  CHECK_NO_VALIDATION_ERRORS();
}

TEST(GuiGpu, ErrorsAreReportedNotCrashed) {
  GPU_OR_SKIP();
  CHECK(!mingol::gui::GuiRenderer::create(dev, {rhi::Format::Undefined, 2}).ok());
  CHECK(!mingol::gui::GuiRenderer::create(dev, {rhi::Format::D32Float, 2}).ok());
  CHECK(!mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 0}).ok());
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 2});
  REQUIRE(gr.ok());
  auto wrong = makeTarget(dev, 8, 8, rhi::Format::BGRA8Unorm);
  REQUIRE(wrong != nullptr);
  mingol::gui::DrawList l;
  CHECK(!gr.value()->render(l, *wrong).ok());
}

TEST(GuiGpu, ManyFramesWithGrowingGeometry) {
  GPU_OR_SKIP();
  auto gr = mingol::gui::GuiRenderer::create(dev, {rhi::Format::RGBA8Unorm, 2});
  REQUIRE(gr.ok());
  auto target = makeTarget(dev, 64, 64);
  REQUIRE(target != nullptr);
  const float clear[4] = {0, 0, 0, 1};
  for (int i = 1; i <= 40; ++i) {
    mingol::gui::DrawList l;
    for (int k = 0; k < i * 60; ++k) l.addRect({static_cast<float>(k % 60), static_cast<float>(k % 50), static_cast<float>(k % 60 + 3), static_cast<float>(k % 50 + 3)}, {static_cast<std::uint8_t>(k), 100, 100, 200});
    REQUIRE(gr.value()->render(l, *target, clear).ok());
  }
  CHECK_NO_VALIDATION_ERRORS();
}
