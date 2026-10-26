// SPDX-License-Identifier: GPL-2.0-only
// Software rasterizer for DrawList. Used for headless rendering (tests, screenshots, CI) and as
// the reference the GPU backend is compared against.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "mingol/gui/draw.hpp"

namespace mingol::gui {

// Premultiplication-free RGBA8 framebuffer, row 0 at the top.
struct Image {
  std::uint32_t width = 0, height = 0;
  std::vector<std::uint8_t> rgba;
  Image() = default;
  Image(std::uint32_t w, std::uint32_t h, Color fill = {0, 0, 0, 255});
  Color at(std::uint32_t x, std::uint32_t y) const;
  void set(std::uint32_t x, std::uint32_t y, Color c);
};

// User texture lookup for ids >= kTextureUserBase (nullptr = missing -> triangle skipped).
using UserTextureLookup = std::function<const Image*(std::uint32_t id)>;

void rasterize(const DrawList& list, Image& target, const UserTextureLookup& users = {});

// Binary PPM (P6) writer for screenshots; alpha is dropped.
bool writePpm(const Image& img, const char* path);

}  // namespace mingol::gui
