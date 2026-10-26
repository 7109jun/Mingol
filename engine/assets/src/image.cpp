// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/assets/image.hpp"

#include <limits>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

namespace mingol::assets {

ImageFormat sniffImageFormat(std::span<const std::uint8_t> d) noexcept {
  static constexpr std::uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (d.size() >= 8 && std::equal(png, png + 8, d.begin())) return ImageFormat::Png;
  if (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ImageFormat::Jpeg;
  return ImageFormat::Unknown;
}

Result<Image> decodeImage(std::span<const std::uint8_t> data, const ImageLimits& limits) {
  if (sniffImageFormat(data) == ImageFormat::Unknown) {
    return makeError(ErrorCode::Unsupported, "not a PNG or JPEG image");
  }
  if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return makeError(ErrorCode::InvalidArgument, "image data too large ({} bytes)", data.size());
  }
  const int len = static_cast<int>(data.size());
  int w = 0, h = 0, comp = 0;
  if (stbi_info_from_memory(data.data(), len, &w, &h, &comp) == 0 || w <= 0 || h <= 0) {
    return makeError(ErrorCode::Corrupt, "unreadable image header: {}", stbi_failure_reason());
  }
  const auto uw = static_cast<std::uint64_t>(w), uh = static_cast<std::uint64_t>(h);
  if (uw > limits.maxDimension || uh > limits.maxDimension) {
    return makeError(ErrorCode::OutOfRange, "image {}x{} exceeds the {} pixel side limit", w, h,
                     limits.maxDimension);
  }
  if (uw * uh * 4 > limits.maxDecodedBytes) {
    return makeError(ErrorCode::OutOfRange, "image {}x{} would need {} bytes (limit {})", w, h, uw * uh * 4,
                     limits.maxDecodedBytes);
  }
  stbi_uc* px = stbi_load_from_memory(data.data(), len, &w, &h, &comp, 4);
  if (px == nullptr) return makeError(ErrorCode::Corrupt, "image decode failed: {}", stbi_failure_reason());
  Image img;
  img.width = static_cast<std::uint32_t>(w);
  img.height = static_cast<std::uint32_t>(h);
  img.rgba.assign(px, px + uw * uh * 4);
  stbi_image_free(px);
  return img;
}

}  // namespace mingol::assets
