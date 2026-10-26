// SPDX-License-Identifier: GPL-2.0-only
// The editor with a real renderer: the viewport shows the scene, and the GUI composes it.
#include "app_util.hpp"
#include "mingol/gui/gui_renderer.hpp"
#include "render_test_util.hpp"

using namespace mingol;
using namespace mingol::editor;
using namespace mingol::test;

TEST(EditorAppGpu, ViewportRendersSceneAndGuiComposesIt) {
  GPU_OR_SKIP();
  EditorAppDesc desc;
  desc.device = &dev;
  desc.renderer = &rnd;
  AppDriver d(desc);
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  auto box = doc.createPrimitive(PrimitiveKind::Box);
  REQUIRE(box.ok());
  box.value()->transform().setLocalScale({2, 2, 2});
  box.value()->transform().setLocalPosition({0, 1, 0});
  doc.commit("shape");
  d.frame(3);

  auto tex = d.app->renderViewport();
  REQUIRE(tex.ok());
  REQUIRE(tex.value() != nullptr);
  CHECK_EQ(tex.value()->desc().width, static_cast<std::uint32_t>(d.app->viewportRect().width()));
  auto img = readImage(dev, *tex.value());
  REQUIRE(img.width > 0);
  // the box is lit by the default directional light: the centre pixel is neither clear colour nor black
  const Rgba c = img.at(img.width / 2, img.height / 2);
  const Rgba corner = img.at(2, 2);
  CHECK_MSG(c.r + c.g + c.b > corner.r + corner.g + corner.b + 60, "box not visible in viewport texture");

  d.frame(2);  // the next frame knows the texture and draws it
  {  // readback left the texture in TransferSrc; put it back where the GUI samples it
    auto list = dev.createCommandList();
    REQUIRE(list.ok());
    REQUIRE(list.value()->begin().ok());
    list.value()->transition(*tex.value(), rhi::TextureState::ShaderRead);
    REQUIRE(list.value()->end().ok());
    REQUIRE(dev.submit(*list.value()).ok());
  }
  // GUI -> offscreen target, viewport texture sampled through the user texture id
  gui::GuiRendererDesc gd;
  gd.targetFormat = rhi::Format::RGBA8Unorm;
  auto gr = gui::GuiRenderer::create(dev, gd);
  REQUIRE(gr.ok());
  REQUIRE(gr.value()->setTexture(gui::kTextureUserBase, tex.value(), true).ok());
  auto target = makeTarget(dev, 1280, 720, rhi::Format::RGBA8Unorm);
  REQUIRE(target != nullptr);
  const float clear[4] = {0, 0, 0, 1};
  REQUIRE(gr.value()->render(d.app->gui().drawList(), *target, clear).ok());
  CHECK_EQ(gr.value()->skippedCommands(), 0u);
  auto shot = readImage(dev, *target);
  const Rect vr = d.app->viewportRect();
  int total = 0, match = 0, litTexture = 0;
  for (std::uint32_t y = 0; y < img.height; y += 4) {
    for (std::uint32_t x = 0; x < img.width; x += 4) {
      const Rgba t = img.at(x, y);
      const Rgba g = shot.at(static_cast<std::uint32_t>(vr.x0) + x, static_cast<std::uint32_t>(vr.y0) + y);
      ++total;
      if (std::abs(t.r - g.r) < 8 && std::abs(t.g - g.g) < 8 && std::abs(t.b - g.b) < 8) ++match;
      if (t.r + t.g + t.b > corner.r + corner.g + corner.b + 60) ++litTexture;
    }
  }
  CHECK_MSG(match * 100 >= total * 85, "GUI shows the viewport texture (" + std::to_string(match) + "/" + std::to_string(total) + ")");
  CHECK(litTexture > 50);
  if (const char* out = std::getenv("MINGOL_GUI_SCREENSHOT_DIR")) {
    std::FILE* f = std::fopen((std::string(out) + "/editor_gpu.ppm").c_str(), "wb");
    if (f != nullptr) {
      std::fprintf(f, "P6\n%u %u\n255\n", shot.width, shot.height);
      for (const Rgba& p : shot.px) { const unsigned char rgb[3] = {static_cast<unsigned char>(p.r), static_cast<unsigned char>(p.g), static_cast<unsigned char>(p.b)}; std::fwrite(rgb, 1, 3, f); }
      std::fclose(f);
    }
  }
}

TEST(EditorAppGpu, PlayModeUsesTheGameCameraAndStopRestores) {
  GPU_OR_SKIP();
  EditorAppDesc desc;
  desc.device = &dev;
  desc.renderer = &rnd;
  AppDriver d(desc);
  REQUIRE(d.app != nullptr);
  auto& doc = d.app->document();
  REQUIRE(doc.createPrimitive(PrimitiveKind::Sphere).ok());
  d.frame(3);
  REQUIRE(doc.play().ok());
  d.frame(5);
  auto tex = d.app->renderViewport();
  REQUIRE(tex.ok());
  CHECK(tex.value() != nullptr);
  REQUIRE(doc.stop().ok());
  d.frame(2);
  CHECK(doc.scene().find("Sphere") != nullptr);
  CHECK(d.app->gui().errors().empty());
}
