// SPDX-License-Identifier: GPL-2.0-only
// Shared fixture for GPU renderer tests: one validation-enabled device, offscreen render helper,
// and an independent CPU reference of the shading model.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mingol/renderer/renderer.hpp"
#include "mingol_test.hpp"

namespace mingol::test {

struct Rgba {
  int r = 0, g = 0, b = 0, a = 0;
};

struct Image {
  std::uint32_t width = 0, height = 0;
  std::vector<Rgba> px;  // row-major, top row first, always R,G,B,A order
  const Rgba& at(std::uint32_t x, std::uint32_t y) const {
    if (x >= width || y >= height) {
      fail(__FILE__, __LINE__, "Image::at(" + std::to_string(x) + "," + std::to_string(y) + ") out of bounds");
      static const Rgba none;
      return none;
    }
    return px[static_cast<std::size_t>(y) * width + x];
  }
};

struct Gpu {
  std::unique_ptr<rhi::Device> device;
  std::unique_ptr<renderer::Renderer> renderer;
  std::string skipReason;
};

inline Gpu& gpu() {
  static Gpu g = [] {
    Gpu r;
    rhi::DeviceDesc dd;
    dd.enableValidation = true;
    auto d = rhi::createVulkanDevice(dd);
    if (!d.ok()) {
      r.skipReason = "no usable Vulkan device: " + d.error().describe();
      return r;
    }
    r.device = std::move(d).value();
    renderer::RendererDesc rd;
    rd.shadowMapSize = 1024;  // keeps the software rasteriser quick; shadows are still sharp enough to test
    auto rr = renderer::Renderer::create(*r.device, rd);
    if (!rr.ok()) {
      r.skipReason = "renderer creation failed: " + rr.error().describe();
      r.device.reset();
      return r;
    }
    r.renderer = std::move(rr).value();
    return r;
  }();
  return g;
}

struct ValidationGuard {
  rhi::Device& device;
  std::uint64_t base;
  explicit ValidationGuard(rhi::Device& d) : device(d), base(d.validationErrorCount()) {}
  ~ValidationGuard() { CHECK_EQ(device.validationErrorCount(), base); }
};

#define GPU_OR_SKIP()                                    \
  if (!::mingol::test::gpu().device) SKIP_TEST(::mingol::test::gpu().skipReason); \
  rhi::Device& dev = *::mingol::test::gpu().device;      \
  [[maybe_unused]] renderer::Renderer& rnd = *::mingol::test::gpu().renderer; \
  ::mingol::test::ValidationGuard validationGuard(dev)

inline std::shared_ptr<rhi::Texture> makeTarget(rhi::Device& dev, std::uint32_t w, std::uint32_t h,
                                                rhi::Format fmt = rhi::Format::RGBA8Srgb) {
  rhi::TextureDesc t;
  t.width = w;
  t.height = h;
  t.format = fmt;
  t.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled;
  auto r = dev.createTexture(t);
  if (!r.ok()) {
    fail(__FILE__, __LINE__, "target creation: " + r.error().describe());
    return nullptr;
  }
  return std::move(r).value();
}

inline Image readImage(rhi::Device& dev, rhi::Texture& t) {
  Image img;
  img.width = t.desc().width;
  img.height = t.desc().height;
  auto bytes = dev.readbackTexture(t);
  if (!bytes.ok()) {
    fail(__FILE__, __LINE__, "readback: " + bytes.error().describe());
    return img;
  }
  const bool bgra = t.desc().format == rhi::Format::BGRA8Srgb || t.desc().format == rhi::Format::BGRA8Unorm;
  img.px.resize(static_cast<std::size_t>(img.width) * img.height);
  for (std::size_t i = 0; i < img.px.size(); ++i) {
    const std::uint8_t* p = &bytes.value()[i * 4];
    img.px[i] = bgra ? Rgba{p[2], p[1], p[0], p[3]} : Rgba{p[0], p[1], p[2], p[3]};
  }
  return img;
}

// Renders and reads back. Returns an empty image (and records a failure) on error.
inline Image renderToImage(rhi::Device& dev, renderer::Renderer& rnd, const renderer::RenderScene& scene,
                           std::uint32_t w = 96, std::uint32_t h = 96, rhi::Format fmt = rhi::Format::RGBA8Srgb) {
  auto target = makeTarget(dev, w, h, fmt);
  if (!target) return {};
  auto serial = rnd.render(scene, *target);
  if (!serial.ok()) {
    fail(__FILE__, __LINE__, "render: " + serial.error().describe());
    return {};
  }
  return readImage(dev, *target);
}

inline void writePpm(const Image& img, const std::string& path) {
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (const Rgba& p : img.px) {
      const unsigned char c[3] = {static_cast<unsigned char>(p.r), static_cast<unsigned char>(p.g), static_cast<unsigned char>(p.b)};
      std::fwrite(c, 1, 3, f);
    }
    std::fclose(f);
  }
}

// ---- CPU reference of the shading model (double precision, written from the published formulas) ---

struct V3 {
  double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 norm3(V3 a) {
  const double l = std::sqrt(dot3(a, a));
  return {a.x / l, a.y / l, a.z / l};
}
inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

inline double acesRef(double x) {
  return clamp01((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
inline double srgbEncode(double c) {
  c = clamp01(c);
  return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}
// Final 8-bit value for a linear scene colour as written to an sRGB target.
inline int toByte(double linear) { return static_cast<int>(std::lround(srgbEncode(acesRef(linear)) * 255.0)); }

struct PbrRef {
  V3 albedo{0.8, 0.8, 0.8};
  double metallic = 0, roughness = 1;
  // Returns linear (pre-tonemap) radiance contribution of one light with radiance multiplier E.
  V3 light(V3 N, V3 V, V3 L, double E) const {
    const double pi = 3.14159265358979323846;
    const double rough = std::fmax(std::fmin(roughness, 1.0), 0.045);
    const double a = rough * rough, a2 = a * a;
    const double NL = dot3(N, L);
    if (NL <= 0) return {};
    const V3 H = norm3(V + L);
    const double NV = std::fmax(dot3(N, V), 1e-4), NH = clamp01(dot3(N, H)), VH = clamp01(dot3(V, H));
    const double d = NH * NH * (a2 - 1) + 1;
    const double D = a2 / (pi * d * d);
    const double gv = NL * std::sqrt(NV * NV * (1 - a2) + a2);
    const double gl = NV * std::sqrt(NL * NL * (1 - a2) + a2);
    const double Vis = 0.5 / std::fmax(gv + gl, 1e-5);
    const double fw = std::pow(clamp01(1 - VH), 5);
    V3 f0 = V3{0.04, 0.04, 0.04} * (1 - metallic) + albedo * metallic;
    V3 F = f0 + (V3{1, 1, 1} - f0) * fw;
    V3 diffuseColor = albedo * (1 - metallic);
    V3 out;
    out.x = ((1 - F.x) * diffuseColor.x / pi + D * Vis * F.x) * E * NL;
    out.y = ((1 - F.y) * diffuseColor.y / pi + D * Vis * F.y) * E * NL;
    out.z = ((1 - F.z) * diffuseColor.z / pi + D * Vis * F.z) * E * NL;
    return out;
  }
};

inline bool near(int a, int b, int tol) { return std::abs(a - b) <= tol; }

// Brightness (mean of RGB) of a pixel.
inline double luma(const Rgba& p) { return (p.r + p.g + p.b) / 3.0; }

}  // namespace mingol::test

namespace mingol::test {

// Projects a world point to pixel coordinates (origin top-left) for a camera and target size.
inline bool project(const renderer::Camera& cam, Vec3 p, std::uint32_t w, std::uint32_t h, int& px, int& py) {
  const Vec4 c = (cam.projection * cam.view) * Vec4(p, 1.0F);
  if (c.w <= 0) return false;
  const float x = c.x / c.w, y = c.y / c.w;
  px = static_cast<int>(std::floor((x * 0.5F + 0.5F) * static_cast<float>(w)));
  py = static_cast<int>(std::floor((y * 0.5F + 0.5F) * static_cast<float>(h)));
  return true;
}

}  // namespace mingol::test
