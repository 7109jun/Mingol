// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>
#include <functional>

#include "asset_test_util.hpp"
#include "mingol/assets/model.hpp"

using namespace mingol;
using namespace mingol::assets;
using namespace mingol::test;

namespace {

struct Bin {
  std::vector<std::uint8_t> bytes;
  std::size_t floats(const std::vector<float>& v) {
    align();
    const std::size_t off = bytes.size();
    for (float f : v) {
      std::uint32_t u;
      std::memcpy(&u, &f, 4);
      for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
    }
    return off;
  }
  std::size_t u16(const std::vector<int>& v) {
    align();
    const std::size_t off = bytes.size();
    for (int x : v) { bytes.push_back(static_cast<std::uint8_t>(x)); bytes.push_back(static_cast<std::uint8_t>(x >> 8)); }
    return off;
  }
  std::size_t u8(const std::vector<int>& v) {
    align();
    const std::size_t off = bytes.size();
    for (int x : v) bytes.push_back(static_cast<std::uint8_t>(x));
    return off;
  }
  std::size_t raw(const std::vector<std::uint8_t>& v) {
    align();
    const std::size_t off = bytes.size();
    bytes.insert(bytes.end(), v.begin(), v.end());
    return off;
  }
  void align() { while (bytes.size() % 4) bytes.push_back(0); }
};

std::string n(std::size_t v) { return std::to_string(v); }

// bufferView + accessor JSON fragments are built by index so tests read linearly.
struct Json {
  std::vector<std::string> views, accessors;
  int view(std::size_t off, std::size_t len) {
    views.push_back("{\"buffer\":0,\"byteOffset\":" + n(off) + ",\"byteLength\":" + n(len) + "}");
    return static_cast<int>(views.size() - 1);
  }
  int accessor(std::size_t off, std::size_t len, int comp, std::size_t count, const char* type, const char* extra = "") {
    const int v = view(off, len);
    accessors.push_back("{\"bufferView\":" + std::to_string(v) + ",\"componentType\":" + std::to_string(comp) +
                        ",\"count\":" + n(count) + ",\"type\":\"" + type + "\"" + extra + "}");
    return static_cast<int>(accessors.size() - 1);
  }
  static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s;
  }
  std::string tail(std::size_t binSize, const std::string& bufferExtra = "") const {
    return ",\"accessors\":[" + join(accessors) + "],\"bufferViews\":[" + join(views) + "],\"buffers\":[{\"byteLength\":" +
           n(binSize) + bufferExtra + "}]}";
  }
};

std::vector<std::uint8_t> glb(const std::string& jsonIn, const std::vector<std::uint8_t>& binIn) {
  std::string json = jsonIn;
  while (json.size() % 4) json += ' ';
  std::vector<std::uint8_t> bin = binIn;
  while (bin.size() % 4) bin.push_back(0);
  std::vector<std::uint8_t> out;
  auto p32 = [&](std::uint32_t x) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(x >> (8 * i))); };
  const std::uint32_t total = 12 + 8 + static_cast<std::uint32_t>(json.size()) + (bin.empty() ? 0 : 8 + static_cast<std::uint32_t>(bin.size()));
  p32(0x46546C67); p32(2); p32(total);
  p32(static_cast<std::uint32_t>(json.size())); p32(0x4E4F534A);
  out.insert(out.end(), json.begin(), json.end());
  if (!bin.empty()) {
    p32(static_cast<std::uint32_t>(bin.size())); p32(0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
  }
  return out;
}

const char* kHead = "{\"asset\":{\"version\":\"2.0\"},";

// One CCW triangle in the XY plane (normal +Z), optional extra JSON per primitive / material.
std::vector<std::uint8_t> triangleGlb(const std::string& nodes = "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0",
                                      const std::string& materials = "", const std::string& primExtra = "") {
  Bin b;
  Json j;
  const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
  const int a = j.accessor(pos, 36, 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
  const std::string json = std::string(kHead) + nodes + materials + ",\"meshes\":[{\"name\":\"tri\",\"primitives\":[{\"attributes\":{\"POSITION\":" +
                           std::to_string(a) + "}" + primExtra + "}]}]" + j.tail(b.bytes.size());
  return glb(json, b.bytes);
}

bool near3(Vec3 a, Vec3 b, float e = 1e-5F) {
  return std::fabs(a.x - b.x) <= e && std::fabs(a.y - b.y) <= e && std::fabs(a.z - b.z) <= e;
}
bool hasWarning(const ModelData& m, const std::string& part) {
  for (const auto& w : m.warnings) if (w.find(part) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST(Gltf, TriangleWithoutNormalsGetsGeneratedNormalsAndTangents) {
  auto r = loadGltf(triangleGlb());
  REQUIRE(r.ok());
  const ModelData& m = r.value();
  REQUIRE(m.meshes.size() == 1);
  REQUIRE(m.meshes[0].primitives.size() == 1);
  const auto& d = m.meshes[0].primitives[0].data;
  CHECK_EQ(d.vertices.size(), std::size_t{3});
  CHECK_EQ(d.indices.size(), std::size_t{3});
  CHECK(near3(d.vertices[1].position, {1, 0, 0}));
  for (const auto& v : d.vertices) {
    CHECK(near3(v.normal, {0, 0, 1}));  // CCW seen from +Z
    const Vec3 t{v.tangent.x, v.tangent.y, v.tangent.z};
    CHECK_NEAR(length(t), 1.0, 1e-4);
    CHECK_NEAR(dot(t, v.normal), 0.0, 1e-4);
    CHECK(std::fabs(v.tangent.w) == 1.0F);
  }
  CHECK(hasWarning(m, "no normals"));
  CHECK_EQ(m.materials.size(), std::size_t{0});
  CHECK_EQ(m.meshes[0].primitives[0].material, -1);
  CHECK(m.rootNodes == std::vector<int>{0});
  CHECK_EQ(m.nodes[0].mesh, 0);
}

TEST(Gltf, QuadAttributesTangentsFromUvAndMaterials) {
  Bin b;
  Json j;
  // Quad in the XY plane; u follows +X, v follows -Y (image space), so tangent = +X.
  const auto pos = b.floats({0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0});
  const auto nrm = b.floats({0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1});
  const auto uv = b.floats({0, 1, 1, 1, 1, 0, 0, 0});
  const auto idx = b.u16({0, 1, 2, 0, 2, 3});
  const int aP = j.accessor(pos, 48, 5126, 4, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
  const int aN = j.accessor(nrm, 48, 5126, 4, "VEC3");
  const int aU = j.accessor(uv, 32, 5126, 4, "VEC2");
  const int aI = j.accessor(idx, 12, 5123, 6, "SCALAR");
  const std::string materials =
      ",\"materials\":[{\"name\":\"gold\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.8,0.2,0.5],\"metallicFactor\":0.25,"
      "\"roughnessFactor\":0.75},\"emissiveFactor\":[0.1,0.2,0.3],\"alphaMode\":\"BLEND\",\"doubleSided\":true,"
      "\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":4}}},"
      "{\"name\":\"cutout\",\"alphaMode\":\"MASK\"}]";
  const std::string json = std::string(kHead) + "\"extensionsUsed\":[\"KHR_materials_emissive_strength\"],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]" + materials +
                           ",\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(aP) + ",\"NORMAL\":" + std::to_string(aN) +
                           ",\"TEXCOORD_0\":" + std::to_string(aU) + "},\"indices\":" + std::to_string(aI) + ",\"material\":0}]}]" + j.tail(b.bytes.size());
  auto r = loadGltf(glb(json, b.bytes));
  REQUIRE(r.ok());
  const ModelData& m = r.value();
  const auto& d = m.meshes[0].primitives[0].data;
  CHECK_EQ(d.indices.size(), std::size_t{6});
  CHECK_EQ(d.indices[4], 2u);
  CHECK_NEAR(d.vertices[2].uv.x, 1.0, 1e-6);
  CHECK_NEAR(d.vertices[2].uv.y, 0.0, 1e-6);
  for (const auto& v : d.vertices) {
    CHECK_MSG(near3({v.tangent.x, v.tangent.y, v.tangent.z}, {1, 0, 0}, 1e-4F),
              "tangent should follow +U which is +X here");
    // glTF: bitangent = cross(normal, tangent) * w. Here dP/dv = -Y (v grows downwards) while
    // cross(+Z, +X) = +Y, so w must be -1.
    CHECK_EQ(v.tangent.w, -1.0F);
  }
  CHECK(!hasWarning(m, "no normals"));
  REQUIRE(m.materials.size() == 2);
  const ModelMaterial& g = m.materials[0];
  CHECK_EQ(g.name, std::string("gold"));
  CHECK_NEAR(g.baseColor.y, 0.8, 1e-6);
  CHECK_NEAR(g.baseColor.w, 0.5, 1e-6);
  CHECK_NEAR(g.metallic, 0.25, 1e-6);
  CHECK_NEAR(g.roughness, 0.75, 1e-6);
  CHECK(near3(g.emissive, {0.4F, 0.8F, 1.2F}, 1e-5F));  // factor x emissive_strength
  CHECK(g.alphaMode == renderer::AlphaMode::Blend);
  CHECK(g.doubleSided);
  CHECK_EQ(g.baseColorImage, -1);
  CHECK(m.materials[1].alphaMode == renderer::AlphaMode::Opaque);
  CHECK(hasWarning(m, "alpha MASK"));  // never silently downgraded
}

TEST(Gltf, StripAndFanAreConvertedToTriangleLists) {
  for (int mode : {5, 6}) {
    Bin b;
    Json j;
    const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0});
    const int a = j.accessor(pos, 48, 5126, 4, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
    const std::string json = std::string(kHead) + "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" +
                             std::to_string(a) + "},\"mode\":" + std::to_string(mode) + "}]}]" + j.tail(b.bytes.size());
    auto r = loadGltf(glb(json, b.bytes));
    REQUIRE(r.ok());
    const auto& idx = r.value().meshes[0].primitives[0].data.indices;
    const std::vector<std::uint32_t> strip = {0, 1, 2, 2, 1, 3};  // second triangle flipped to keep winding
    const std::vector<std::uint32_t> fan = {0, 1, 2, 0, 2, 3};
    CHECK(idx == (mode == 5 ? strip : fan));
  }
}

TEST(Gltf, PointsAreSkippedWithAWarning) {
  auto r = loadGltf(triangleGlb("\"nodes\":[{\"mesh\":0}]", "", ",\"mode\":0"));
  REQUIRE(r.ok());
  CHECK(r.value().meshes[0].primitives.empty());
  CHECK(hasWarning(r.value(), "non-triangle"));
}

TEST(Gltf, NodeHierarchyTransformsAndMatrix) {
  const std::string nodes =
      "\"scene\":0,\"scenes\":[{\"nodes\":[0,3]}],\"nodes\":["
      "{\"name\":\"a\",\"translation\":[1,2,3],\"rotation\":[0,0.7071068,0,0.7071068],\"scale\":[2,2,2],\"children\":[1,2]},"
      "{\"name\":\"b\",\"matrix\":[1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1]},"
      "{\"name\":\"c\",\"mesh\":0},"
      "{\"name\":\"d\"}]";
  auto r = loadGltf(triangleGlb(nodes));
  REQUIRE(r.ok());
  const ModelData& m = r.value();
  REQUIRE(m.nodes.size() == 4);
  CHECK(m.rootNodes == (std::vector<int>{0, 3}));
  CHECK(near3(m.nodes[0].translation, {1, 2, 3}));
  CHECK(near3(m.nodes[0].scale, {2, 2, 2}, 1e-4F));
  CHECK_NEAR(m.nodes[0].rotation.y, 0.7071068, 1e-5);
  CHECK(near3(m.nodes[1].translation, {5, 6, 7}));  // matrix decomposed
  CHECK_EQ(m.nodes[1].parent, 0);
  CHECK_EQ(m.nodes[2].parent, 0);
  CHECK(m.nodes[0].children == (std::vector<int>{1, 2}));
  CHECK_EQ(m.nodes[2].mesh, 0);
  CHECK_EQ(m.nodes[1].mesh, -1);
}

TEST(Gltf, SharedMeshIsLoadedOnce) {
  auto r = loadGltf(triangleGlb("\"scenes\":[{\"nodes\":[0,1,2]}],\"nodes\":[{\"mesh\":0},{\"mesh\":0,\"translation\":[1,0,0]},{\"mesh\":0}]"));
  REQUIRE(r.ok());
  CHECK_EQ(r.value().meshes.size(), std::size_t{1});
  CHECK(r.value().nodes[0].mesh == r.value().nodes[2].mesh);
}

namespace {
// Skinned model. Skin joint order is deliberately NOT parent-first: [tip, root, mid].
std::vector<std::uint8_t> skinnedGlb(bool withBadJoint = false) {
  Bin b;
  Json j;
  const auto pos = b.floats({0, 0, 0, 0, 2, 0, 1, 0, 0});
  const auto joints = b.u8({1, 0, 0, 0,   0, 2, 0, 0,   1, 0, 0, 0});  // glTF joint positions (root, tip/mid, root)
  const auto weights = b.floats({1, 0, 0, 0,   0.5F, 0.5F, 0, 0,   1, 0, 0, 0});
  std::vector<float> ibm;
  auto mat = [&](float ty) {  // translation matrix, column-major
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, ty, 0, 1};
    ibm.insert(ibm.end(), m, m + 16);
  };
  mat(-2);  // tip  (skin joint 0)
  mat(0);   // root (skin joint 1)
  mat(-1);  // mid  (skin joint 2)
  const auto ib = b.floats(ibm);
  const auto times = b.floats({0, 1});
  const auto rootT = b.floats({0, 0, 0, 2, 0, 0});
  const auto midR = b.floats({0, 0, 0, 1, 0, 0.7071068F, 0, 0.7071068F});
  const int aP = j.accessor(pos, 36, 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,2,0]");
  const int aJ = j.accessor(joints, 12, 5121, 3, "VEC4");
  const int aW = j.accessor(weights, 48, 5126, 3, "VEC4");
  const int aIb = j.accessor(ib, 192, 5126, 3, "MAT4");
  const int aT = j.accessor(times, 8, 5126, 2, "SCALAR", ",\"min\":[0],\"max\":[1]");
  const int aRt = j.accessor(rootT, 24, 5126, 2, "VEC3");
  const int aMr = j.accessor(midR, 32, 5126, 2, "VEC4");
  const std::string badNode = withBadJoint ? ",{\"name\":\"stray\"}" : "";
  (void)badNode;
  const std::string json = std::string(kHead) +
      "\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],\"nodes\":["
      "{\"name\":\"body\",\"mesh\":0,\"skin\":0},"                                  // 0
      "{\"name\":\"root\",\"children\":[2]},"                                       // 1
      "{\"name\":\"mid\",\"translation\":[0,1,0],\"children\":[3]},"                // 2
      "{\"name\":\"tip\",\"translation\":[0,1,0]}],"                                // 3
      "\"skins\":[{\"joints\":[3,1,2],\"inverseBindMatrices\":" + std::to_string(aIb) + "}],"
      "\"animations\":[{\"name\":\"wave\",\"samplers\":["
      "{\"input\":" + std::to_string(aT) + ",\"output\":" + std::to_string(aRt) + ",\"interpolation\":\"LINEAR\"},"
      "{\"input\":" + std::to_string(aT) + ",\"output\":" + std::to_string(aMr) + ",\"interpolation\":\"STEP\"}],"
      "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
      "{\"sampler\":1,\"target\":{\"node\":2,\"path\":\"rotation\"}},"
      "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(aP) + ",\"JOINTS_0\":" + std::to_string(aJ) +
      ",\"WEIGHTS_0\":" + std::to_string(aW) + "}}]}]" + j.tail(b.bytes.size());
  return glb(json, b.bytes);
}
}  // namespace

TEST(Gltf, SkinJointsAreReorderedParentFirstAndVerticesRemapped) {
  auto r = loadGltf(skinnedGlb());
  REQUIRE(r.ok());
  const ModelData& m = r.value();
  REQUIRE(m.skins.size() == 1);
  const renderer::Skeleton& sk = m.skins[0].skeleton;
  REQUIRE(sk.jointCount() == 3);
  CHECK_EQ(sk.joint(0).name, std::string("root"));
  CHECK_EQ(sk.joint(1).name, std::string("mid"));
  CHECK_EQ(sk.joint(2).name, std::string("tip"));
  CHECK_EQ(sk.joint(0).parent, -1);
  CHECK_EQ(sk.joint(1).parent, 0);
  CHECK_EQ(sk.joint(2).parent, 1);
  CHECK(near3(sk.joint(1).bindLocal.translation, {0, 1, 0}));
  CHECK(m.skins[0].jointNodes == (std::vector<int>{1, 2, 3}));
  // Inverse bind matrices follow their joints through the reordering.
  CHECK_NEAR(sk.inverseBind(0).m[13], 0.0, 1e-6);
  CHECK_NEAR(sk.inverseBind(1).m[13], -1.0, 1e-6);
  CHECK_NEAR(sk.inverseBind(2).m[13], -2.0, 1e-6);

  REQUIRE(m.meshes.size() == 1);
  CHECK_EQ(m.meshes[0].skin, 0);
  CHECK_EQ(m.nodes[0].skin, 0);
  const auto& d = m.meshes[0].primitives[0].data;
  REQUIRE(d.skin.size() == 3);
  // v0: glTF joint 1 (root) -> 0.   v1: glTF joints 0 (tip) and 2 (mid) -> 2 and 1.
  CHECK_EQ(d.skin[0].joints[0], 0.0F);
  CHECK_EQ(d.skin[1].joints[0], 2.0F);
  CHECK_EQ(d.skin[1].joints[1], 1.0F);
  CHECK_NEAR(d.skin[1].weights[0] + d.skin[1].weights[1], 1.0, 1e-6);
  CHECK(d.validate().ok());
  CHECK_EQ(d.skin[2].joints[0], 0.0F);

  // At rest the skin matrices must be identity: global * inverseBind.
  auto rest = renderer::computeSkinMatrices(sk, sk.bindPose());
  REQUIRE(rest.ok());
  for (const Mat4& mm : rest.value()) {
    CHECK_NEAR(mm.m[0], 1.0, 1e-5);
    CHECK_NEAR(mm.m[13], 0.0, 1e-5);
  }
}

TEST(Gltf, AnimationBecomesAClipOnTheSkeleton) {
  auto r = loadGltf(skinnedGlb());
  REQUIRE(r.ok());
  const ModelData& m = r.value();
  REQUIRE(m.skins[0].clips.size() == 1);
  const renderer::AnimationClip& clip = m.skins[0].clips[0];
  CHECK_EQ(clip.name(), std::string("wave"));
  CHECK_NEAR(clip.duration(), 1.0, 1e-6);
  CHECK_EQ(clip.channels().size(), std::size_t{2});  // root translation + mid rotation
  auto pose = renderer::samplePose(m.skins[0].skeleton, clip, 0.5F, false);
  REQUIRE(pose.ok());
  CHECK(near3(pose.value()[0].translation, {1, 0, 0}, 1e-5F));  // LINEAR halfway: x = 1
  // STEP rotation holds the first key until the second time: identity at t = 0.5.
  CHECK_NEAR(pose.value()[1].rotation.w, 1.0, 1e-5);
  auto end = renderer::samplePose(m.skins[0].skeleton, clip, 1.0F, false);
  REQUIRE(end.ok());
  CHECK_NEAR(end.value()[1].rotation.y, 0.7071068, 1e-5);
  // The channel that targets the non-joint body node is reported, not silently dropped.
  CHECK(hasWarning(m, "not a skeleton joint"));
}

TEST(Gltf, EmbeddedPngViaBufferViewAndDataUri) {
  const auto png = fixture("rgba_8x8.png");
  Bin b;
  Json j;
  const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
  const auto pngOff = b.raw(png);
  const int aP = j.accessor(pos, 36, 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
  const int imgView = j.view(pngOff, png.size());
  const std::string json = std::string(kHead) + "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
      "\"images\":[{\"bufferView\":" + std::to_string(imgView) + ",\"mimeType\":\"image/png\"}],\"textures\":[{\"source\":0}],"
      "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},\"emissiveTexture\":{\"index\":0}}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(aP) + "},\"material\":0}]}]" + j.tail(b.bytes.size());
  auto r = loadGltf(glb(json, b.bytes));
  REQUIRE(r.ok());
  REQUIRE(r.value().images.size() == 1);  // same image used twice -> decoded once
  CHECK_EQ(r.value().images[0].width, 8u);
  CHECK_EQ(r.value().materials[0].baseColorImage, 0);
  CHECK_EQ(r.value().materials[0].emissiveImage, 0);
  CHECK_EQ(r.value().materials[0].normalImage, -1);

  // .gltf with the whole buffer as a base64 data URI.
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string b64;
  for (std::size_t i = 0; i < b.bytes.size(); i += 3) {
    const std::uint32_t v = (b.bytes[i] << 16) | (i + 1 < b.bytes.size() ? b.bytes[i + 1] << 8 : 0) | (i + 2 < b.bytes.size() ? b.bytes[i + 2] : 0);
    b64 += tbl[(v >> 18) & 63]; b64 += tbl[(v >> 12) & 63];
    b64 += i + 1 < b.bytes.size() ? tbl[(v >> 6) & 63] : '='; b64 += i + 2 < b.bytes.size() ? tbl[v & 63] : '=';
  }
  const std::string gltf = std::string(kHead) + "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
      "\"images\":[{\"bufferView\":" + std::to_string(imgView) + ",\"mimeType\":\"image/png\"}],\"textures\":[{\"source\":0}],"
      "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(aP) + "},\"material\":0}]}]" +
      j.tail(b.bytes.size(), ",\"uri\":\"data:application/octet-stream;base64," + b64 + "\"");
  auto r2 = loadGltf(std::vector<std::uint8_t>(gltf.begin(), gltf.end()));
  REQUIRE(r2.ok());
  CHECK_EQ(r2.value().images.size(), std::size_t{1});
  CHECK_EQ(r2.value().meshes[0].primitives[0].data.vertices.size(), std::size_t{3});
}

TEST(Gltf, ExternalResourcesGoThroughTheResolver) {
  Bin b;
  Json j;
  const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
  const int aP = j.accessor(pos, 36, 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
  const std::string gltf = std::string(kHead) + "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(aP) + "}}]}]" +
      j.tail(b.bytes.size(), ",\"uri\":\"my%20data.bin\"");
  const std::vector<std::uint8_t> bytes(gltf.begin(), gltf.end());

  std::vector<std::string> asked;
  UriResolver good = [&](const std::string& uri) -> Result<std::vector<std::uint8_t>> {
    asked.push_back(uri);
    return b.bytes;
  };
  auto r = loadGltf(bytes, good);
  REQUIRE(r.ok());
  CHECK(asked == std::vector<std::string>{"my data.bin"});  // percent-decoded

  auto none = loadGltf(bytes);  // no resolver
  REQUIRE(!none.ok());
  CHECK(none.error().code == ErrorCode::NotFound);

  UriResolver shortData = [&](const std::string&) -> Result<std::vector<std::uint8_t>> {
    return std::vector<std::uint8_t>(b.bytes.begin(), b.bytes.begin() + 10);
  };
  CHECK(!loadGltf(bytes, shortData).ok());
  UriResolver failing = [](const std::string&) -> Result<std::vector<std::uint8_t>> { return makeError(ErrorCode::Io, "disk on fire"); };
  auto f = loadGltf(bytes, failing);
  REQUIRE(!f.ok());
  CHECK(f.error().message.find("disk on fire") != std::string::npos);
}

TEST(Gltf, FileLoaderRejectsPathsThatEscapeTheDirectory) {
  const std::string dir = std::string(MINGOL_TEST_DATA_DIR);
  Bin b;
  Json j;
  const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
  const int aP = j.accessor(pos, 36, 5126, 3, "VEC3");
  auto write = [&](const std::string& name, const std::string& uri) -> std::filesystem::path {
    const std::string gltf = std::string(kHead) + "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" +
                             std::to_string(aP) + "}}]}]" + j.tail(b.bytes.size(), ",\"uri\":\"" + uri + "\"");
    const auto path = std::filesystem::temp_directory_path() / name;
    std::vector<std::uint8_t> bytes(gltf.begin(), gltf.end());
    CHECK(fs::writeFileAtomic(path, bytes).ok());
    return path;
  };
  (void)dir;
  const auto up = write("mingol_test_escape.gltf", "../secret.bin");
  auto r = loadGltfFile(up);
  REQUIRE(!r.ok());
  CHECK(r.error().message.find("escapes") != std::string::npos);
  const auto abs = write("mingol_test_abs.gltf", "/etc/passwd");
  CHECK(!loadGltfFile(abs).ok());
  const auto ok = write("mingol_test_ok.gltf", "mingol_test_ok.bin");
  REQUIRE(fs::writeFileAtomic(std::filesystem::temp_directory_path() / "mingol_test_ok.bin", b.bytes).ok());
  CHECK(loadGltfFile(ok).ok());
  std::error_code ec;
  for (const char* f : {"mingol_test_escape.gltf", "mingol_test_abs.gltf", "mingol_test_ok.gltf", "mingol_test_ok.bin"}) {
    std::filesystem::remove(std::filesystem::temp_directory_path() / f, ec);
  }
  CHECK(!loadGltfFile("/nonexistent/model.glb").ok());
}

TEST(Gltf, MalformedInputIsRejectedNotCrashed) {
  CHECK(!loadGltf({}).ok());
  const std::string text = "this is not a model";
  CHECK(!loadGltf(std::vector<std::uint8_t>(text.begin(), text.end())).ok());
  const std::string v1 = "{\"asset\":{\"version\":\"1.0\"}}";
  CHECK(!loadGltf(std::vector<std::uint8_t>(v1.begin(), v1.end())).ok());

  // Index beyond the vertex count.
  {
    Bin b; Json j;
    const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    const auto idx = b.u16({0, 1, 7});
    const int aP = j.accessor(pos, 36, 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
    const int aI = j.accessor(idx, 6, 5123, 3, "SCALAR");
    const std::string json = std::string(kHead) + "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" +
                             std::to_string(aP) + "},\"indices\":" + std::to_string(aI) + "}]}]" + j.tail(b.bytes.size());
    auto r = loadGltf(glb(json, b.bytes));
    REQUIRE(!r.ok());
    CHECK(r.error().code == ErrorCode::Corrupt);
  }
  // Accessor reaching past its buffer view.
  {
    Bin b; Json j;
    const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    const int aP = j.accessor(pos, 36, 5126, 30, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
    const std::string json = std::string(kHead) + "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" +
                             std::to_string(aP) + "}}]}]" + j.tail(b.bytes.size());
    CHECK(!loadGltf(glb(json, b.bytes)).ok());
  }
  // Buffer larger than the available binary chunk.
  {
    Bin b; Json j;
    const auto pos = b.floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    const int aP = j.accessor(pos, 36, 5126, 3, "VEC3");
    const std::string json = std::string(kHead) + "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" +
                             std::to_string(aP) + "}}]}]" + j.tail(100000);
    CHECK(!loadGltf(glb(json, b.bytes)).ok());
  }
  // Node cycle.
  CHECK(!loadGltf(triangleGlb("\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"children\":[1],\"mesh\":0},{\"children\":[0]}]")).ok());
  // Skinned joint index beyond the skin.
  {
    auto bytes = skinnedGlb();
    // JOINTS_0 holds {1,0,0,0, 0,2,0,0, 1,0,0,0}: change the 2 to 9 in the binary chunk.
    std::size_t p = 0;
    for (; p + 12 < bytes.size(); ++p) {
      if (bytes[p] == 1 && bytes[p + 1] == 0 && bytes[p + 5] == 2 && bytes[p + 8] == 1 && bytes[p + 2] == 0) break;
    }
    REQUIRE(p + 12 < bytes.size());
    bytes[p + 5] = 9;
    auto r = loadGltf(bytes);
    REQUIRE(!r.ok());
  }
}

TEST(Gltf, TooManyJointsAndVertexLimit) {
  ModelLimits lim;
  lim.maxVertices = 2;
  auto r = loadGltf(triangleGlb(), {}, lim);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::OutOfRange);
  ModelLimits nodes;
  nodes.maxNodes = 0;
  CHECK(!loadGltf(triangleGlb(), {}, nodes).ok());
}

TEST(Gltf, RandomCorruptionNeverCrashes) {
  const std::vector<std::vector<std::uint8_t>> seeds = {skinnedGlb(), triangleGlb()};
  Lcg rng(2024);
  int rejected = 0, accepted = 0;
  for (const auto& src : seeds) {
    for (int i = 0; i < 4000; ++i) {
      auto r = loadGltf(mutate(src, rng));
      if (!r.ok()) { ++rejected; continue; }
      ++accepted;
      // Anything accepted must be internally consistent: every mesh validates and every index is in range.
      for (const auto& mesh : r.value().meshes) {
        for (const auto& p : mesh.primitives) CHECK(p.data.validate().ok());
      }
      for (const auto& node : r.value().nodes) {
        CHECK(node.mesh >= -1 && node.mesh < static_cast<int>(r.value().meshes.size()));
        CHECK(node.skin >= -1 && node.skin < static_cast<int>(r.value().skins.size()));
      }
    }
  }
  CHECK(rejected > 100);
  CHECK(accepted > 0);  // single-byte edits in unused regions still load; the harness is not just rejecting everything
}
