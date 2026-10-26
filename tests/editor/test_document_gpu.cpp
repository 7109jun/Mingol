// SPDX-License-Identifier: GPL-2.0-only
// EditorDocument with a real renderer: primitives, model assets, scene round trip with mesh references.
#include "editor_util.hpp"
#include "mingol/scene/components.hpp"
#include "render_test_util.hpp"

using namespace mingol;
using namespace mingol::editor;
using namespace mingol::test;

namespace {

assets::ModelData tinyModel() {
  assets::ModelData m;
  assets::ModelMaterial mat;
  mat.baseColor = {1, 0, 0, 1};
  m.materials.push_back(mat);
  assets::ModelMesh mesh;
  mesh.name = "quad";
  assets::ModelPrimitive prim;
  prim.data = renderer::makePlane(1.0F, 1.0F);
  prim.material = 0;
  mesh.primitives.push_back(std::move(prim));
  m.meshes.push_back(std::move(mesh));
  assets::ModelNode root;
  root.name = "holder";
  root.children = {1};
  assets::ModelNode leaf;
  leaf.name = "quad";
  leaf.parent = 0;
  leaf.mesh = 0;
  m.nodes = {root, leaf};
  m.rootNodes = {0};
  return m;
}

}  // namespace

TEST(EditorDocGpu, PrimitivesAreNamedAssetsAndSurviveUndoAndSave) {
  GPU_OR_SKIP();
  auto d = EditorDocument::create(&dev, &rnd);
  REQUIRE(d.ok());
  auto& doc = *d.value();
  CHECK(!doc.assets().meshNames().empty());
  CHECK(doc.assets().mesh("builtin/box") != nullptr);
  CHECK(doc.assets().material("builtin/default") != nullptr);

  auto box = doc.createPrimitive(PrimitiveKind::Box);
  REQUIRE(box.ok());
  auto sphere = doc.createPrimitive(PrimitiveKind::Sphere);
  REQUIRE(sphere.ok());
  CHECK(doc.createPrimitive(PrimitiveKind::Plane).ok());
  REQUIRE(doc.undo().ok());
  CHECK(doc.scene().find("Plane") == nullptr);
  auto* mr = doc.scene().find("Sphere")->getComponent<scene::MeshRenderer>();
  REQUIRE(mr != nullptr);
  CHECK(mr->mesh == doc.assets().mesh("builtin/sphere"));  // identity restored through names
  CHECK(mr->material == doc.assets().material("builtin/default"));

  // the asset picker offers the registered meshes and rejects unknown ones
  auto props = describeComponent(*mr, doc.propertyEnv());
  Property* meshProp = nullptr;
  for (auto& p : props) if (p.kind == PropKind::Asset && p.name == "Mesh") meshProp = &p;
  REQUIRE(meshProp != nullptr);
  CHECK(meshProp->assetChoices.size() >= 3);
  REQUIRE(doc.setProperty(*meshProp, PropValue(std::string("builtin/plane"))).ok());
  CHECK(mr->mesh == doc.assets().mesh("builtin/plane"));
  CHECK(!doc.setProperty(*meshProp, PropValue(std::string("nope"))).ok());
}

TEST(EditorDocGpu, ProjectWithPrimitivesReopensWithSameMeshes) {
  GPU_OR_SKIP();
  TempDir dir;
  {
    auto d = EditorDocument::create(&dev, &rnd);
    REQUIRE(d.ok());
    REQUIRE(d.value()->createProject(dir.path, "G").ok());
    REQUIRE(d.value()->createPrimitive(PrimitiveKind::Sphere).ok());
    REQUIRE(d.value()->saveScene().ok());
  }
  auto d = EditorDocument::create(&dev, &rnd);
  REQUIRE(d.ok());
  REQUIRE(d.value()->openProject(dir.path / "G.mindev").ok());
  auto* mr = d.value()->scene().find("Sphere")->getComponent<scene::MeshRenderer>();
  REQUIRE(mr != nullptr);
  CHECK(mr->mesh == d.value()->assets().mesh("builtin/sphere"));
}

TEST(EditorDocGpu, ModelAssetsInstantiateAndUndo) {
  GPU_OR_SKIP();
  auto d = EditorDocument::create(&dev, &rnd);
  REQUIRE(d.ok());
  auto& doc = *d.value();
  REQUIRE(doc.registerModel("assets/tiny.glb", tinyModel()).ok());
  CHECK(doc.assets().mesh("assets/tiny.glb#mesh0.0") != nullptr);
  CHECK(doc.assets().material("assets/tiny.glb#material0") != nullptr);
  auto root = doc.instantiateModelAsset("assets/tiny.glb");
  REQUIRE(root.ok());
  CHECK_EQ(root.value()->name(), std::string("tiny"));
  CHECK_EQ(doc.selected(), root.value());
  const std::size_t n = doc.allObjects().size();
  // a scene with model-derived meshes must serialise and reload by name
  REQUIRE(doc.undo().ok());
  REQUIRE(doc.redo().ok());
  CHECK_EQ(doc.allObjects().size(), n);
  auto* quad = doc.scene().find("quad");
  REQUIRE(quad != nullptr);
  CHECK(quad->getComponent<scene::MeshRenderer>()->mesh == doc.assets().mesh("assets/tiny.glb#mesh0.0"));
  CHECK(!doc.instantiateModelAsset("missing.glb").ok());
}
