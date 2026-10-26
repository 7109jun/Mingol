// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/gui/draw.hpp"

#include <cmath>

#include "mingol/gui/font.hpp"

namespace mingol::gui {

Color Color::mix(Color o, float t) const {
  t = clamp(t, 0.0F, 1.0F);
  auto m = [t](std::uint8_t p, std::uint8_t q) {
    return static_cast<std::uint8_t>(std::lround(static_cast<float>(p) + (static_cast<float>(q) - static_cast<float>(p)) * t));
  };
  return {m(r, o.r), m(g, o.g), m(b, o.b), m(a, o.a)};
}

Rect Rect::intersect(const Rect& o) const {
  Rect r{std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
  if (r.x1 < r.x0) r.x1 = r.x0;
  if (r.y1 < r.y0) r.y1 = r.y0;
  return r;
}

void DrawList::clear() {
  vertices_.clear();
  indices_.clear();
  cmds_.clear();
  clipStack_.clear();
}

void DrawList::append(const DrawList& o) {
  const auto vbase = static_cast<std::uint32_t>(vertices_.size());
  const auto ibase = static_cast<std::uint32_t>(indices_.size());
  vertices_.insert(vertices_.end(), o.vertices_.begin(), o.vertices_.end());
  for (std::uint32_t i : o.indices_) indices_.push_back(i + vbase);
  for (DrawCmd c : o.cmds_) {
    c.firstIndex += ibase;
    cmds_.push_back(c);
  }
}

void DrawList::pushClip(const Rect& r) { clipStack_.push_back(r.intersect(currentClip())); }
void DrawList::popClip() {
  if (!clipStack_.empty()) clipStack_.pop_back();
}
Rect DrawList::currentClip() const { return clipStack_.empty() ? baseClip_ : clipStack_.back(); }

DrawCmd& DrawList::cmdFor(std::uint32_t texture) {
  const Rect clip = currentClip();
  if (!cmds_.empty()) {
    DrawCmd& last = cmds_.back();
    if (last.texture == texture && last.clip == clip &&
        last.firstIndex + last.indexCount == static_cast<std::uint32_t>(indices_.size()))
      return last;
  }
  cmds_.push_back({clip, texture, static_cast<std::uint32_t>(indices_.size()), 0});
  return cmds_.back();
}

void DrawList::addQuad(const Rect& r, Vec2 uv0, Vec2 uv1, Color c, std::uint32_t texture) {
  if (r.empty() || c.a == 0) return;
  if (r.intersect(currentClip()).empty()) return;
  DrawCmd& cmd = cmdFor(texture);
  const auto base = static_cast<std::uint32_t>(vertices_.size());
  vertices_.push_back({{r.x0, r.y0}, {uv0.x, uv0.y}, c});
  vertices_.push_back({{r.x1, r.y0}, {uv1.x, uv0.y}, c});
  vertices_.push_back({{r.x1, r.y1}, {uv1.x, uv1.y}, c});
  vertices_.push_back({{r.x0, r.y1}, {uv0.x, uv1.y}, c});
  for (std::uint32_t i : {0U, 1U, 2U, 0U, 2U, 3U}) indices_.push_back(base + i);
  cmd.indexCount += 6;
}

void DrawList::addRect(const Rect& r, Color c) { addQuad(r, {0, 0}, {0, 0}, c, kTextureNone); }

void DrawList::addRectGradientV(const Rect& r, Color top, Color bottom) {
  if (r.empty()) return;
  if (r.intersect(currentClip()).empty()) return;
  DrawCmd& cmd = cmdFor(kTextureNone);
  const auto base = static_cast<std::uint32_t>(vertices_.size());
  vertices_.push_back({{r.x0, r.y0}, {}, top});
  vertices_.push_back({{r.x1, r.y0}, {}, top});
  vertices_.push_back({{r.x1, r.y1}, {}, bottom});
  vertices_.push_back({{r.x0, r.y1}, {}, bottom});
  for (std::uint32_t i : {0U, 1U, 2U, 0U, 2U, 3U}) indices_.push_back(base + i);
  cmd.indexCount += 6;
}

void DrawList::addRectOutline(const Rect& r, Color c, float t) {
  addRect({r.x0, r.y0, r.x1, r.y0 + t}, c);
  addRect({r.x0, r.y1 - t, r.x1, r.y1}, c);
  addRect({r.x0, r.y0 + t, r.x0 + t, r.y1 - t}, c);
  addRect({r.x1 - t, r.y0 + t, r.x1, r.y1 - t}, c);
}

void DrawList::addConvex(const std::vector<Vec2>& pts, Color c) {
  if (pts.size() < 3 || c.a == 0) return;
  DrawCmd& cmd = cmdFor(kTextureNone);
  const auto base = static_cast<std::uint32_t>(vertices_.size());
  for (Vec2 p : pts) vertices_.push_back({p, {}, c});
  for (std::uint32_t i = 1; i + 1 < pts.size(); ++i) {
    indices_.push_back(base);
    indices_.push_back(base + i);
    indices_.push_back(base + i + 1);
  }
  cmd.indexCount += 3 * (static_cast<std::uint32_t>(pts.size()) - 2);
}

void DrawList::addRectRounded(const Rect& r, Color c, float radius) {
  if (r.empty()) return;
  radius = std::min({radius, r.width() * 0.5F, r.height() * 0.5F});
  if (radius < 1.0F) { addRect(r, c); return; }
  const int seg = std::max(3, static_cast<int>(radius));  // segments per corner
  std::vector<Vec2> pts;
  const Vec2 centers[4] = {{r.x1 - radius, r.y0 + radius}, {r.x1 - radius, r.y1 - radius},
                           {r.x0 + radius, r.y1 - radius}, {r.x0 + radius, r.y0 + radius}};
  for (int corner = 0; corner < 4; ++corner) {
    const float a0 = (-0.5F + 0.5F * static_cast<float>(corner)) * kPi;
    for (int i = 0; i <= seg; ++i) {
      const float a = a0 + 0.5F * kPi * static_cast<float>(i) / static_cast<float>(seg);
      pts.push_back({centers[corner].x + std::cos(a) * radius, centers[corner].y + std::sin(a) * radius});
    }
  }
  addConvex(pts, c);
}

void DrawList::addLine(Vec2 a, Vec2 b, Color c, float t) {
  const Vec2 d = b - a;
  const float len = std::sqrt(d.x * d.x + d.y * d.y);
  if (len < 1e-6F) return;
  const Vec2 n{-d.y / len * t * 0.5F, d.x / len * t * 0.5F};
  addConvex({a + n, b + n, b - n, a - n}, c);
}

void DrawList::addTriangle(Vec2 a, Vec2 b, Vec2 c, Color col) { addConvex({a, b, c}, col); }

void DrawList::addCircle(Vec2 center, float radius, Color c, int segments) {
  segments = std::max(segments, 3);
  std::vector<Vec2> pts;
  for (int i = 0; i < segments; ++i) {
    const float a = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(segments);
    pts.push_back({center.x + std::cos(a) * radius, center.y + std::sin(a) * radius});
  }
  addConvex(pts, c);
}

void DrawList::addImage(std::uint32_t texture, const Rect& r, Vec2 uv0, Vec2 uv1, Color tint) {
  addQuad(r, uv0, uv1, tint, texture);
}

void DrawList::addText(Vec2 pos, std::string_view utf8, Color c, int scale) {
  scale = std::max(scale, 1);
  const auto s = static_cast<float>(scale);
  float x = pos.x, y = pos.y;
  for (char32_t cp : font::decode(utf8)) {
    if (cp == U'\n') { x = pos.x; y += font::lineHeight(scale); continue; }
    if (cp != U' ') {
      const int cell = font::cellFor(cp);
      const float u0 = static_cast<float>((cell % font::kAtlasCols) * font::kCellW) / font::kAtlasW;
      const float v0 = static_cast<float>((cell / font::kAtlasCols) * font::kCellH) / font::kAtlasH;
      const float u1 = u0 + static_cast<float>(font::kGlyphW) / font::kAtlasW;
      const float v1 = v0 + static_cast<float>(font::kGlyphH) / font::kAtlasH;
      addQuad({x, y, x + font::kGlyphW * s, y + font::kGlyphH * s}, {u0, v0}, {u1, v1}, c, kTextureFont);
    }
    x += font::advance(scale);
  }
}

}  // namespace mingol::gui
