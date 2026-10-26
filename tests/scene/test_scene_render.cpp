// SPDX-License-Identifier: GPL-2.0-only
// Scene -> RenderScene -> pixels, on the real renderer.
#include <cmath>

#include "mingol/scene/components.hpp"
#include "render_test_util.hpp"

using namespace mingol;
using namespace mingol::scene;
using namespace mingol::test;

namespace {
bool isClear(const Rgba& p) { return p.r < 8 && p.g < 8 && p.b < 8; }
bool isEmissive(const Rgba& p) { return p.r > 100 && p.g < 60 && p.b < 60; }  // emissive (0.6,0,0) is red
}  // namespace

TEST(SceneRender, ObjectAppearsWhereItsWorldTransformSays) {
  GPU_OR_SKIP();
  auto mesh = rnd.createMesh(renderer::makeBox({0.5F, 0.5F, 0.5F}));
  REQUIRE(mesh.ok());
  renderer::MaterialDesc md;
  md.baseColor = {0, 0, 0, 1};
  md.emissive = {0.6F, 0, 0};
  auto mat = rnd.createMaterial(md);
  REQUIRE(mat.ok());

  Scene scene;
  scene.environment.clearColor[0] = scene.environment.clearColor[1] = scene.environment.clearColor[2] = 0.0F;
  GameObject* cam = scene.createObject("cam");
  cam->addComponent<CameraComponent>();
  cam->transform().setLocalPosition({0, 0, 8});

  GameObject* pivot = scene.createObject("pivot");
  GameObject* box = scene.createObject("box", pivot);
  MeshRenderer* mr = box->addComponent<MeshRenderer>();
  mr->mesh = mesh.value();
  mr->material = mat.value();
  box->transform().setLocalPosition({2, 0, 0});

  constexpr std::uint32_t W = 96, H = 96;
  using Frame = std::pair<renderer::RenderScene, Image>;
  auto frame = [&]() -> Frame {
    auto rs = buildRenderScene(scene, 1.0F);
    CHECK(rs.ok());
    if (!rs.ok()) return {};
    Image img = renderToImage(dev, rnd, rs.value(), W, H);
    return {std::move(rs).value(), std::move(img)};
  };

  // Parent at the origin: the box sits at world x = +2.
  {
    auto [rs, img] = frame();
    REQUIRE(!img.px.empty());
    CHECK_EQ(rs.items.size(), std::size_t{1});
    int px = 0, py = 0;
    REQUIRE(project(rs.camera, {2, 0, 0}, W, H, px, py));
    CHECK_MSG(isEmissive(img.at(px, py)), "box missing at predicted pixel");
    int cx = 0, cy = 0;
    REQUIRE(project(rs.camera, {-2, 0, 0}, W, H, cx, cy));
    CHECK_MSG(isClear(img.at(cx, cy)), "unexpected geometry at the mirrored position");
  }
  // Moving the PARENT moves the child on screen: the hierarchy reaches the renderer.
  pivot->transform().setLocalPosition({-4, 0, 0});  // box now at world x = -2
  {
    auto [rs, img] = frame();
    int px = 0, py = 0;
    REQUIRE(project(rs.camera, {-2, 0, 0}, W, H, px, py));
    CHECK_MSG(isEmissive(img.at(px, py)), "box did not follow its parent");
    int ox = 0, oy = 0;
    REQUIRE(project(rs.camera, {2, 0, 0}, W, H, ox, oy));
    CHECK_MSG(isClear(img.at(ox, oy)), "box left a ghost at its old position");
  }
  // Deactivating the parent removes the child from the frame.
  pivot->setActive(false);
  {
    auto [rs, img] = frame();
    CHECK_EQ(rs.items.size(), std::size_t{0});
    int px = 0, py = 0;
    REQUIRE(project(rs.camera, {-2, 0, 0}, W, H, px, py));
    CHECK(isClear(img.at(px, py)));
  }
  // Destroying it leaves nothing behind.
  scene.destroy(pivot);
  CHECK_EQ(scene.objectCount(), std::size_t{1});
}
