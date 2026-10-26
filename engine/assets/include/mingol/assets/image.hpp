// SPDX-License-Identifier: GPL-2.0-only
// PNG / JPEG decoding to 8-bit RGBA.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "mingol/core/error.hpp"

namespace mingol::assets {

struct Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;  // width * height * 4, row-major, top row first, straight alpha

  std::size_t byteSize() const noexcept { return rgba.size(); }
};

struct ImageLimits {
  std::uint32_t maxDimension = 16384;                // per side
  std::uint64_t maxDecodedBytes = 512ull << 20;      // protects against decompression bombs
};

enum class ImageFormat : std::uint8_t { Unknown, Png, Jpeg };

// Identifies the format from the leading bytes only (never trusts a file extension).
ImageFormat sniffImageFormat(std::span<const std::uint8_t> data) noexcept;

// Decodes PNG (all colour types, 1-16 bit, interlaced) or baseline/progressive JPEG. Dimensions
// and decoded size are checked against `limits` BEFORE any pixel memory is allocated. Fails on
// unknown formats, truncated or corrupt data. 16-bit PNGs are reduced to 8 bit.
Result<Image> decodeImage(std::span<const std::uint8_t> data, const ImageLimits& limits = {});

}  // namespace mingol::assets
