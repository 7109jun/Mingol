// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace mingol::test {

// True when this build can read back X11 window contents (Linux with Xlib).
bool x11ProbeAvailable();

// Reads one pixel of an X11 window as the X server shows it. Returns false on failure.
// (Own translation unit: Xlib's macros such as None/Status collide with engine identifiers.)
bool x11ReadPixel(void* display, std::uintptr_t window, int x, int y, std::uint8_t rgb[3]);

}  // namespace mingol::test
