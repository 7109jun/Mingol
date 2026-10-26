// SPDX-License-Identifier: GPL-2.0-only
#include <cstdlib>
#include <cstdio>
#include <random>
#include <string>

#include "mingol/gui/draw.hpp"
#include "mingol/gui/font.hpp"
#include "mingol/gui/raster.hpp"
#include "mingol_test.hpp"

using namespace mingol::gui;

namespace {
Image render(const DrawList& l, std::uint32_t w = 32, std::uint32_t h = 32, Color bg = {0, 0, 0}) {
  Image img(w, h, bg);
  rasterize(l, img);
  return img;
}
int countNot(const Image& img, Color bg) {
  int n = 0;
  for (std::uint32_t y = 0; y < img.height; ++y)
    for (std::uint32_t x = 0; x < img.width; ++x)
      if (!(img.at(x, y) == bg)) ++n;
  return n;
}
}  // namespace

TEST(GuiDraw, RectCoversExactPixels) {
  DrawList l;
  l.addRect({4, 5, 10, 9}, {255, 0, 0});
  const Image img = render(l);
  for (std::uint32_t y = 0; y < 32; ++y)
    for (std::uint32_t x = 0; x < 32; ++x) {
      const bool inside = x >= 4 && x < 10 && y >= 5 && y < 9;
      CHECK_MSG(img.at(x, y) == (inside ? Color{255, 0, 0} : Color{0, 0, 0}), "pixel " + std::to_string(x) + "," + std::to_string(y));
    }
}

TEST(GuiDraw, TranslucentQuadDiagonalNotDoubleBlended) {
  DrawList l;
  l.addRect({0, 0, 16, 16}, {255, 255, 255, 128});
  const Image img = render(l, 16, 16);
  const Color first = img.at(0, 0);
  CHECK_EQ(static_cast<int>(first.r), 128);
  for (std::uint32_t y = 0; y < 16; ++y)
    for (std::uint32_t x = 0; x < 16; ++x) CHECK(img.at(x, y) == first);
}

TEST(GuiDraw, AdjacentRectsTileWithoutGapsOrOverlap) {
  DrawList l;
  l.addRect({0, 0, 7, 16}, {255, 255, 255, 100});
  l.addRect({7, 0, 16, 16}, {255, 255, 255, 100});
  const Image img = render(l, 16, 16);
  for (std::uint32_t y = 0; y < 16; ++y)
    for (std::uint32_t x = 0; x < 16; ++x) CHECK(img.at(x, y) == img.at(0, 0));
}

TEST(GuiDraw, ClipRestrictsAndNests) {
  DrawList l;
  l.pushClip({2, 2, 10, 10});
  l.pushClip({6, 0, 20, 20});
  l.addRect({0, 0, 32, 32}, {0, 255, 0});
  l.popClip();
  l.addRect({0, 20, 4, 24}, {0, 0, 255});  // clipped to [2,10)^2 -> nothing visible
  l.popClip();
  const Image img = render(l);
  CHECK_EQ(countNot(img, {0, 0, 0}), 4 * 8);
  CHECK(img.at(6, 2) == (Color{0, 255, 0}));
  CHECK(img.at(5, 2) == (Color{0, 0, 0}));
  CHECK(img.at(10, 5) == (Color{0, 0, 0}));
}

TEST(GuiDraw, FullyClippedGeometryIsNotEmitted) {
  DrawList l;
  l.pushClip({0, 0, 4, 4});
  l.addRect({10, 10, 20, 20}, {1, 2, 3});
  CHECK(l.empty());
  CHECK(l.vertices().empty());
}

TEST(GuiDraw, CommandsMergeByStateAndSplitOnChange) {
  DrawList l;
  l.addRect({0, 0, 4, 4}, {1, 1, 1});
  l.addRect({4, 0, 8, 4}, {2, 2, 2});
  CHECK_EQ(l.commands().size(), 1U);
  l.addText({0, 8}, "A", {255, 255, 255}, 1);
  CHECK_EQ(l.commands().size(), 2U);
  l.pushClip({0, 0, 20, 20});
  l.addRect({0, 0, 2, 2}, {1, 1, 1});
  CHECK_EQ(l.commands().size(), 3U);
}

TEST(GuiDraw, TextRendersGlyphShape) {
  DrawList l;
  l.addText({1, 1}, "I", {255, 255, 255}, 1);
  const Image img = render(l, 8, 12);
  // 'I' = columns 00 41 7F 41 00 -> column 2 has rows 0..6 lit; column 1 and 3 rows 0 and 6.
  for (std::uint32_t r = 0; r < 7; ++r) CHECK(img.at(1 + 2, 1 + r) == (Color{255, 255, 255}));
  CHECK(img.at(1 + 1, 1) == (Color{255, 255, 255}));
  CHECK(img.at(1 + 3, 1 + 6) == (Color{255, 255, 255}));
  CHECK(img.at(1 + 1, 1 + 3) == (Color{0, 0, 0}));
  CHECK(img.at(1 + 0, 1) == (Color{0, 0, 0}));
}

TEST(GuiDraw, TextScaleIsExactIntegerMagnification) {
  DrawList a, b;
  a.addText({0, 0}, "Mingol 1!", {255, 255, 255}, 1);
  b.addText({0, 0}, "Mingol 1!", {255, 255, 255}, 3);
  const Image ia = render(a, 64, 12), ib = render(b, 192, 36);
  for (std::uint32_t y = 0; y < 36; ++y)
    for (std::uint32_t x = 0; x < 192; ++x) CHECK(ib.at(x, y) == ia.at(x / 3, y / 3));
}

TEST(GuiDraw, NewlineAndAdvance) {
  DrawList l;
  l.addText({0, 0}, "A\nA", {255, 255, 255}, 1);
  const Image img = render(l, 12, 24);
  bool top = false, second = false;
  for (std::uint32_t x = 0; x < 12; ++x) {
    top = top || img.at(x, 3) == (Color{255, 255, 255});
    second = second || img.at(x, 3 + 11) == (Color{255, 255, 255});
  }
  CHECK(top);
  CHECK(second);
  CHECK_EQ(font::advance(2), 12.0F);
}

TEST(GuiFont, DecodeUtf8) {
  using namespace font;
  auto d = decode("A\xEC\x95\x88\xF0\x9F\x98\x80");  // A, U+C548, U+1F600
  REQUIRE(d.size() == 3);
  CHECK(d[0] == U'A');
  CHECK(d[1] == 0xC548);
  CHECK(d[2] == 0x1F600);
  CHECK_EQ(decode("\xC0\xAF").size(), 2U);          // overlong -> two replacement chars
  CHECK_EQ(decode("\xED\xA0\x80").size(), 3U);      // surrogate
  CHECK_EQ(decode("\xE2\x82").size(), 2U);          // truncated
  CHECK_EQ(decode("\xFF").front(), char32_t{0xFFFD});
  CHECK_EQ(cellFor(U'A'), 'A' - 32 + 1);
  CHECK_EQ(cellFor(0xC548), 0);
  CHECK_EQ(cellFor(U'\n'), 0);
}

TEST(GuiFont, EveryAsciiGlyphHasInkExceptSpace) {
  const auto& a = font::atlas();
  for (int cp = font::kFirst; cp <= font::kLast; ++cp) {
    const int cell = font::cellFor(static_cast<char32_t>(cp));
    int ink = 0;
    for (int y = 0; y < font::kCellH; ++y)
      for (int x = 0; x < font::kCellW; ++x)
        ink += a[static_cast<std::size_t>((cell / font::kAtlasCols) * font::kCellH + y) * font::kAtlasW +
                 static_cast<std::size_t>((cell % font::kAtlasCols) * font::kCellW + x)] != 0;
    if (cp == ' ') CHECK_EQ(ink, 0); else CHECK_MSG(ink > 0, "glyph " + std::to_string(cp));
  }
  // distinct glyph shapes (no accidental duplicates in the table)
  for (int a1 = font::kFirst + 1; a1 <= font::kLast; ++a1)
    for (int b1 = a1 + 1; b1 <= font::kLast; ++b1) {
      bool same = true;
      const int ca = font::cellFor(static_cast<char32_t>(a1)), cb = font::cellFor(static_cast<char32_t>(b1));
      for (int y = 0; y < font::kCellH && same; ++y)
        for (int x = 0; x < font::kCellW && same; ++x)
          same = a[static_cast<std::size_t>((ca / font::kAtlasCols) * font::kCellH + y) * font::kAtlasW + static_cast<std::size_t>((ca % font::kAtlasCols) * font::kCellW + x)] ==
                 a[static_cast<std::size_t>((cb / font::kAtlasCols) * font::kCellH + y) * font::kAtlasW + static_cast<std::size_t>((cb % font::kAtlasCols) * font::kCellW + x)];
      CHECK_MSG(!same, "glyphs " + std::to_string(a1) + " and " + std::to_string(b1) + " identical");
    }
}

TEST(GuiDraw, RoundedRectCornersCutAndCenterFilled) {
  DrawList l;
  l.addRectRounded({0, 0, 20, 20}, {255, 255, 255}, 6);
  const Image img = render(l, 20, 20);
  CHECK(img.at(0, 0) == (Color{0, 0, 0}));
  CHECK(img.at(19, 19) == (Color{0, 0, 0}));
  CHECK(img.at(10, 10) == (Color{255, 255, 255}));
  CHECK(img.at(10, 0) == (Color{255, 255, 255}));
  CHECK(img.at(0, 10) == (Color{255, 255, 255}));
}

TEST(GuiDraw, LineCircleTriangleOutlineGradient) {
  DrawList l;
  l.addLine({2, 10}, {28, 10}, {255, 0, 0}, 2);
  const Image a = render(l);
  CHECK(a.at(15, 9) == (Color{255, 0, 0}));
  CHECK(a.at(15, 10) == (Color{255, 0, 0}));
  CHECK(a.at(15, 12) == (Color{0, 0, 0}));
  DrawList c;
  c.addCircle({16, 16}, 8, {0, 255, 0});
  const Image b = render(c);
  CHECK(b.at(16, 16) == (Color{0, 255, 0}));
  CHECK(b.at(1, 1) == (Color{0, 0, 0}));
  DrawList o;
  o.addRectOutline({4, 4, 12, 12}, {9, 9, 9}, 1);
  const Image oi = render(o);
  CHECK(oi.at(4, 4) == (Color{9, 9, 9}));
  CHECK(oi.at(8, 8) == (Color{0, 0, 0}));
  CHECK_EQ(countNot(oi, {0, 0, 0}), 28);
  DrawList g;
  g.addRectGradientV({0, 0, 4, 32}, {0, 0, 0}, {255, 255, 255});
  const Image gi = render(g, 4, 32);
  CHECK(gi.at(0, 0).r < gi.at(0, 16).r);
  CHECK(gi.at(0, 16).r < gi.at(0, 31).r);
}

TEST(GuiDraw, UserTextureSampledAndTinted) {
  Image tex(2, 1);
  tex.set(0, 0, {255, 0, 0});
  tex.set(1, 0, {0, 0, 255});
  DrawList l;
  l.addImage(kTextureUserBase, {0, 0, 8, 4});
  Image img(8, 4, {0, 0, 0});
  rasterize(l, img, [&](std::uint32_t id) { return id == kTextureUserBase ? &tex : nullptr; });
  CHECK(img.at(1, 1) == (Color{255, 0, 0}));
  CHECK(img.at(6, 1) == (Color{0, 0, 255}));
  Image none(8, 4, {0, 0, 0});
  rasterize(l, none);  // unknown texture: skipped, must not crash
  CHECK_EQ(countNot(none, {0, 0, 0}), 0);
}

TEST(GuiDraw, DegenerateAndHugeGeometrySafe) {
  DrawList l;
  l.addTriangle({1, 1}, {1, 1}, {1, 1}, {255, 255, 255});
  l.addTriangle({0, 0}, {1e7F, 0}, {0, 1e7F}, {10, 10, 10});
  l.addRect({-1e9F, -1e9F, 1e9F, 1e9F}, {5, 5, 5});
  l.addRect({3, 3, 3, 3}, {255, 255, 255});
  l.addLine({1, 1}, {1, 1}, {255, 255, 255});
  const Image img = render(l);
  CHECK(img.at(5, 5) == (Color{5, 5, 5}));
}

TEST(GuiDraw, RandomTrianglesNeverCrash) {
  std::mt19937 rng(123);
  std::uniform_real_distribution<float> d(-100.0F, 150.0F);
  for (int i = 0; i < 2000; ++i) {
    DrawList l;
    if (i % 3 == 0) l.pushClip({d(rng), d(rng), d(rng), d(rng)});
    l.addTriangle({d(rng), d(rng)}, {d(rng), d(rng)}, {d(rng), d(rng)},
                  {static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()), 7, static_cast<std::uint8_t>(rng())});
    l.addText({d(rng), d(rng)}, "fuzz\xFF text", {200, 200, 200}, 1 + static_cast<int>(rng() % 3));
    (void)render(l, 100, 100);
  }
}

TEST(GuiDraw, WritePpm) {
  DrawList l;
  l.addRect({0, 0, 2, 2}, {1, 2, 3});
  const Image img = render(l, 4, 4);
  const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/mingol_gui_test.ppm";
  REQUIRE(writePpm(img, path.c_str()));
  std::FILE* f = std::fopen(path.c_str(), "rb");
  REQUIRE(f != nullptr);
  char hdr[16] = {};
  CHECK(std::fgets(hdr, sizeof hdr, f) != nullptr);
  CHECK_EQ(std::string(hdr), std::string("P6\n"));
  std::fclose(f);
  std::remove(path.c_str());
  CHECK(!writePpm(img, "/nonexistent_dir/x.ppm"));
}
