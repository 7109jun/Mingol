// SPDX-License-Identifier: GPL-2.0-only
// Built-in 5x8 (5x7 + descender row) bitmap font (ASCII 32..126). Non-ASCII code points render as a hollow box.
// No Hangul/CJK glyphs: this is a documented limitation of the built-in font.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace mingol::gui::font {

inline constexpr int kGlyphW = 5, kGlyphH = 8;
inline constexpr int kCellW = 6, kCellH = 9;  // glyph + 1px spacing
inline constexpr int kFirst = 32, kLast = 126;
inline constexpr int kAtlasCols = 16;
inline constexpr int kAtlasRows = 8;  // 128 cells; cell 0 = fallback box
inline constexpr int kAtlasW = kAtlasCols * kCellW;
inline constexpr int kAtlasH = kAtlasRows * kCellH;

// 8-bit coverage atlas, kAtlasW x kAtlasH. Cell index = codepoint - kFirst + 1; cell 0 = fallback.
const std::vector<std::uint8_t>& atlas();
int cellFor(char32_t cp);
// Decodes UTF-8 leniently: invalid bytes become U+FFFD, one per bad byte.
std::vector<char32_t> decode(std::string_view utf8);
inline float advance(int scale) { return static_cast<float>(kCellW * scale); }
inline float lineHeight(int scale) { return static_cast<float>((kCellH + 2) * scale); }

}  // namespace mingol::gui::font
