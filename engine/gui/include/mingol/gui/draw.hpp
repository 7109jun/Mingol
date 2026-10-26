// SPDX-License-Identifier: GPL-2.0-only
// CPU-side 2D draw list: what every GUI frame is reduced to. Backends (software rasterizer, RHI)
// only consume this structure, so the widget code never touches a graphics API.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "mingol/core/math.hpp"

namespace mingol::gui {

// 0xAABBGGRR is avoided on purpose: colors are explicit 8-bit channels, non-premultiplied alpha.
struct Color {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;
  constexpr Color() = default;
  constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_ = 255)
      : r(r_), g(g_), b(b_), a(a_) {}
  friend constexpr bool operator==(Color x, Color y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
  constexpr Color withAlpha(std::uint8_t alpha) const { return {r, g, b, alpha}; }
  Color mix(Color o, float t) const;
};

struct Rect {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // [x0,x1) x [y0,y1)
  constexpr float width() const { return x1 - x0; }
  constexpr float height() const { return y1 - y0; }
  constexpr bool empty() const { return x1 <= x0 || y1 <= y0; }
  constexpr bool contains(Vec2 p) const { return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1; }
  constexpr Vec2 min() const { return {x0, y0}; }
  constexpr Vec2 max() const { return {x1, y1}; }
  Rect intersect(const Rect& o) const;
  Rect expanded(float d) const { return {x0 - d, y0 - d, x1 + d, y1 + d}; }
  friend constexpr bool operator==(const Rect& a, const Rect& b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
  }
};

struct Vertex {
  Vec2 pos;
  Vec2 uv;  // into the texture of the command; ignored when the command has no texture
  Color color;
};

// Texture ids: 0 = none (flat color), 1 = built-in font atlas, >= 16 = user textures (viewport).
inline constexpr std::uint32_t kTextureNone = 0;
inline constexpr std::uint32_t kTextureFont = 1;
inline constexpr std::uint32_t kTextureUserBase = 16;

struct DrawCmd {
  Rect clip;
  std::uint32_t texture = kTextureNone;
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
};

class DrawList {
 public:
  void clear();
  bool empty() const { return cmds_.empty(); }
  // Appends another list after this one (its commands draw on top). Clip stacks are not merged.
  void append(const DrawList& other);
  const std::vector<Vertex>& vertices() const { return vertices_; }
  const std::vector<std::uint32_t>& indices() const { return indices_; }
  const std::vector<DrawCmd>& commands() const { return cmds_; }

  void pushClip(const Rect& r);  // intersected with the current clip
  void popClip();
  Rect currentClip() const;

  void addRect(const Rect& r, Color c);
  void addRectRounded(const Rect& r, Color c, float radius);
  void addRectOutline(const Rect& r, Color c, float thickness = 1.0F);
  void addRectGradientV(const Rect& r, Color top, Color bottom);
  void addLine(Vec2 a, Vec2 b, Color c, float thickness = 1.0F);
  void addTriangle(Vec2 a, Vec2 b, Vec2 c, Color col);
  void addCircle(Vec2 center, float radius, Color c, int segments = 24);
  void addImage(std::uint32_t texture, const Rect& r, Vec2 uv0 = {0, 0}, Vec2 uv1 = {1, 1}, Color tint = {255, 255, 255});
  // Text uses the built-in 5x7 font scaled by `scale` (integer scale keeps it crisp).
  void addText(Vec2 pos, std::string_view utf8, Color c, int scale = 2);

 private:
  void addQuad(const Rect& r, Vec2 uv0, Vec2 uv1, Color c, std::uint32_t texture);
  DrawCmd& cmdFor(std::uint32_t texture);
  void addConvex(const std::vector<Vec2>& pts, Color c);

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<DrawCmd> cmds_;
  std::vector<Rect> clipStack_;
  Rect baseClip_{-1e9F, -1e9F, 1e9F, 1e9F};
};

}  // namespace mingol::gui
