// SPDX-License-Identifier: GPL-2.0-only
#include "gui_util.hpp"

#include "mingol/gui/font.hpp"

namespace guitest {
std::vector<char32_t> Harness::font_decode(const std::string& s) { return mingol::gui::font::decode(s); }
}  // namespace guitest
