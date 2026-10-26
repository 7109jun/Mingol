// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/gui/raster.hpp"

#include <cmath>
#include <cstdio>

#include "mingol/gui/font.hpp"

namespace mingol::gui {

Image::Image(std::uint32_t w, std::uint32_t h, Color fill)
    : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4) {
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = fill.r; rgba[i + 1] = fill.g; rgba[i + 2] = fill.b; rgba[i + 3] = fill.a;
  }
}

Color Image::at(std::uint32_t x, std::uint32_t y) const {
  const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
  return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
}

void Image::set(std::uint32_t x, std::uint32_t y, Color c) {
  const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
  rgba[i] = c.r; rgba[i + 1] = c.g; rgba[i + 2] = c.b; rgba[i + 3] = c.a;
}

namespace {

constexpr int kSub = 16;  // sub-pixel precision (1/16 px); edge functions are exact in int64

struct P {
  std::int64_t x, y;
};

// Coordinates are clamped to +-32768 px so edge-function products (< 2^42) can never overflow int64.
// Geometry beyond that range is far outside any real target; only axis-aligned giants stay exact.
std::int64_t snap(float v) {
  if (!(v == v)) return 0;  // NaN
  const double c = std::clamp(static_cast<double>(v), -32768.0, 32768.0);
  return static_cast<std::int64_t>(std::llround(c * kSub));
}

// Edge function: >0 when p is to the left of a->b (counter-clockwise interior for positive area).
std::int64_t edge(P a, P b, P p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

// Top-left rule on a positively oriented triangle (y down => "left" and "top" per convention below).
bool isTopLeft(P a, P b) {
  const std::int64_t dx = b.x - a.x, dy = b.y - a.y;
  return (dy == 0 && dx > 0) || dy < 0;
}

struct Tex {
  const std::uint8_t* data = nullptr;
  std::uint32_t w = 0, h = 0;
  bool coverage = false;  // 1 byte per texel (font) vs RGBA
};

inline std::uint8_t blendChannel(unsigned src, unsigned dst, unsigned a) {
  return static_cast<std::uint8_t>((src * a + dst * (255 - a) + 127) / 255);
}

void drawTriangle(Image& img, const Rect& clip, const Vertex& v0, const Vertex& v1, const Vertex& v2, const Tex& tex) {
  P a{snap(v0.pos.x), snap(v0.pos.y)}, b{snap(v1.pos.x), snap(v1.pos.y)}, c{snap(v2.pos.x), snap(v2.pos.y)};
  const Vertex* va = &v0;
  const Vertex* vb = &v1;
  const Vertex* vc = &v2;
  std::int64_t area = edge(a, b, c);
  if (area == 0) return;
  if (area < 0) { std::swap(b, c); std::swap(vb, vc); area = -area; }

  const Rect bounds = clip.intersect({0, 0, static_cast<float>(img.width), static_cast<float>(img.height)});
  if (bounds.empty()) return;
  const std::int64_t minX = std::max<std::int64_t>({std::min({a.x, b.x, c.x}) / kSub - 1, 0});
  const std::int64_t minY = std::max<std::int64_t>({std::min({a.y, b.y, c.y}) / kSub - 1, 0});
  const std::int64_t maxX = std::min<std::int64_t>(std::max({a.x, b.x, c.x}) / kSub + 1, img.width - 1);
  const std::int64_t maxY = std::min<std::int64_t>(std::max({a.y, b.y, c.y}) / kSub + 1, img.height - 1);
  // Clip rect in whole pixels: a pixel is drawn when its center lies inside the clip rect.
  const std::int64_t cx0 = static_cast<std::int64_t>(std::ceil(static_cast<double>(bounds.x0) - 0.5));
  const std::int64_t cy0 = static_cast<std::int64_t>(std::ceil(static_cast<double>(bounds.y0) - 0.5));
  const std::int64_t cx1 = static_cast<std::int64_t>(std::ceil(static_cast<double>(bounds.x1) - 0.5)) - 1;
  const std::int64_t cy1 = static_cast<std::int64_t>(std::ceil(static_cast<double>(bounds.y1) - 0.5)) - 1;

  // Bias so that shared edges are rasterised exactly once.
  const std::int64_t bias0 = isTopLeft(b, c) ? 0 : -1;
  const std::int64_t bias1 = isTopLeft(c, a) ? 0 : -1;
  const std::int64_t bias2 = isTopLeft(a, b) ? 0 : -1;
  const double invArea = 1.0 / static_cast<double>(area);

  for (std::int64_t y = std::max(minY, cy0); y <= std::min(maxY, cy1); ++y) {
    for (std::int64_t x = std::max(minX, cx0); x <= std::min(maxX, cx1); ++x) {
      const P p{x * kSub + kSub / 2, y * kSub + kSub / 2};
      const std::int64_t w0 = edge(b, c, p), w1 = edge(c, a, p), w2 = edge(a, b, p);
      if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) continue;
      const double l0 = static_cast<double>(w0) * invArea, l1 = static_cast<double>(w1) * invArea,
                   l2 = static_cast<double>(w2) * invArea;
      double r = l0 * va->color.r + l1 * vb->color.r + l2 * vc->color.r;
      double g = l0 * va->color.g + l1 * vb->color.g + l2 * vc->color.g;
      double bl = l0 * va->color.b + l1 * vb->color.b + l2 * vc->color.b;
      double al = l0 * va->color.a + l1 * vb->color.a + l2 * vc->color.a;
      if (tex.data != nullptr) {
        const double u = l0 * va->uv.x + l1 * vb->uv.x + l2 * vc->uv.x;
        const double v = l0 * va->uv.y + l1 * vb->uv.y + l2 * vc->uv.y;
        const auto tx = static_cast<std::int64_t>(std::clamp(std::floor(u * tex.w), 0.0, static_cast<double>(tex.w) - 1));
        const auto ty = static_cast<std::int64_t>(std::clamp(std::floor(v * tex.h), 0.0, static_cast<double>(tex.h) - 1));
        const std::size_t ti = static_cast<std::size_t>(ty) * tex.w + static_cast<std::size_t>(tx);
        if (tex.coverage) {
          al *= tex.data[ti] / 255.0;
        } else {
          r *= tex.data[ti * 4] / 255.0;
          g *= tex.data[ti * 4 + 1] / 255.0;
          bl *= tex.data[ti * 4 + 2] / 255.0;
          al *= tex.data[ti * 4 + 3] / 255.0;
        }
      }
      const auto ia = static_cast<unsigned>(std::clamp(std::lround(al), 0L, 255L));
      if (ia == 0) continue;
      const std::size_t di = (static_cast<std::size_t>(y) * img.width + static_cast<std::size_t>(x)) * 4;
      auto* d = &img.rgba[di];
      d[0] = blendChannel(static_cast<unsigned>(std::clamp(std::lround(r), 0L, 255L)), d[0], ia);
      d[1] = blendChannel(static_cast<unsigned>(std::clamp(std::lround(g), 0L, 255L)), d[1], ia);
      d[2] = blendChannel(static_cast<unsigned>(std::clamp(std::lround(bl), 0L, 255L)), d[2], ia);
      d[3] = static_cast<std::uint8_t>(std::max<unsigned>(d[3], ia));
    }
  }
}

}  // namespace

void rasterize(const DrawList& list, Image& target, const UserTextureLookup& users) {
  const auto& vs = list.vertices();
  const auto& is = list.indices();
  for (const DrawCmd& cmd : list.commands()) {
    Tex tex;
    if (cmd.texture == kTextureFont) {
      tex = {font::atlas().data(), font::kAtlasW, font::kAtlasH, true};
    } else if (cmd.texture >= kTextureUserBase) {
      const Image* im = users ? users(cmd.texture) : nullptr;
      if (im == nullptr || im->width == 0 || im->height == 0) continue;
      tex = {im->rgba.data(), im->width, im->height, false};
    } else if (cmd.texture != kTextureNone) {
      continue;
    }
    for (std::uint32_t i = cmd.firstIndex; i + 2 < cmd.firstIndex + cmd.indexCount; i += 3)
      drawTriangle(target, cmd.clip, vs[is[i]], vs[is[i + 1]], vs[is[i + 2]], tex);
  }
}

bool writePpm(const Image& img, const char* path) {
  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
  for (std::size_t i = 0; i < img.rgba.size(); i += 4) std::fwrite(&img.rgba[i], 1, 3, f);
  return std::fclose(f) == 0;
}

}  // namespace mingol::gui
