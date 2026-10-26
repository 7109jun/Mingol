// SPDX-License-Identifier: GPL-2.0-only
// GameApp with a renderer, and the shipped player executable end to end (project -> archive -> single exe).
#include <cstdlib>
#include <cmath>

#include "player_util.hpp"
#include "render_test_util.hpp"

using namespace mingol;
using namespace mingol::test;

namespace {

// Pixels that differ clearly from the top-left (background) pixel: the box and its shading.
int litPixels(const Image& img) {
  if (img.px.empty()) return 0;
  const Rgba bg = img.px[0];
  int n = 0;
  for (const Rgba& p : img.px) if (std::abs(p.r - bg.r) + std::abs(p.g - bg.g) + std::abs(p.b - bg.b) > 40) ++n;
  return n;
}

// Reads a binary PPM written by the player and counts non-dark pixels.
int ppmLitPixels(const std::filesystem::path& p, std::uint32_t* w = nullptr, std::uint32_t* h = nullptr) {
  auto bytes = fs::readFile(p);
  if (!bytes.ok()) return -1;
  const std::string s(bytes.value().begin(), bytes.value().end());
  unsigned W = 0, H = 0, mx = 0;
  int off = 0;
  if (std::sscanf(s.c_str(), "P6 %u %u %u%n", &W, &H, &mx, &off) != 3) return -1;
  ++off;  // single whitespace after maxval
  if (w) *w = W;
  if (h) *h = H;
  int n = 0;
  const auto px = [&s](std::size_t i, int c) { return static_cast<int>(static_cast<unsigned char>(s[i + static_cast<std::size_t>(c)])); };
  const std::size_t start = static_cast<std::size_t>(off);
  for (std::size_t i = start; i + 2 < s.size(); i += 3)
    if (std::abs(px(i, 0) - px(start, 0)) + std::abs(px(i, 1) - px(start, 1)) + std::abs(px(i, 2) - px(start, 2)) > 40) ++n;
  return n;
}

}  // namespace

TEST(PlayerGpu, RendersTheEntrySceneFromAnArchive) {
  GPU_OR_SKIP();
  TempDir dir;
  const auto mindev = buildProject(dir, &dev, &rnd);
  REQUIRE(!mindev.empty());
  auto bytes = package::packProject(mindev, {});
  REQUIRE(bytes.ok());
  const auto arc = dir.path / "game.mingol";
  REQUIRE(fs::writeFileAtomic(arc, bytes.value()).ok());
  auto src = player::openArchiveSource(arc);
  REQUIRE(src.ok());
  player::GameAppDesc gd;
  gd.device = &dev;
  gd.renderer = &rnd;
  auto app = player::GameApp::create(std::move(src).value(), gd);
  REQUIRE(app.ok());
  CHECK(app.value()->assets().mesh("builtin/box") != nullptr);
  auto* box = app.value()->scene().find("Box");
  REQUIRE(box != nullptr);
  CHECK(box->getComponent<scene::MeshRenderer>()->mesh == app.value()->assets().mesh("builtin/box"));
  app.value()->tick(1.0F / 60.0F);

  auto target = makeTarget(dev, 128, 96);
  REQUIRE(target != nullptr);
  REQUIRE(app.value()->render(*target).ok());
  const Image img = readImage(dev, *target);
  CHECK_MSG(litPixels(img) > 200, "rendered image is mostly black: " + std::to_string(litPixels(img)));
}

TEST(PlayerGpu, SingleExecutableRunsHeadlessAndRenders) {
  GPU_OR_SKIP();
  TempDir dir;
  const auto mindev = buildProject(dir, &dev, &rnd);
  REQUIRE(!mindev.empty());
  auto bytes = package::packProject(mindev, {});
  REQUIRE(bytes.ok());
  const auto arc = dir.path / "game.mingol";
  REQUIRE(fs::writeFileAtomic(arc, bytes.value()).ok());
  const std::filesystem::path exe = dir.path / "game";
  REQUIRE(package::packExecutable(MINGOL_PLAYER_EXE, arc, exe).ok());

  const auto shot = dir.path / "shot.ppm";
  const std::string cmd = "\"" + exe.string() + "\" --headless --frames 5 --width 160 --height 120 --screenshot \"" + shot.string() + "\" 2> \"" + (dir.path / "err.txt").string() + "\"";
  const int rc = std::system(cmd.c_str());
  CHECK_EQ(rc, 0);
  std::uint32_t w = 0, h = 0;
  const int lit = ppmLitPixels(shot, &w, &h);
  CHECK_EQ(w, 160u);
  CHECK_EQ(h, 120u);
  CHECK_MSG(lit > 200, "player screenshot is mostly black: " + std::to_string(lit));

  // the same game straight from the project file, and a wrong path fails cleanly
  const std::string cmd2 = "\"" MINGOL_PLAYER_EXE "\" \"" + mindev.string() + "\" --headless --frames 2 2> \"" + (dir.path / "err2.txt").string() + "\"";
  CHECK_EQ(std::system(cmd2.c_str()), 0);
  const std::string cmd3 = "\"" MINGOL_PLAYER_EXE "\" \"" + (dir.path / "missing.mingol").string() + "\" --headless --frames 1 2> \"" + (dir.path / "err3.txt").string() + "\"";
  CHECK(std::system(cmd3.c_str()) != 0);
}
