// SPDX-License-Identifier: GPL-2.0-only
// MeshRenderer round trip on the real renderer: the reloaded scene must render the same pixels.
#include "render_test_util.hpp"
#include "sceneio_util.hpp"

using namespace mingol;
using namespace mingol::test;
using namespace mingol::sceneio;

TEST(SceneIoGpu, ReloadedSceneRendersIdenticalPixels) {
  GPU_OR_SKIP();
  auto mesh = rnd.createMesh(renderer::makeBox({0.5F, 0.5F, 0.5F}));
  REQUIRE(mesh.ok());
  renderer::MaterialDesc md;
  md.baseColor = {0, 0, 0, 1};
  md.emissive = {0.6F, 0, 0};
  auto mat = rnd.createMaterial(md);
  REQUIRE(mat.ok());

  AssetTable assets;
  assets.addMesh("mesh/box", mesh.value());
  assets.addMaterial("mat/red", mat.value());
  SceneIoContext ctx;
  ctx.assets = &assets;
  const auto registry = ComponentRegistry::withBuiltins();

  scene::Scene a;
  a.environment.clearColor[0] = a.environment.clearColor[1] = a.environment.clearColor[2] = 0.0F;
  auto* cam = a.createObject("cam");
  cam->addComponent<scene::CameraComponent>();
  cam->transform().setLocalPosition({0, 0, 8});
  auto* pivot = a.createObject("pivot");
  pivot->transform().setLocalPosition({-1, 0.5F, 0});
  auto* box = a.createObject("box", pivot);
  auto* mr = box->addComponent<scene::MeshRenderer>();
  mr->mesh = mesh.value();
  mr->material = mat.value();
  mr->castShadow = false;
  box->transform().setLocalPosition({2, 0, 0});
  box->transform().setLocalRotation(Quat::fromAxisAngle({0, 1, 0}, 0.5F));

  auto text = saveScene(a, ctx, registry);
  REQUIRE(text.ok());
  scene::Scene b;
  REQUIRE(loadScene(b, text.value(), ctx, registry).ok());
  auto* mr2 = b.find("box")->getComponent<scene::MeshRenderer>();
  REQUIRE(mr2 != nullptr);
  CHECK(mr2->mesh == mesh.value());
  CHECK(mr2->material == mat.value());
  CHECK(!mr2->castShadow);

  constexpr std::uint32_t W = 64, H = 64;
  auto ra = buildRenderScene(a, 1.0F);
  auto rb = buildRenderScene(b, 1.0F);
  REQUIRE(ra.ok());
  REQUIRE(rb.ok());
  Image ia = renderToImage(dev, rnd, ra.value(), W, H);
  Image ib = renderToImage(dev, rnd, rb.value(), W, H);
  REQUIRE(!ia.px.empty());
  bool same = ia.px.size() == ib.px.size();
  for (std::size_t i = 0; same && i < ia.px.size(); ++i) same = ia.px[i].r == ib.px[i].r && ia.px[i].g == ib.px[i].g && ia.px[i].b == ib.px[i].b && ia.px[i].a == ib.px[i].a;
  CHECK_MSG(same, "the reloaded scene renders differently");
  // and something was actually drawn
  std::size_t lit = 0;
  for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) if (ia.at(x, y).r > 60) ++lit;
  CHECK(lit > 50);
}

TEST(SceneIoGpu, MeshRendererWithUnregisteredAssetsCannotBeSaved) {
  GPU_OR_SKIP();
  auto mesh = rnd.createMesh(renderer::makeBox({0.5F, 0.5F, 0.5F}));
  REQUIRE(mesh.ok());
  AssetTable assets;   // empty
  SceneIoContext ctx;
  ctx.assets = &assets;
  scene::Scene a;
  a.createObject("o")->addComponent<scene::MeshRenderer>()->mesh = mesh.value();
  auto r = saveScene(a, ctx, ComponentRegistry::withBuiltins());
  CHECK(!r.ok());
}
