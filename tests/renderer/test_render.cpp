// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/renderer/animation.hpp"
#include "render_test_util.hpp"

using namespace mingol;
using namespace mingol::test;
using namespace mingol::renderer;

namespace {

[[maybe_unused]] Camera topDown(float height = 4.0F, float fovDeg = 40.0F) {
  // Looking straight down -Y; "up" on screen is -Z.
  return Camera::lookAt({0, height, 0}, {0, 0, 0}, {0, 0, -1}, radians(fovDeg), 1.0F, 0.1F, 100.0F);
}

std::shared_ptr<Material> mat(Renderer& r, MaterialDesc d) {
  auto m = r.createMaterial(d);
  if (!m.ok()) {
    fail(__FILE__, __LINE__, m.error().describe());
    return nullptr;
  }
  return std::move(m).value();
}
std::shared_ptr<Mesh> mesh(Renderer& r, const MeshData& d) {
  auto m = r.createMesh(d);
  if (!m.ok()) {
    fail(__FILE__, __LINE__, m.error().describe());
    return nullptr;
  }
  return std::move(m).value();
}

}  // namespace

TEST(Render, SmokeDumpsImage) {
  GPU_OR_SKIP();
  RenderScene s;
  s.camera = Camera::lookAt({3, 3, 4}, {0, 0.5F, 0}, {0, 1, 0}, radians(45.0F), 1.0F, 0.1F, 50.0F);
  s.clearColor[0] = 0.05F; s.clearColor[1] = 0.07F; s.clearColor[2] = 0.1F;
  Light sun;
  sun.direction = {-0.5F, -1.0F, -0.4F};
  sun.intensity = 3.1F;
  sun.castShadow = true;
  s.lights.push_back(sun);
  MaterialDesc floor;
  floor.baseColor = {0.6F, 0.6F, 0.6F, 1};
  MaterialDesc red;
  red.baseColor = {0.8F, 0.1F, 0.1F, 1};
  red.roughness = 0.4F;
  MaterialDesc gold;
  gold.baseColor = {1.0F, 0.77F, 0.34F, 1};
  gold.metallic = 1.0F;
  gold.roughness = 0.25F;
  s.items.push_back({mesh(rnd, makePlane(10, 10)), mat(rnd, floor), Mat4::identity(), {}, true});
  s.items.push_back({mesh(rnd, makeBox({0.5F, 0.5F, 0.5F})), mat(rnd, red), Mat4::translation({-0.9F, 0.5F, 0}), {}, true});
  s.items.push_back({mesh(rnd, makeUvSphere(0.6F, 48, 24)), mat(rnd, gold), Mat4::translation({0.7F, 0.6F, 0.2F}), {}, true});
  const Image img = renderToImage(dev, rnd, s, 256, 256);
  REQUIRE(!img.px.empty());
  writePpm(img, "/tmp/mingol_smoke.ppm");
  CHECK(luma(img.at(128, 128)) > 0.0);
}

TEST(Shadow, FallsWhereGeometryPredictsAndHasNoAcne) {
  GPU_OR_SKIP();
  RenderScene s;
  s.camera = topDown(10.0F, 30.0F);
  s.ambient = {0, 0, 0};
  Light sun;
  sun.direction = {0.4F, -1.0F, 0.0F};  // travels toward +X, so shadows fall to +X (screen right)
  sun.intensity = 3.14159F;
  sun.castShadow = true;
  s.lights.push_back(sun);
  MaterialDesc plain;
  plain.baseColor = {0.8F, 0.8F, 0.8F, 1};
  auto plane = mesh(rnd, makePlane(12, 12));
  auto box = mesh(rnd, makeBox({0.6F, 1.0F, 0.6F}));  // 2 tall: top face at y = 2
  auto m = mat(rnd, plain);
  s.items.push_back({plane, m, Mat4::identity(), {}, true});
  s.items.push_back({box, m, Mat4::translation({-1.0F, 1.0F, 0.5F}), {}, true});

  const std::uint32_t N = 128;
  const Image shadowed = renderToImage(dev, rnd, s, N, N);
  REQUIRE(!shadowed.px.empty());
  writePpm(shadowed, "/tmp/mingol_shadow.ppm");
  CHECK(rnd.stats().shadowsRendered);
  CHECK(rnd.stats().shadowDrawCalls == 2);

  // The box footprint spans x in [-1.6,-0.4]. Its top edge (y=2) projects along the light by
  // 2 * 0.4 = 0.8 in +X, so the shadow on the floor covers x in about [-0.4, 0.4] to the right of
  // the footprint (z in [-0.1, 1.1]).
  int sx = 0, sy = 0, lx = 0, ly = 0, ox = 0, oy = 0;
  REQUIRE(project(s.camera, {0.0F, 0, 0.5F}, N, N, sx, sy));     // inside the predicted shadow
  REQUIRE(project(s.camera, {2.5F, 0, 0}, N, N, lx, ly));        // lit floor, well clear
  REQUIRE(project(s.camera, {0.0F, 0, -0.7F}, N, N, ox, oy));    // lit floor beside the shadow (z outside)
  int mx = 0, my = 0;
  REQUIRE(project(s.camera, {0.0F, 0, 1.8F}, N, N, mx, my));     // and on the other side
  const double shadowLuma = luma(shadowed.at(sx, sy));
  const double litLuma = luma(shadowed.at(lx, ly));
  CHECK_MSG(shadowLuma < litLuma * 0.2, "shadow luma " + std::to_string(shadowLuma) + " lit " + std::to_string(litLuma));
  CHECK(luma(shadowed.at(ox, oy)) > litLuma * 0.9);
  CHECK(luma(shadowed.at(mx, my)) > litLuma * 0.9);

  // No acne: the lit floor is uniform. Sample a grid of floor points away from the box.
  double lo = 1e9, hi = -1e9;
  for (float x = 1.0F; x <= 4.0F; x += 0.5F) {
    for (float z = -3.0F; z <= 3.0F; z += 0.5F) {
      int px = 0, py = 0;
      if (!project(s.camera, {x, 0, z}, N, N, px, py) || px < 0 || py < 0 || px >= int(N) || py >= int(N)) continue;
      const double l = luma(shadowed.at(px, py));
      lo = std::fmin(lo, l);
      hi = std::fmax(hi, l);
    }
  }
  CHECK_MSG(hi - lo < 3.0, "lit floor varies from " + std::to_string(lo) + " to " + std::to_string(hi) + " (shadow acne?)");

  // The same scene without shadows: the shadowed spot becomes as bright as the rest of the floor.
  RenderScene noShadow = s;
  noShadow.lights[0].castShadow = false;
  const Image flat = renderToImage(dev, rnd, noShadow, N, N);
  REQUIRE(!flat.px.empty());
  CHECK(!rnd.stats().shadowsRendered);
  CHECK(near(static_cast<int>(luma(flat.at(sx, sy))), static_cast<int>(luma(flat.at(lx, ly))), 2));

  // A caster that opts out leaves no shadow either.
  RenderScene optOut = s;
  optOut.items[1].castShadow = false;
  const Image img3 = renderToImage(dev, rnd, optOut, N, N);
  CHECK(luma(img3.at(sx, sy)) > litLuma * 0.9);
}

// ---- Shading model vs. an independent CPU reference ------------------------------------------------------

namespace {

RenderScene floorScene(float cameraHeight = 4.0F) {
  RenderScene s;
  s.camera = topDown(cameraHeight, 40.0F);
  s.ambient = {0, 0, 0};
  return s;
}

// The pixel under the world origin sees V = (0,1,0) and the floor normal N = (0,1,0).
int expectedByte(double linear) { return toByte(linear); }

}  // namespace

TEST(Shading, EmissiveOnlyMatchesToneMapAndSrgbEncode) {
  GPU_OR_SKIP();
  RenderScene s = floorScene();
  MaterialDesc d;
  d.baseColor = {0, 0, 0, 1};
  d.emissive = {0.4F, 0.2F, 0.05F};
  s.items.push_back({mesh(rnd, makePlane(20, 20)), mat(rnd, d), Mat4::identity(), {}, true});
  const Image img = renderToImage(dev, rnd, s);
  REQUIRE(!img.px.empty());
  const Rgba p = img.at(48, 48);
  CHECK_MSG(near(p.r, expectedByte(0.4), 2), "r=" + std::to_string(p.r) + " expected " + std::to_string(expectedByte(0.4)));
  CHECK_MSG(near(p.g, expectedByte(0.2), 2), "g=" + std::to_string(p.g));
  CHECK_MSG(near(p.b, expectedByte(0.05), 2), "b=" + std::to_string(p.b));
  CHECK_EQ(p.a, 255);
  // Empty areas show the clear colour.
  RenderScene empty = floorScene();
  empty.clearColor[0] = 1.0F;
  empty.clearColor[1] = 0.0F;
  empty.clearColor[2] = 0.0F;
  const Image cleared = renderToImage(dev, rnd, empty);
  CHECK(near(cleared.at(10, 10).r, 255, 1) && cleared.at(10, 10).g == 0);
}

TEST(Shading, UnormTargetsAreGammaEncodedInTheShader) {
  GPU_OR_SKIP();
  RenderScene s = floorScene();
  MaterialDesc d;
  d.baseColor = {0, 0, 0, 1};
  d.emissive = {0.3F, 0.3F, 0.3F};
  s.items.push_back({mesh(rnd, makePlane(20, 20)), mat(rnd, d), Mat4::identity(), {}, true});
  const Image srgb = renderToImage(dev, rnd, s, 32, 32, rhi::Format::RGBA8Srgb);
  const Image unorm = renderToImage(dev, rnd, s, 32, 32, rhi::Format::RGBA8Unorm);
  const Image bgra = renderToImage(dev, rnd, s, 32, 32, rhi::Format::BGRA8Srgb);
  REQUIRE(!srgb.px.empty() && !unorm.px.empty() && !bgra.px.empty());
  // pow(x, 1/2.2) in the shader vs the exact sRGB curve differ by at most a few levels.
  CHECK(near(srgb.at(16, 16).r, unorm.at(16, 16).r, 4));
  CHECK(near(bgra.at(16, 16).r, srgb.at(16, 16).r, 1));  // channel order is handled by the readback
  CHECK(near(bgra.at(16, 16).b, srgb.at(16, 16).b, 1));
}

namespace {

void checkLambertLike(rhi::Device& dev, Renderer& rnd, double angleDeg, double metallic, double roughness, V3 albedo,
                      float intensity, double tol) {
  RenderScene s = floorScene();
  Light sun;
  const double a = angleDeg * 3.14159265358979 / 180.0;
  sun.direction = {static_cast<float>(std::sin(a)), static_cast<float>(-std::cos(a)), 0};
  sun.intensity = intensity;
  s.lights.push_back(sun);
  MaterialDesc d;
  d.baseColor = {static_cast<float>(albedo.x), static_cast<float>(albedo.y), static_cast<float>(albedo.z), 1};
  d.metallic = static_cast<float>(metallic);
  d.roughness = static_cast<float>(roughness);
  s.items.push_back({mesh(rnd, makePlane(20, 20)), mat(rnd, d), Mat4::identity(), {}, true});
  const Image img = renderToImage(dev, rnd, s, 64, 64);
  if (img.px.empty()) return;
  PbrRef ref;
  ref.albedo = albedo;
  ref.metallic = metallic;
  ref.roughness = roughness;
  const V3 L{-std::sin(a), std::cos(a), 0};
  // Pixel (32,32)'s centre is half a pixel right/down of the image centre; sharp specular lobes
  // are sensitive to that, so evaluate the reference at the pixel's actual floor position.
  const double halfExtent = 4.0 * std::tan(20.0 * 3.14159265358979 / 180.0);
  const double off = (0.5 / 32.0) * halfExtent;
  const V3 V = norm3(V3{0, 4, 0} - V3{off, 0, off});
  const V3 lin = ref.light({0, 1, 0}, V, L, intensity);
  const Rgba p = img.at(32, 32);
  const std::string what = "angle " + std::to_string(angleDeg) + " metal " + std::to_string(metallic) + " rough " +
                           std::to_string(roughness);
  CHECK_MSG(near(p.r, expectedByte(lin.x), static_cast<int>(tol)),
            what + ": r=" + std::to_string(p.r) + " expected " + std::to_string(expectedByte(lin.x)));
  CHECK_MSG(near(p.g, expectedByte(lin.y), static_cast<int>(tol)),
            what + ": g=" + std::to_string(p.g) + " expected " + std::to_string(expectedByte(lin.y)));
  CHECK_MSG(near(p.b, expectedByte(lin.z), static_cast<int>(tol)),
            what + ": b=" + std::to_string(p.b) + " expected " + std::to_string(expectedByte(lin.z)));
}

}  // namespace

TEST(Shading, DirectionalLightMatchesReferenceBrdf) {
  GPU_OR_SKIP();
  const V3 grey{0.8, 0.8, 0.8};
  for (double angle : {0.0, 30.0, 60.0, 80.0}) checkLambertLike(dev, rnd, angle, 0.0, 1.0, grey, 3.14159F, 2);
  // Glossier dielectric and coloured albedo: exercises the specular lobe and Fresnel.
  checkLambertLike(dev, rnd, 25.0, 0.0, 0.35, {0.9, 0.3, 0.2}, 4.0F, 2);
  checkLambertLike(dev, rnd, 10.0, 0.0, 0.15, {0.5, 0.5, 0.8}, 3.0F, 3);
  // Metals: no diffuse, tinted specular.
  checkLambertLike(dev, rnd, 30.0, 1.0, 0.3, {1.0, 0.77, 0.34}, 4.0F, 5);  // steep specular lobe: float vs double drift
  checkLambertLike(dev, rnd, 0.0, 1.0, 0.5, {0.95, 0.64, 0.54}, 3.0F, 3);
  checkLambertLike(dev, rnd, 45.0, 0.5, 0.6, {0.7, 0.7, 0.7}, 5.0F, 3);
}

TEST(Shading, BackfacingLightGivesOnlyAmbient) {
  GPU_OR_SKIP();
  RenderScene s = floorScene();
  s.ambient = {0.1F, 0.1F, 0.1F};
  Light sun;
  sun.direction = {0, 1, 0};  // travels upward: lights the underside only
  sun.intensity = 10;
  s.lights.push_back(sun);
  MaterialDesc d;
  d.baseColor = {0.5F, 0.5F, 0.5F, 1};
  s.items.push_back({mesh(rnd, makePlane(20, 20)), mat(rnd, d), Mat4::identity(), {}, true});
  const Image img = renderToImage(dev, rnd, s, 64, 64);
  REQUIRE(!img.px.empty());
  const int expected = expectedByte(0.05);
  CHECK_MSG(near(img.at(32, 32).r, expected, 2), "r=" + std::to_string(img.at(32, 32).r) + " expected " + std::to_string(expected));
}

TEST(Shading, PointLightFollowsInverseSquareWithRangeWindow) {
  GPU_OR_SKIP();
  auto run = [&](float lightHeight, float range, float worldX) -> std::pair<int, int> {
    RenderScene s = floorScene(6.0F);
    Light l;
    l.type = LightType::Point;
    l.position = {0, lightHeight, 0};
    l.intensity = 12.0F;
    l.range = range;
    s.lights.push_back(l);
    MaterialDesc d;
    d.baseColor = {0.8F, 0.8F, 0.8F, 1};
    s.items.push_back({mesh(rnd, makePlane(30, 30)), mat(rnd, d), Mat4::identity(), {}, true});
    const Image img = renderToImage(dev, rnd, s, 96, 96);
    if (img.px.empty()) return {-1, -1};
    int px = 0, py = 0;
    project(s.camera, {worldX, 0, 0}, 96, 96, px, py);
    // Reference for that floor point.
    const double d2 = lightHeight * lightHeight + worldX * worldX;
    const double dist = std::sqrt(d2);
    const double w = clamp01(1.0 - std::pow(dist / range, 4.0));
    const double atten = w * w / std::fmax(d2, 1e-2);
    PbrRef ref;
    const V3 N{0, 1, 0};
    const V3 pos{worldX, 0, 0};
    const V3 V = norm3(V3{0, 6, 0} - pos);
    const V3 L = norm3(V3{0, lightHeight, 0} - pos);
    return {img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py)).g, expectedByte(ref.light(N, V, L, 12.0 * atten).y)};
  };
  for (float x : {0.0F, 1.0F, 1.8F}) {  // the view spans +-2.18 at this camera height
    const auto [got, want] = run(2.0F, 20.0F, x);
    CHECK_MSG(near(got, want, 3), "x=" + std::to_string(x) + " got " + std::to_string(got) + " want " + std::to_string(want));
  }
  const auto [far, wantFar] = run(2.0F, 1.5F, 0.0F);  // floor point at distance 2 > range 1.5
  CHECK_EQ(far, 0);
  CHECK_EQ(wantFar, 0);
}

TEST(Shading, SpotLightConeFalloff) {
  GPU_OR_SKIP();
  RenderScene s = floorScene(8.0F);
  Light l;
  l.type = LightType::Spot;
  l.position = {0, 3, 0};
  l.direction = {0, -1, 0};
  l.innerConeRadians = radians(10.0F);
  l.outerConeRadians = radians(25.0F);
  l.intensity = 20.0F;
  l.range = 30.0F;
  s.lights.push_back(l);
  MaterialDesc d;
  d.baseColor = {0.8F, 0.8F, 0.8F, 1};
  s.items.push_back({mesh(rnd, makePlane(30, 30)), mat(rnd, d), Mat4::identity(), {}, true});
  const Image img = renderToImage(dev, rnd, s, 96, 96);
  REQUIRE(!img.px.empty());
  auto at = [&](float x) {
    int px = 0, py = 0;
    project(s.camera, {x, 0, 0}, 96, 96, px, py);
    return img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py)).g;
  };
  const int center = at(0.0F);
  const int inner = at(0.3F);    // atan(0.3/3) = 5.7 deg: full intensity region
  const int mid = at(1.0F);      // atan(1/3) = 18.4 deg: inside the falloff band
  const int outside = at(2.0F);  // atan(2/3) = 33.7 deg: beyond the outer cone
  CHECK(center > 100);
  CHECK(inner > 100);
  CHECK(mid > 0 && mid < inner);
  CHECK_EQ(outside, 0);
  // The cone is circular: the same radius along Z behaves the same.
  int px = 0, py = 0;
  project(s.camera, {0, 0, 2.0F}, 96, 96, px, py);
  CHECK_EQ(img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py)).g, 0);
}

TEST(Shading, NormalMapTiltsTheShading) {
  GPU_OR_SKIP();
  rhi::TextureDesc td;
  td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
  td.format = rhi::Format::RGBA8Unorm;
  const std::uint8_t texel[4] = {204, 128, 230, 255};  // tangent-space normal ~ (0.6, 0.0, 0.8)
  auto tex = dev.createTextureWithData(td, {texel, 4}, false);
  REQUIRE(tex.ok());

  auto shade = [&](bool useMap) {
    RenderScene s = floorScene();
    Light sun;
    sun.direction = {-1, -1, 0};
    sun.intensity = 3.14159F;
    s.lights.push_back(sun);
    MaterialDesc d;
    d.baseColor = {0.8F, 0.8F, 0.8F, 1};
    if (useMap) d.normalTexture = tex.value();
    s.items.push_back({mesh(rnd, makePlane(20, 20)), mat(rnd, d), Mat4::identity(), {}, true});
    return renderToImage(dev, rnd, s, 32, 32).at(16, 16);
  };
  const Rgba flat = shade(false);
  const Rgba mapped = shade(true);
  const V3 L = norm3({1, 1, 0});
  PbrRef ref;
  const double tx = 204.0 / 255 * 2 - 1, ty = 128.0 / 255 * 2 - 1, tz = 230.0 / 255 * 2 - 1;
  // Plane tangent T = +X, bitangent B = cross(N, T) = -Z, N = +Y.
  const V3 Nm = norm3(V3{tx, tz, -ty});
  const double wantFlat = ref.light({0, 1, 0}, {0, 1, 0}, L, 3.14159).x;
  const double wantMapped = ref.light(Nm, {0, 1, 0}, L, 3.14159).x;
  CHECK_MSG(near(flat.r, expectedByte(wantFlat), 2), "flat r=" + std::to_string(flat.r) + " want " + std::to_string(expectedByte(wantFlat)));
  CHECK_MSG(near(mapped.r, expectedByte(wantMapped), 2), "mapped r=" + std::to_string(mapped.r) + " want " + std::to_string(expectedByte(wantMapped)));
  CHECK(mapped.r > flat.r + 10);
}

TEST(Shading, TexturesModulateBaseColorAndRoughnessMetal) {
  GPU_OR_SKIP();
  rhi::TextureDesc td;
  td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
  td.format = rhi::Format::RGBA8Unorm;
  const std::uint8_t halfRed[4] = {128, 0, 0, 255};
  auto tex = dev.createTextureWithData(td, {halfRed, 4}, false);
  REQUIRE(tex.ok());
  RenderScene s = floorScene();
  MaterialDesc d;
  d.baseColor = {0, 0, 0, 1};
  d.emissive = {1, 1, 1};
  d.emissiveTexture = tex.value();  // emissive = (1,1,1) * (0.502, 0, 0)
  s.items.push_back({mesh(rnd, makePlane(20, 20)), mat(rnd, d), Mat4::identity(), {}, true});
  const Image img = renderToImage(dev, rnd, s, 32, 32);
  REQUIRE(!img.px.empty());
  CHECK(near(img.at(16, 16).r, expectedByte(128.0 / 255.0), 2));
  CHECK_EQ(img.at(16, 16).g, 0);
  CHECK_EQ(img.at(16, 16).b, 0);
}

// ---- Blending ---------------------------------------------------------------------------------------------

namespace {

std::shared_ptr<Material> emissiveMat(Renderer& r, Vec3 emissive, float alpha, AlphaMode mode) {
  MaterialDesc d;
  d.baseColor = {0, 0, 0, alpha};
  d.emissive = emissive;
  d.alphaMode = mode;
  return mat(r, d);
}

}  // namespace

TEST(Blend, AlphaBlendsInLinearSpace) {
  GPU_OR_SKIP();
  RenderScene s = floorScene();
  auto quad = mesh(rnd, makePlane(20, 20));
  s.items.push_back({quad, emissiveMat(rnd, {0.5F, 0, 0}, 1.0F, AlphaMode::Opaque), Mat4::identity(), {}, false});
  s.items.push_back({quad, emissiveMat(rnd, {0, 0, 0.5F}, 0.5F, AlphaMode::Blend), Mat4::translation({0, 0.5F, 0}), {}, false});
  const Image img = renderToImage(dev, rnd, s, 32, 32);
  REQUIRE(!img.px.empty());
  const double t = acesRef(0.5);
  const int want = toByte(0.5 * t / 1.0);  // toByte tone-maps again, so compute the encode directly:
  (void)want;
  const int wantEnc = static_cast<int>(std::lround(srgbEncode(0.5 * t) * 255.0));
  const Rgba p = img.at(16, 16);
  CHECK_MSG(near(p.r, wantEnc, 3), "r=" + std::to_string(p.r) + " want " + std::to_string(wantEnc));
  CHECK_MSG(near(p.b, wantEnc, 3), "b=" + std::to_string(p.b) + " want " + std::to_string(wantEnc));
  CHECK_EQ(p.g, 0);
  CHECK(rnd.stats().drawCalls == 2);
}

TEST(Blend, TransparentObjectsAreSortedBackToFrontRegardlessOfSceneOrder) {
  GPU_OR_SKIP();
  auto quad = mesh(rnd, makePlane(20, 20));
  auto green = emissiveMat(rnd, {0, 0.5F, 0}, 0.5F, AlphaMode::Blend);
  auto blue = emissiveMat(rnd, {0, 0, 0.5F}, 0.5F, AlphaMode::Blend);
  // Camera looks down: higher y is nearer. green is near (y=1), blue far (y=0).
  auto build = [&](bool greenFirst) {
    RenderScene s = floorScene();
    DrawItem g{quad, green, Mat4::translation({0, 1.0F, 0}), {}, false};
    DrawItem b{quad, blue, Mat4::identity(), {}, false};
    if (greenFirst) {
      s.items = {g, b};
    } else {
      s.items = {b, g};
    }
    return s;
  };
  const Image a = renderToImage(dev, rnd, build(true), 32, 32);
  const Image b = renderToImage(dev, rnd, build(false), 32, 32);
  REQUIRE(!a.px.empty() && !b.px.empty());
  CHECK_EQ(a.at(16, 16).g, b.at(16, 16).g);
  CHECK_EQ(a.at(16, 16).b, b.at(16, 16).b);
  // Far blue is drawn first (0.5 B over black), then near green over it: G has the larger weight.
  CHECK(a.at(16, 16).g > a.at(16, 16).b);
  const double t = acesRef(0.5);
  const int wantG = static_cast<int>(std::lround(srgbEncode(0.5 * t) * 255.0));
  const int wantB = static_cast<int>(std::lround(srgbEncode(0.25 * t) * 255.0));
  CHECK_MSG(near(a.at(16, 16).g, wantG, 3), "g=" + std::to_string(a.at(16, 16).g) + " want " + std::to_string(wantG));
  CHECK_MSG(near(a.at(16, 16).b, wantB, 3), "b=" + std::to_string(a.at(16, 16).b) + " want " + std::to_string(wantB));
}

TEST(Blend, DepthTestedAgainstOpaqueGeometry) {
  GPU_OR_SKIP();
  auto quad = mesh(rnd, makePlane(20, 20));
  RenderScene s = floorScene();
  // The transparent layer lies BELOW the opaque one, so the opaque surface must hide it.
  s.items.push_back({quad, emissiveMat(rnd, {0, 0, 0.5F}, 0.5F, AlphaMode::Blend), Mat4::identity(), {}, false});
  s.items.push_back({quad, emissiveMat(rnd, {0.5F, 0, 0}, 1.0F, AlphaMode::Opaque), Mat4::translation({0, 1.0F, 0}), {}, false});
  const Image img = renderToImage(dev, rnd, s, 32, 32);
  REQUIRE(!img.px.empty());
  CHECK_EQ(img.at(16, 16).b, 0);
  CHECK(img.at(16, 16).r > 100);
}

// ---- Skeletal animation on the GPU -------------------------------------------------------------------------

namespace {

// A tube along +Y from y=0 to y=2 with rings at y=0,1,2 (0.2 half-width). Ring 0 follows joint 0,
// ring 2 follows joint 1, the middle ring blends both.
MeshData makeBar() {
  MeshData m;
  const float h = 0.2F;
  const Vec3 corners[4] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
  for (int ring = 0; ring < 3; ++ring) {
    for (int c = 0; c < 4; ++c) {
      Vertex v;
      v.position = {corners[c].x, static_cast<float>(ring), corners[c].z};
      v.normal = normalize(Vec3{corners[c].x, 0, corners[c].z});
      v.uv = {static_cast<float>(c) / 4.0F, static_cast<float>(ring) / 2.0F};
      v.tangent = {0, 1, 0, 1};
      m.vertices.push_back(v);
      SkinVertex sv;
      if (ring == 0) {
        sv.joints[0] = 0;
        sv.weights[0] = 1;
      } else if (ring == 1) {
        sv.joints[0] = 0;
        sv.joints[1] = 1;
        sv.weights[0] = 0.5F;
        sv.weights[1] = 0.5F;
      } else {
        sv.joints[0] = 1;
        sv.weights[0] = 1;
      }
      m.skin.push_back(sv);
    }
  }
  for (std::uint32_t ring = 0; ring < 2; ++ring) {
    for (std::uint32_t c = 0; c < 4; ++c) {
      const std::uint32_t a = ring * 4 + c, b = ring * 4 + (c + 1) % 4, a2 = a + 4, b2 = b + 4;
      for (std::uint32_t i : {a, b, b2, a, b2, a2}) m.indices.push_back(i);
    }
  }
  return m;
}

Skeleton barSkeleton() {
  std::vector<Joint> j(2);
  j[0].name = "base";
  j[1].name = "elbow";
  j[1].parent = 0;
  j[1].bindLocal.translation = {0, 1, 0};
  return Skeleton::create(std::move(j)).value();
}

Camera sideCamera() {
  return Camera::lookAt({0, 1, 9}, {0, 1, 0}, {0, 1, 0}, radians(40.0F), 1.0F, 0.1F, 50.0F);
}

}  // namespace

TEST(Skinning, GpuMatchesCpuSkinnedGeometry) {
  GPU_OR_SKIP();
  const Skeleton skel = barSkeleton();
  auto bar = mesh(rnd, makeBar());
  REQUIRE(bar != nullptr);
  CHECK(bar->skinned());
  CHECK_EQ(bar->maxJointIndex(), 1u);

  MaterialDesc d;
  d.baseColor = {0, 0, 0, 1};
  d.emissive = {1, 1, 1};
  d.doubleSided = true;  // the tube is open and its winding is not what is under test
  auto white = mat(rnd, d);

  auto render = [&](float angleDeg) {
    auto pose = skel.bindPose();
    pose[1].rotation = Quat::fromAxisAngle({0, 0, 1}, radians(angleDeg));
    auto skin = computeSkinMatrices(skel, pose);
    RenderScene s;
    s.camera = sideCamera();
    s.ambient = {0, 0, 0};
    DrawItem it{bar, white, Mat4::identity(), skin.value(), false};
    s.items.push_back(it);
    return std::make_pair(s, renderToImage(dev, rnd, s, 128, 128));
  };

  const auto [bindScene, bindImg] = render(0.0F);
  REQUIRE(!bindImg.px.empty());
  auto lit = [](const Image& img, const Camera& cam, Vec3 p) {
    int px = 0, py = 0;
    project(cam, p, img.width, img.height, px, py);
    return luma(img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py))) > 128.0;
  };
  CHECK(lit(bindImg, bindScene.camera, {0, 1.8F, 0}));   // straight bar reaches up
  CHECK(lit(bindImg, bindScene.camera, {0, 0.3F, 0}));
  CHECK(!lit(bindImg, bindScene.camera, {-0.9F, 1.0F, 0}));

  const auto [bentScene, bentImg] = render(90.0F);
  REQUIRE(!bentImg.px.empty());
  // CPU reference: the elbow joint (0,1,0) rotates 90 degrees about Z, so the upper half points to -X.
  CHECK(!lit(bentImg, bentScene.camera, {0, 1.8F, 0}));  // the old top is now empty
  CHECK(lit(bentImg, bentScene.camera, {-0.8F, 1.0F, 0}));  // arm lies along -X at the elbow height
  CHECK(lit(bentImg, bentScene.camera, {0, 0.3F, 0}));      // lower half unchanged
  CHECK(!lit(bentImg, bentScene.camera, {0.8F, 1.0F, 0}));  // nothing on the other side
}

TEST(Skinning, SkinnedMeshesCastShadows) {
  GPU_OR_SKIP();
  const Skeleton skel = barSkeleton();
  auto bar = mesh(rnd, makeBar());
  auto plane = mesh(rnd, makePlane(12, 12));
  MaterialDesc floorDesc;
  floorDesc.baseColor = {0.8F, 0.8F, 0.8F, 1};
  auto floorMat = mat(rnd, floorDesc);
  MaterialDesc ghost;  // fully transparent: invisible, but it still casts a shadow
  ghost.baseColor = {0, 0, 0, 0};
  ghost.alphaMode = AlphaMode::Blend;
  ghost.doubleSided = true;
  auto ghostMat = mat(rnd, ghost);

  auto shade = [&](float angleDeg, Vec3 probe) {
    auto pose = skel.bindPose();
    pose[1].rotation = Quat::fromAxisAngle({0, 0, 1}, radians(angleDeg));
    RenderScene s = floorScene(8.0F);
    s.ambient = {0, 0, 0};
    Light sun;
    sun.direction = {0, -1, 0};
    sun.intensity = 3.14159F;
    sun.castShadow = true;
    s.lights.push_back(sun);
    s.items.push_back({plane, floorMat, Mat4::identity(), {}, true});
    s.items.push_back({bar, ghostMat, Mat4::identity(), computeSkinMatrices(skel, pose).value(), true});
    const Image img = renderToImage(dev, rnd, s, 128, 128);
    int px = 0, py = 0;
    project(s.camera, probe, 128, 128, px, py);
    return luma(img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py)));
  };
  // Bent 90 degrees the upper arm spans x in [-1.2, 0] at y ~ 1, so the floor at x = -0.7 is shadowed.
  const double bentShadow = shade(90.0F, {-0.7F, 0, 0});
  const double bentLit = shade(90.0F, {0.9F, 0, 0});
  // Straight, the same floor point is lit.
  const double straight = shade(0.0F, {-0.7F, 0, 0});
  CHECK_MSG(bentShadow < bentLit * 0.2, "bent shadow " + std::to_string(bentShadow) + " lit " + std::to_string(bentLit));
  CHECK_MSG(straight > bentLit * 0.9, "straight " + std::to_string(straight));
  CHECK(rnd.stats().shadowDrawCalls == 2);
}

// ---- Frames in flight -----------------------------------------------------------------------------------------

TEST(Frames, OverlappingFramesDoNotCorruptEachOthersData) {
  GPU_OR_SKIP();
  auto quad = mesh(rnd, makePlane(1, 1));  // a small quad, moved to a different place each frame
  constexpr int kFrames = 9;
  std::vector<std::shared_ptr<rhi::Texture>> targets;
  std::vector<RenderScene> scenes;
  std::vector<std::shared_ptr<Material>> mats;
  for (int i = 0; i < kFrames; ++i) {
    targets.push_back(makeTarget(dev, 64, 64));
    RenderScene s = floorScene();
    const float x = -1.2F + 0.3F * static_cast<float>(i);
    MaterialDesc d;
    d.baseColor = {0, 0, 0, 1};
    d.emissive = {0.1F + 0.1F * static_cast<float>(i), 0, 0};
    mats.push_back(mat(rnd, d));
    s.items.push_back({quad, mats.back(), Mat4::translation({x, 0, 0}), {}, false});
    scenes.push_back(s);
  }
  // Render everything back to back without reading anything in between.
  for (int i = 0; i < kFrames; ++i) {
    auto r = rnd.render(scenes[static_cast<std::size_t>(i)], *targets[static_cast<std::size_t>(i)]);
    REQUIRE(r.ok());
  }
  for (int i = 0; i < kFrames; ++i) {
    const Image img = readImage(dev, *targets[static_cast<std::size_t>(i)]);
    REQUIRE(!img.px.empty());
    int px = 0, py = 0;
    project(scenes[static_cast<std::size_t>(i)].camera, {-1.2F + 0.3F * static_cast<float>(i), 0, 0}, 64, 64, px, py);
    const int want = expectedByte(0.1 + 0.1 * i);
    const Rgba p = img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py));
    CHECK_MSG(near(p.r, want, 3), "frame " + std::to_string(i) + ": r=" + std::to_string(p.r) + " want " + std::to_string(want));
    // The quad of frame i must not appear at another frame's position.
    const int other = (i + 1) % kFrames;
    project(scenes[static_cast<std::size_t>(i)].camera, {-1.2F + 0.3F * static_cast<float>(other), 0, 0}, 64, 64, px, py);
    if (std::abs(other - i) > 1) CHECK_EQ(img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py)).r, 0);
  }
}

TEST(Frames, PipelinesAreCachedAcrossFrames) {
  GPU_OR_SKIP();
  RenderScene s = floorScene();
  s.items.push_back({mesh(rnd, makePlane(4, 4)), emissiveMat(rnd, {0.2F, 0.2F, 0.2F}, 1.0F, AlphaMode::Opaque), Mat4::identity(), {}, false});
  (void)renderToImage(dev, rnd, s, 32, 32);
  const auto created = rnd.stats().pipelinesCreated;
  for (int i = 0; i < 5; ++i) (void)renderToImage(dev, rnd, s, 32, 32);
  CHECK_EQ(rnd.stats().pipelinesCreated, created);
}

TEST(Frames, RenderTargetSizeCanChangeBetweenFrames) {
  GPU_OR_SKIP();
  RenderScene s = floorScene();
  s.items.push_back({mesh(rnd, makePlane(20, 20)), emissiveMat(rnd, {0.3F, 0.3F, 0.3F}, 1.0F, AlphaMode::Opaque), Mat4::identity(), {}, false});
  for (std::uint32_t size : {48U, 96U, 32U, 96U}) {
    const Image img = renderToImage(dev, rnd, s, size, size);
    REQUIRE(!img.px.empty());
    CHECK(near(img.at(size / 2, size / 2).r, expectedByte(0.3), 2));
  }
}

// ---- Error handling ---------------------------------------------------------------------------------------

TEST(Errors, InvalidScenesAreRejectedNotRendered) {
  GPU_OR_SKIP();
  auto target = makeTarget(dev, 32, 32);
  REQUIRE(target != nullptr);
  auto quad = mesh(rnd, makePlane(2, 2));
  auto bar = mesh(rnd, makeBar());
  auto m = emissiveMat(rnd, {1, 1, 1}, 1.0F, AlphaMode::Opaque);
  auto fails = [&](const RenderScene& s, ErrorCode code) {
    auto r = rnd.render(s, *target);
    CHECK(!r.ok());
    if (!r.ok()) CHECK_MSG(r.error().code == code, r.error().describe());
  };
  RenderScene base = floorScene();

  RenderScene noMesh = base;
  noMesh.items.push_back({nullptr, m, Mat4::identity(), {}, false});
  fails(noMesh, ErrorCode::InvalidArgument);

  RenderScene noMat = base;
  noMat.items.push_back({quad, nullptr, Mat4::identity(), {}, false});
  fails(noMat, ErrorCode::InvalidArgument);

  RenderScene skinnedNoMatrices = base;
  skinnedNoMatrices.items.push_back({bar, m, Mat4::identity(), {}, false});
  fails(skinnedNoMatrices, ErrorCode::InvalidArgument);

  RenderScene tooFewMatrices = base;
  tooFewMatrices.items.push_back({bar, m, Mat4::identity(), std::vector<Mat4>(1), false});  // bar uses joint 1
  fails(tooFewMatrices, ErrorCode::InvalidArgument);

  RenderScene staticWithMatrices = base;
  staticWithMatrices.items.push_back({quad, m, Mat4::identity(), std::vector<Mat4>(2), false});
  fails(staticWithMatrices, ErrorCode::InvalidArgument);

  RenderScene nanTransform = base;
  Mat4 bad;
  bad.m[12] = std::nanf("");
  nanTransform.items.push_back({quad, m, bad, {}, false});
  fails(nanTransform, ErrorCode::InvalidArgument);

  RenderScene twoSuns = base;
  twoSuns.lights = {Light{}, Light{}};
  fails(twoSuns, ErrorCode::InvalidArgument);

  RenderScene tooManyLights = base;
  Light point;
  point.type = LightType::Point;
  tooManyLights.lights.assign(kMaxPunctualLights + 1, point);
  fails(tooManyLights, ErrorCode::OutOfRange);

  RenderScene zeroDir = base;
  Light sun;
  sun.direction = {0, 0, 0};
  zeroDir.lights = {sun};
  fails(zeroDir, ErrorCode::InvalidArgument);

  RenderScene badSpot = base;
  Light spot;
  spot.type = LightType::Spot;
  spot.innerConeRadians = 0.8F;
  spot.outerConeRadians = 0.4F;
  badSpot.lights = {spot};
  fails(badSpot, ErrorCode::InvalidArgument);

  RenderScene nanCamera = base;
  nanCamera.camera.view.m[0] = std::nanf("");
  fails(nanCamera, ErrorCode::InvalidArgument);

  // Targets the renderer cannot draw into.
  rhi::TextureDesc td;
  td.width = td.height = 16;
  td.format = rhi::Format::RGBA16Float;
  td.usage = rhi::TextureUsage::ColorAttachment;
  auto hdr = dev.createTexture(td);
  REQUIRE(hdr.ok());
  CHECK(!rnd.render(base, *hdr.value()).ok());
  td.format = rhi::Format::RGBA8Srgb;
  td.usage = rhi::TextureUsage::Sampled;  // no ColorAttachment
  auto sampledOnly = dev.createTexture(td);
  REQUIRE(sampledOnly.ok());
  CHECK(!rnd.render(base, *sampledOnly.value()).ok());

  // After all those rejections the renderer still works.
  RenderScene good = base;
  good.items.push_back({quad, m, Mat4::identity(), {}, false});
  CHECK(rnd.render(good, *target).ok());
}

TEST(Errors, TooManyItemsForTheRendererConfiguration) {
  GPU_OR_SKIP();
  RendererDesc rd;
  rd.maxObjects = 2;
  rd.maxSkinMatrices = 4;
  rd.shadowMapSize = 64;
  auto small = Renderer::create(dev, rd);
  REQUIRE(small.ok());
  auto quad = small.value()->createMesh(makePlane(1, 1));
  auto bar = small.value()->createMesh(makeBar());
  auto m = small.value()->createMaterial({});
  REQUIRE(quad.ok() && bar.ok() && m.ok());
  auto target = makeTarget(dev, 16, 16);
  RenderScene s = floorScene();
  for (int i = 0; i < 3; ++i) s.items.push_back({quad.value(), m.value(), Mat4::identity(), {}, false});
  auto r = small.value()->render(s, *target);
  CHECK(!r.ok());
  if (!r.ok()) CHECK(r.error().code == ErrorCode::OutOfRange);
  RenderScene skinHeavy = floorScene();
  skinHeavy.items.push_back({bar.value(), m.value(), Mat4::identity(), std::vector<Mat4>(3), false});
  skinHeavy.items.push_back({bar.value(), m.value(), Mat4::identity(), std::vector<Mat4>(3), false});
  CHECK(!small.value()->render(skinHeavy, *target).ok());
  CHECK(!Renderer::create(dev, RendererDesc{.maxObjects = 0}).ok());
}

TEST(Errors, InvalidMeshAndMaterialData) {
  GPU_OR_SKIP();
  MeshData bad = makeBox({1, 1, 1});
  bad.indices[0] = 1000;
  CHECK(!rnd.createMesh(bad).ok());
  MaterialDesc nan;
  nan.roughness = std::nanf("");
  CHECK(!rnd.createMaterial(nan).ok());
}

TEST(Shading, NormalsUseInverseTransposeUnderNonUniformScale) {
  GPU_OR_SKIP();
  RenderScene s = floorScene(8.0F);
  Light sun;
  sun.direction = {-1.0F, -0.3F, 0};  // light arrives from +X, slightly above the horizon
  sun.intensity = 2.0F;
  s.lights.push_back(sun);
  MaterialDesc d;
  d.baseColor = {0.8F, 0.8F, 0.8F, 1};
  s.items.push_back({mesh(rnd, makeUvSphere(1.0F, 96, 48)), mat(rnd, d), Mat4::scale({2, 1, 1}), {}, false});
  const Image img = renderToImage(dev, rnd, s, 128, 128);
  REQUIRE(!img.px.empty());
  // Ellipsoid x^2/4 + y^2 = 1 at x = sqrt(2): y = sqrt(2)/2. True normal ~ (x/4, y, 0) = (0.354, 0.707).
  const V3 pos{std::sqrt(2.0), std::sqrt(2.0) / 2.0, 0};
  int px = 0, py = 0;
  REQUIRE(project(s.camera, {static_cast<float>(pos.x), static_cast<float>(pos.y), 0}, 128, 128, px, py));
  const V3 N = norm3({pos.x / 4.0, pos.y, 0});
  const V3 V = norm3(V3{0, 8, 0} - pos);
  const V3 L = norm3({1.0, 0.3, 0});
  PbrRef ref;
  const double want = ref.light(N, V, L, 2.0).x;
  const V3 wrongN = norm3({pos.x, pos.y, 0});  // what using the model matrix for normals would give
  const double wrong = ref.light(wrongN, V, L, 2.0).x;
  REQUIRE(std::abs(expectedByte(want) - expectedByte(wrong)) > 12);  // the case discriminates
  const Rgba p = img.at(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py));
  CHECK_MSG(near(p.r, expectedByte(want), 5), "r=" + std::to_string(p.r) + " want " + std::to_string(expectedByte(want)) +
                                                  " (wrong normals would give " + std::to_string(expectedByte(wrong)) + ")");
}

// ---- Golden image ---------------------------------------------------------------------------------------------
// A regression guard for the whole pipeline (shadows, PBR, point light, skinning, blending together).
// The reference was produced by this renderer on lavapipe after the analytic tests above passed and the
// image was inspected by eye: it detects *changes*, it does not prove correctness by itself. Regenerate with
// MINGOL_UPDATE_GOLDEN=1 only when a change in output is intended.

TEST(Golden, ComposedSceneMatchesReferenceImage) {
  GPU_OR_SKIP();
  const Skeleton skel = barSkeleton();
  RenderScene s;
  s.camera = Camera::lookAt({4.5F, 3.5F, 6.0F}, {0, 0.8F, 0}, {0, 1, 0}, radians(45.0F), 1.0F, 0.1F, 60.0F);
  s.clearColor[0] = 0.04F; s.clearColor[1] = 0.05F; s.clearColor[2] = 0.08F;
  s.ambient = {0.04F, 0.045F, 0.06F};
  Light sun;
  sun.direction = {-0.5F, -1.0F, -0.35F};
  sun.intensity = 3.0F;
  sun.color = {1.0F, 0.95F, 0.85F};
  sun.castShadow = true;
  Light lamp;
  lamp.type = LightType::Point;
  lamp.position = {-1.5F, 1.6F, 1.5F};
  lamp.color = {0.4F, 0.6F, 1.0F};
  lamp.intensity = 14.0F;
  lamp.range = 8.0F;
  s.lights = {sun, lamp};

  MaterialDesc floorD; floorD.baseColor = {0.55F, 0.55F, 0.5F, 1};
  MaterialDesc redD;   redD.baseColor = {0.8F, 0.12F, 0.1F, 1}; redD.roughness = 0.45F;
  MaterialDesc goldD;  goldD.baseColor = {1.0F, 0.77F, 0.34F, 1}; goldD.metallic = 1.0F; goldD.roughness = 0.3F;
  MaterialDesc glassD; glassD.baseColor = {0.3F, 0.7F, 0.9F, 0.35F}; glassD.alphaMode = AlphaMode::Blend; glassD.roughness = 0.2F;
  MaterialDesc armD;   armD.baseColor = {0.2F, 0.7F, 0.3F, 1}; armD.doubleSided = true; armD.roughness = 0.6F;

  auto pose = skel.bindPose();
  pose[1].rotation = Quat::fromAxisAngle({0, 0, 1}, radians(70.0F));
  s.items.push_back({mesh(rnd, makePlane(14, 14)), mat(rnd, floorD), Mat4::identity(), {}, true});
  s.items.push_back({mesh(rnd, makeBox({0.5F, 0.5F, 0.5F})), mat(rnd, redD),
                     Mat4::trs({-0.6F, 0.5F, -0.4F}, Quat::fromAxisAngle({0, 1, 0}, 0.5F), {1, 1, 1}), {}, true});
  s.items.push_back({mesh(rnd, makeUvSphere(0.6F, 48, 24)), mat(rnd, goldD), Mat4::translation({1.2F, 0.6F, 0.4F}), {}, true});
  s.items.push_back({mesh(rnd, makeBar()), mat(rnd, armD), Mat4::translation({0.2F, 0, 1.6F}),
                     computeSkinMatrices(skel, pose).value(), true});
  s.items.push_back({mesh(rnd, makeBox({0.6F, 0.6F, 0.05F})), mat(rnd, glassD), Mat4::translation({0.3F, 0.7F, 2.4F}), {}, false});

  const Image img = renderToImage(dev, rnd, s, 128, 128);
  REQUIRE(!img.px.empty());
  const std::string path = std::string(MINGOL_TEST_GOLDEN_DIR) + "/forward_scene_128.rgba";

  if (const char* upd = std::getenv("MINGOL_UPDATE_GOLDEN"); upd != nullptr && *upd == '1') {
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
      for (const Rgba& p : img.px) {
        const unsigned char c[4] = {static_cast<unsigned char>(p.r), static_cast<unsigned char>(p.g),
                                    static_cast<unsigned char>(p.b), static_cast<unsigned char>(p.a)};
        std::fwrite(c, 1, 4, f);
      }
      std::fclose(f);
    }
    writePpm(img, "golden_updated.ppm");
    SKIP_TEST("golden image regenerated (MINGOL_UPDATE_GOLDEN=1)");
  }

  FILE* f = std::fopen(path.c_str(), "rb");
  REQUIRE(f != nullptr);
  std::vector<unsigned char> ref(128 * 128 * 4);
  const std::size_t got = std::fread(ref.data(), 1, ref.size(), f);
  std::fclose(f);
  REQUIRE(got == ref.size());

  double sum = 0;
  int maxDiff = 0;
  std::size_t bigDiff = 0;
  for (std::size_t i = 0; i < img.px.size(); ++i) {
    const int dr = std::abs(img.px[i].r - ref[i * 4 + 0]);
    const int dg = std::abs(img.px[i].g - ref[i * 4 + 1]);
    const int db = std::abs(img.px[i].b - ref[i * 4 + 2]);
    sum += dr + dg + db;
    const int m = std::max({dr, dg, db});
    maxDiff = std::max(maxDiff, m);
    if (m > 24) ++bigDiff;
  }
  const double mean = sum / (3.0 * static_cast<double>(img.px.size()));
  if (mean > 1.0 || bigDiff > 40) writePpm(img, "golden_actual.ppm");
  CHECK_MSG(mean <= 1.0, "mean abs difference " + std::to_string(mean) + " (see golden_actual.ppm)");
  CHECK_MSG(bigDiff <= 40, std::to_string(bigDiff) + " pixels differ by more than 24 levels (max " + std::to_string(maxDiff) + ")");
}
