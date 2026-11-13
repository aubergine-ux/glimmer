#pragma once

#include <cstdint>

#include "cart/cart.hpp"
#include "ui/ui.hpp"

namespace glim::editors {

// Pixel access on the 128x256 strip holding both sprite sheets.
inline int sheet_px(const cart::Cart& c, int x, int y) {
  if (x < 0 || y < 0 || x >= kSheetW || y >= kSheetH * kSheets) return 0;
  return c.gfx[size_t(y) * kSheetW + size_t(x)] & 31;
}

inline void set_sheet_px(cart::Cart& c, int x, int y, int color) {
  if (x < 0 || y < 0 || x >= kSheetW || y >= kSheetH * kSheets) return;
  c.gfx[size_t(y) * kSheetW + size_t(x)] = uint8_t(color & 31);
}

// Draws `w`x`h` sheet pixels starting at (sx, sy) into `dst` at `scale`.
// Color 0 is drawn as `zero` so transparent pixels read as background.
void draw_sheet_area(ui::Ui& ui, const cart::Cart& c, int sx, int sy, int w, int h, int dx, int dy, int scale,
                     int zero);

// Draws sprite `n` (8x8) at (x, y) and scale, for tile pickers and previews.
inline void draw_sprite(ui::Ui& ui, const cart::Cart& c, int n, int x, int y, int scale, int zero) {
  draw_sheet_area(ui, c, (n % 16) * 8, (n / 16) * 8, 8, 8, x, y, scale, zero);
}

}  // namespace glim::editors
