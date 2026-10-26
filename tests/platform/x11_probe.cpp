// SPDX-License-Identifier: GPL-2.0-only
#include "x11_probe.hpp"

#if defined(MINGOL_TEST_HAVE_XLIB)
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace mingol::test {

bool x11ProbeAvailable() { return true; }

bool x11ReadPixel(void* display, std::uintptr_t window, int x, int y, std::uint8_t rgb[3]) {
  auto* dpy = static_cast<Display*>(display);
  XSync(dpy, False);
  XImage* img = XGetImage(dpy, static_cast<Window>(window), x, y, 1, 1, AllPlanes, ZPixmap);
  if (img == nullptr) return false;
  const unsigned long p = XGetPixel(img, 0, 0);
  rgb[0] = static_cast<std::uint8_t>((p & img->red_mask) >> __builtin_ctzl(img->red_mask));
  rgb[1] = static_cast<std::uint8_t>((p & img->green_mask) >> __builtin_ctzl(img->green_mask));
  rgb[2] = static_cast<std::uint8_t>((p & img->blue_mask) >> __builtin_ctzl(img->blue_mask));
  XDestroyImage(img);
  return true;
}

}  // namespace mingol::test
#else
namespace mingol::test {
bool x11ProbeAvailable() { return false; }
bool x11ReadPixel(void*, std::uintptr_t, int, int, std::uint8_t*) { return false; }
}  // namespace mingol::test
#endif
