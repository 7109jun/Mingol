// SPDX-License-Identifier: GPL-2.0-only
// Compiles the single-header third-party decoders. Built without the project's warning flags.
#include <cstdlib>
#include <cstring>

// stb_image can read its own uninitialised buffers when a JPEG is truncated or inconsistent
// (valgrind: stbi__YCbCr_to_RGB_simd on corrupt input), which would leak heap contents into the
// decoded picture. Zero-initialising every allocation makes the output deterministic and
// clean; the cost is negligible because calloc obtains fresh pages already zeroed.
static void* mingolStbiMalloc(std::size_t size) { return std::calloc(1, size == 0 ? 1 : size); }
static void* mingolStbiRealloc(void* p, std::size_t oldSize, std::size_t newSize) {
  void* n = std::realloc(p, newSize == 0 ? 1 : newSize);
  if (n != nullptr && newSize > oldSize) std::memset(static_cast<char*>(n) + oldSize, 0, newSize - oldSize);
  return n;
}
#define STBI_MALLOC(sz) mingolStbiMalloc(sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) mingolStbiRealloc(p, oldsz, newsz)
#define STBI_FREE(p) std::free(p)

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
