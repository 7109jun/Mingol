// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstdlib>

#include "asset_test_util.hpp"
#include "mingol/assets/image.hpp"

using namespace mingol;
using namespace mingol::assets;
using namespace mingol::test;

namespace {
// The pattern gen_fixtures.py wrote.
void expectedPixel(int x, int y, int out[4]) {
  out[0] = x * 32;
  out[1] = y * 32;
  out[2] = (x + y) * 16;
  out[3] = 255 - x * 8 - y * 8;
}

bool matchesPattern(const Image& img, bool alpha) {
  if (img.width != 8 || img.height != 8 || img.rgba.size() != 8u * 8 * 4) return false;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      int e[4];
      expectedPixel(x, y, e);
      const std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(y) * 8 + x) * 4];
      if (p[0] != e[0] || p[1] != e[1] || p[2] != e[2]) return false;
      if (p[3] != (alpha ? e[3] : 255)) return false;
    }
  }
  return true;
}
}  // namespace

TEST(Image, PngRgbaIsExactAndTopRowFirst) {
  const auto bytes = fixture("rgba_8x8.png");
  CHECK(sniffImageFormat(bytes) == ImageFormat::Png);
  auto r = decodeImage(bytes);
  REQUIRE(r.ok());
  CHECK_MSG(matchesPattern(r.value(), true), "pixels differ from the generated pattern (channel swap or flip?)");
}

TEST(Image, PngRgbGetsOpaqueAlpha) {
  auto r = decodeImage(fixture("rgb_8x8.png"));
  REQUIRE(r.ok());
  CHECK(matchesPattern(r.value(), false));
}

TEST(Image, PngInterlacedMatchesNonInterlaced) {
  auto a = decodeImage(fixture("rgba_8x8.png"));
  auto b = decodeImage(fixture("rgba_8x8_interlaced.png"));
  REQUIRE(a.ok() && b.ok());
  CHECK(a.value().rgba == b.value().rgba);
}

TEST(Image, PngGrayscalePaletteAnd16Bit) {
  auto g = decodeImage(fixture("gray_8x8.png"));
  REQUIRE(g.ok());
  for (int i = 0; i < 64; ++i) {
    const std::uint8_t* p = &g.value().rgba[static_cast<std::size_t>(i) * 4];
    CHECK_EQ(static_cast<int>(p[0]), (i * 4) & 255);
    CHECK(p[0] == p[1] && p[1] == p[2] && p[3] == 255);
  }
  auto pal = decodeImage(fixture("palette_4x4.png"));
  REQUIRE(pal.ok());
  const int colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}};
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const std::uint8_t* p = &pal.value().rgba[(static_cast<std::size_t>(y) * 4 + x) * 4];
      const int* c = colors[(x + y) % 4];
      CHECK(p[0] == c[0] && p[1] == c[1] && p[2] == c[2]);
    }
  }
  auto g16 = decodeImage(fixture("gray16_8x8.png"));
  REQUIRE(g16.ok());
  for (int i = 0; i < 64; ++i) {
    const int expected = (i * 1000) >> 8;  // high byte
    CHECK_MSG(std::abs(static_cast<int>(g16.value().rgba[static_cast<std::size_t>(i) * 4]) - expected) <= 1,
              "16-bit pixel " + std::to_string(i));
  }
}

TEST(Image, JpegBaselineAndProgressiveAreCloseToSource) {
  for (const char* name : {"gradient_32.jpg", "gradient_32_progressive.jpg"}) {
    const auto bytes = fixture(name);
    CHECK(sniffImageFormat(bytes) == ImageFormat::Jpeg);
    auto r = decodeImage(bytes);
    CHECK_MSG(r.ok(), name);
    if (!r.ok()) continue;
    REQUIRE(r.value().width == 32 && r.value().height == 32);
    int worst = 0;
    for (int y = 0; y < 32; ++y) {
      for (int x = 0; x < 32; ++x) {
        const std::uint8_t* p = &r.value().rgba[(static_cast<std::size_t>(y) * 32 + x) * 4];
        worst = std::max({worst, std::abs(p[0] - x * 8), std::abs(p[1] - y * 8), std::abs(p[2] - 128)});
        CHECK_EQ(static_cast<int>(p[3]), 255);
      }
    }
    CHECK_MSG(worst <= 12, std::string(name) + ": max channel error " + std::to_string(worst));
  }
  auto s = decodeImage(fixture("solid_16.jpg"));
  REQUIRE(s.ok());
  const std::uint8_t* p = &s.value().rgba[(8 * 16 + 8) * 4];
  CHECK(std::abs(p[0] - 200) <= 3 && std::abs(p[1] - 30) <= 3 && std::abs(p[2] - 60) <= 3);
}

TEST(Image, RejectsUnknownEmptyAndWrongFormats) {
  CHECK(!decodeImage({}).ok());
  const std::vector<std::uint8_t> text = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
  auto r = decodeImage(text);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::Unsupported);
  const std::vector<std::uint8_t> gif = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0};
  CHECK(!decodeImage(gif).ok());
  CHECK(sniffImageFormat(gif) == ImageFormat::Unknown);
}

TEST(Image, TruncationAtEveryLengthFailsCleanly) {
  const auto bytes = fixture("rgba_8x8.png");
  // The decoder only needs the pixel data: a prefix that still contains every IDAT byte may decode
  // (it lacks only the IDAT CRC and IEND chunk = the last 16 bytes). Any shorter prefix is missing
  // pixel data and must fail.
  int tooEarly = 0;
  for (std::size_t n = 0; n + 16 < bytes.size(); ++n) {
    if (decodeImage(std::span<const std::uint8_t>(bytes.data(), n)).ok()) ++tooEarly;
  }
  CHECK_EQ(tooEarly, 0);
  const auto jpg = fixture("gradient_32.jpg");
  for (std::size_t n = 0; n + 2 < jpg.size(); n += 7) {
    (void)decodeImage(std::span<const std::uint8_t>(jpg.data(), n));  // must not crash (ASan); may or may not succeed
  }
}

TEST(Image, DecompressionBombIsRejectedBeforeAllocating) {
  auto bytes = fixture("rgba_8x8.png");
  // IHDR width/height live at offsets 16..23 (big endian). Claim 60000 x 60000 = 14 GB of RGBA.
  auto set32 = [&](std::size_t off, std::uint32_t v) {
    bytes[off] = static_cast<std::uint8_t>(v >> 24);
    bytes[off + 1] = static_cast<std::uint8_t>(v >> 16);
    bytes[off + 2] = static_cast<std::uint8_t>(v >> 8);
    bytes[off + 3] = static_cast<std::uint8_t>(v);
  };
  set32(16, 60000);
  set32(20, 60000);
  CHECK(!decodeImage(bytes).ok());  // refused by the decoder's own size check
  // Within the decoder's limits but over ours: rejected by the explicit dimension limit.
  auto wide = fixture("rgba_8x8.png");
  wide[18] = 0x4E; wide[19] = 0x20;  // width 20000
  wide[20] = wide[21] = wide[22] = 0; wide[23] = 4;  // height 4
  auto r = decodeImage(wide);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::OutOfRange);
  // Custom limits apply to honest images too.
  ImageLimits tight;
  tight.maxDimension = 4;
  auto r2 = decodeImage(fixture("rgba_8x8.png"), tight);
  REQUIRE(!r2.ok());
  CHECK(r2.error().code == ErrorCode::OutOfRange);
  ImageLimits bytesLimit;
  bytesLimit.maxDecodedBytes = 100;  // 8*8*4 = 256
  CHECK(!decodeImage(fixture("rgba_8x8.png"), bytesLimit).ok());
}

TEST(Image, RandomCorruptionNeverCrashes) {
  Lcg rng(12345);
  int ok = 0, bad = 0;
  for (const char* name : {"rgba_8x8.png", "palette_4x4.png", "rgba_8x8_interlaced.png", "gradient_32.jpg", "gradient_32_progressive.jpg"}) {
    const auto src = fixture(name);
    for (int i = 0; i < 1500; ++i) {
      auto m = mutate(src, rng);
      auto r = decodeImage(m);
      (r.ok() ? ok : bad)++;
      if (r.ok()) {
        CHECK(r.value().rgba.size() == static_cast<std::size_t>(r.value().width) * r.value().height * 4);
      }
    }
  }
  CHECK(bad > 0);  // the harness really corrupts something
  (void)ok;
}

TEST(Image, DamagedJpegOutputIsDeterministicAndNeverExposesHeapContents) {
  // stb_image leaves parts of the picture unwritten for some damaged JPEGs; the engine
  // zero-initialises its buffers so the result cannot depend on earlier heap contents.
  const auto src = fixture("gradient_32_progressive.jpg");
  Lcg rng(31337);
  int decoded = 0;
  for (int i = 0; i < 600; ++i) {
    const auto damaged = mutate(src, rng);
    auto a = decodeImage(damaged);
    std::vector<std::uint8_t> churn(1 << 16, 0xA5);  // dirty the heap between the two decodes
    auto b = decodeImage(damaged);
    CHECK_EQ(a.ok(), b.ok());
    if (a.ok() && b.ok()) {
      ++decoded;
      CHECK(a.value().rgba == b.value().rgba);
    }
  }
  CHECK(decoded > 0);
}
