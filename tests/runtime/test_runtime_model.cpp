// SPDX-License-Identifier: GPL-2.0-only
// GPU upload + instantiation of model data, on the real renderer.
#include <cmath>

#include "mingol/runtime/model_instance.hpp"
#include "render_test_util.hpp"

using namespace mingol;
using namespace mingol::runtime;
using namespace mingol::scene;
using namespace mingol::test;

namespace {

assets::Image solidImage(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  assets::Image img;
  img.width = w;
  img.height = h;
  img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
  for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
    img.rgba[i] = r; img.rgba[i + 1] = g; img.rgba[i + 2] = b; img.rgba[i + 3] = 255;
  }
  return img;
}

// A unit plane (XZ, normal +Y) with an emissive texture, as a node translated to x = 2.
assets::ModelData texturedPlaneModel() {
  assets::ModelData m;
  m.images.push_back(solidImage(4, 4, 255, 0, 0));
  assets::ModelMaterial mat;
  mat.baseColor = {0, 0, 0, 1};
  mat.emissive = {1, 1, 1};
  mat.emissiveImage = 0;
  m.materials.push_back(mat);
  assets::ModelMesh mesh;
  mesh.name = "plane";
  assets::ModelPrimitive prim;
  prim.data = renderer::makePlane(1.0F, 1.0F);
  prim.material = 0;
  mesh.primitives.push_back(std::move(prim));
  m.meshes.push_back(std::move(mesh));
  assets::ModelNode root;
  root.name = "holder";
  root.translation = {2, 0, 0};
  root.children = {1};
  assets::ModelNode leaf;
  leaf.name = "plane";
  leaf.parent = 0;
  leaf.mesh = 0;
  m.nodes = {root, leaf};
  m.rootNodes = {0};
  return m;
}

}  // namespace

TEST(RuntimeModel, UploadedTexturedMaterialRendersAtTheNodeTransform) {
  GPU_OR_SKIP();
  assets::ModelData model = texturedPlaneModel();
  auto gpuModel = uploadModel(dev, rnd, model);
  REQUIRE(gpuModel.ok());
  CHECK_EQ(gpuModel.value().meshes.size(), std::size_t{1});
  CHECK_EQ(gpuModel.value().materials.size(), std::size_t{1});

  Scene scene;
  scene.environment.clearColor[0] = scene.environment.clearColor[1] = scene.environment.clearColor[2] = 0.0F;
  GameObject* cam = scene.createObject("cam");
  cam->addComponent<CameraComponent>();
  cam->transform().setLocalPosition({0, 6, 0});
  cam->transform().setLocalRotation(Quat::fromAxisAngle({1, 0, 0}, -1.5707963F));  // looks down

  auto inst = instantiateModel(scene, model, gpuModel.value(), "m");
  REQUIRE(inst.ok());
  CHECK_EQ(inst.value().nodes.size(), std::size_t{2});
  CHECK_NEAR(inst.value().nodes[1]->transform().worldPosition().x, 2.0F, 1e-5F);

  constexpr std::uint32_t W = 96, H = 96;
  auto rs = buildRenderScene(scene, 1.0F);
  REQUIRE(rs.ok());
  CHECK_EQ(rs.value().items.size(), std::size_t{1});
  Image img = renderToImage(dev, rnd, rs.value(), W, H);
  REQUIRE(!img.px.empty());
  int px = 0, py = 0;
  REQUIRE(project(rs.value().camera, {2, 0, 0}, W, H, px, py));
  const Rgba c = img.at(px, py);
  // Emissive = texture (red, sRGB-decoded to linear 1,0,0) * (1,1,1): clearly red, no green/blue.
  CHECK_MSG(c.r > 150 && c.g < 40 && c.b < 40, "emissive texture colour not found at the plane");
  int ox = 0, oy = 0;
  REQUIRE(project(rs.value().camera, {-2, 0, 0}, W, H, ox, oy));
  const Rgba o = img.at(ox, oy);
  CHECK_MSG(o.r < 8 && o.g < 8 && o.b < 8, "geometry appeared at the untranslated position");
}

TEST(RuntimeModel, SharedUploadServesManyInstances) {
  GPU_OR_SKIP();
  assets::ModelData model = texturedPlaneModel();
  auto g = uploadModel(dev, rnd, model);
  REQUIRE(g.ok());
  Scene scene;
  auto a = instantiateModel(scene, model, g.value(), "a");
  auto b = instantiateModel(scene, model, g.value(), "b");
  REQUIRE(a.ok());
  REQUIRE(b.ok());
  auto* ma = a.value().nodes[1]->getComponent<MeshRenderer>();
  auto* mb = b.value().nodes[1]->getComponent<MeshRenderer>();
  REQUIRE(ma != nullptr);
  REQUIRE(mb != nullptr);
  CHECK(ma->mesh == mb->mesh);
  CHECK(ma->material == mb->material);
  scene.destroy(a.value().root);
  scene.flushDestroyed();
  CHECK(mb->mesh != nullptr);  // the other instance keeps working
}

TEST(RuntimeModel, BadImageIndexIsReportedNotCrashed) {
  GPU_OR_SKIP();
  assets::ModelData model = texturedPlaneModel();
  model.materials[0].emissiveImage = 7;
  CHECK(!uploadModel(dev, rnd, model).ok());
  model = texturedPlaneModel();
  model.meshes[0].primitives[0].material = 5;
  CHECK(!uploadModel(dev, rnd, model).ok());
}

TEST(RuntimeModel, NodeOrderDoesNotMatterAndCyclesAreRejected) {
  // No GPU needed: meshes are absent.
  assets::ModelData m;
  assets::ModelNode child;
  child.name = "child";
  child.parent = 1;  // parent comes later in the array
  child.translation = {0, 1, 0};
  assets::ModelNode parent;
  parent.name = "parent";
  parent.translation = {3, 0, 0};
  m.nodes = {child, parent};
  GpuModel empty;
  Scene scene;
  auto r = instantiateModel(scene, m, empty, "m");
  REQUIRE(r.ok());
  CHECK_NEAR(r.value().nodes[0]->transform().worldPosition().x, 3.0F, 1e-6F);
  CHECK_NEAR(r.value().nodes[0]->transform().worldPosition().y, 1.0F, 1e-6F);
  CHECK(r.value().nodes[0]->parent() == r.value().nodes[1]);

  assets::ModelData cyc;
  assets::ModelNode a, b;
  a.parent = 1;
  b.parent = 0;
  cyc.nodes = {a, b};
  const auto before = scene.objectCount();
  CHECK(!instantiateModel(scene, cyc, empty, "c").ok());
  scene.flushDestroyed();
  CHECK_EQ(scene.objectCount(), before);  // the half-built instance was removed

  assets::ModelData bad;
  assets::ModelNode n;
  n.mesh = 3;
  bad.nodes = {n};
  CHECK(!instantiateModel(scene, bad, empty, "b").ok());
}

TEST(RuntimeModel, SkinnedNodeGetsAnimatorUnderTheRootWithIdentityTransform) {
  assets::ModelData m;
  renderer::Joint j0;
  j0.name = "j0";
  auto sk = renderer::Skeleton::create({j0});
  REQUIRE(sk.ok());
  assets::ModelSkin skin;
  skin.name = "skin";
  skin.skeleton = std::move(sk).value();
  skin.jointNodes = {0};
  renderer::Channel ch;
  ch.joint = 0;
  ch.translation.times = {0.0F, 1.0F};
  ch.translation.values = {{0, 0, 0}, {0, 2, 0}};
  auto clip = renderer::AnimationClip::create("up", 1.0F, {ch});
  REQUIRE(clip.ok());
  skin.clips.push_back(std::move(clip).value());
  m.skins.push_back(std::move(skin));
  assets::ModelMesh mesh;
  mesh.skin = 0;
  mesh.primitives.emplace_back();
  m.meshes.push_back(std::move(mesh));

  assets::ModelNode jointNode;
  jointNode.name = "j0";
  assets::ModelNode meshNode;
  meshNode.name = "body";
  meshNode.mesh = 0;
  meshNode.skin = 0;
  meshNode.translation = {9, 9, 9};  // must be ignored
  meshNode.parent = 0;
  m.nodes = {jointNode, meshNode};

  GpuModel gm;
  gm.meshes.resize(1);
  gm.meshes[0].resize(1);  // null mesh: instantiation must not dereference it
  gm.defaultMaterial = nullptr;
  Scene scene;
  jointNode.translation = {5, 0, 0};
  m.nodes[0] = jointNode;
  auto r = instantiateModel(scene, m, gm, "rig");
  REQUIRE(r.ok());
  GameObject* body = r.value().nodes[1];
  CHECK(body->parent() == r.value().root);
  CHECK_NEAR(body->transform().worldPosition().x, 0.0F, 1e-6F);
  CHECK_NEAR(body->transform().worldPosition().y, 0.0F, 1e-6F);
  auto* an = body->getComponent<Animator>();
  REQUIRE(an != nullptr);
  CHECK(!an->playing());
  CHECK(an->play("up", false).ok());
  an->advance(1.0F);
  CHECK(an->finished());
  CHECK(!an->play("missing").ok());
}
