#include "gfx/gfx.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

#include "gfx/font.hpp"
#include "gfx/palette.hpp"

namespace glim::gfx {

namespace {
constexpr uint16_t kCellIdMask = 0x01FF;
constexpr uint16_t kCellFlipX = 0x0200;
constexpr uint16_t kCellFlipY = 0x0400;
}  // namespace

void Gfx::reset_state() {
  for (int i = 0; i < kColors; ++i) {
    mem_.poke(addr::kDrawPal + i, uint8_t(i));
    mem_.poke(addr::kScreenPal + i, uint8_t(i));
  }
  mem_.poke32(addr::kTransparent, 1);
  clip();
  camera(0, 0);
  mem_.poke(addr::kPen, 31);
  fillp(0);
  mem_.pokes16(addr::kCursor, 0);
  mem_.pokes16(addr::kCursor + 2, 0);
}

void Gfx::reset_palette() {
  for (int i = 0; i < kColors; ++i) {
    uint32_t rgb = kDefaultPalette[i];
    mem_.poke(addr::kPalette + i * 3 + 0, uint8_t(rgb >> 16));
    mem_.poke(addr::kPalette + i * 3 + 1, uint8_t(rgb >> 8));
    mem_.poke(addr::kPalette + i * 3 + 2, uint8_t(rgb));
  }
}

Gfx::ClipRect Gfx::clip_rect() const {
  return {mem_.peeks16(addr::kClip), mem_.peeks16(addr::kClip + 2), mem_.peeks16(addr::kClip + 4),
          mem_.peeks16(addr::kClip + 6)};
}

void Gfx::plot(int x, int y, int c) {
  auto cr = clip_rect();
  if (x < cr.x0 || y < cr.y0 || x >= cr.x1 || y >= cr.y1) return;
  uint16_t pat = mem_.peek16(addr::kFillPattern);
  if (pat && ((pat >> (15 - ((y & 3) * 4 + (x & 3)))) & 1)) {
    uint8_t alt = mem_.peek(addr::kFillAlt);
    if (alt == 0xFF) return;
    c = alt;
  }
  screen()[y * kScreenW + x] = map_color(c);
  ++work_;
}

void Gfx::span(int x0, int x1, int y, int c) {
  if (x0 > x1) std::swap(x0, x1);
  auto cr = clip_rect();
  if (y < cr.y0 || y >= cr.y1) return;
  x0 = std::max(x0, cr.x0);
  x1 = std::min(x1, cr.x1 - 1);
  if (x0 > x1) return;
  if (mem_.peek16(addr::kFillPattern) == 0) {
    std::fill_n(screen() + y * kScreenW + x0, x1 - x0 + 1, map_color(c));
    work_ += x1 - x0 + 1;
    return;
  }
  for (int x = x0; x <= x1; ++x) plot(x, y, c);
}

void Gfx::cls(int c) {
  std::fill_n(screen(), kScreenW * kScreenH, uint8_t(c & 31));
  mem_.pokes16(addr::kCursor, 0);
  mem_.pokes16(addr::kCursor + 2, 0);
  work_ += kScreenW * kScreenH / 16;
}

void Gfx::pset(int x, int y, int c) { plot(x - cam_x(), y - cam_y(), c); }

int Gfx::pget(int x, int y) const {
  x -= cam_x();
  y -= cam_y();
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) return 0;
  return mem_.peek(addr::kScreen + y * kScreenW + x) & 31;
}

void Gfx::line(int x0, int y0, int x1, int y1, int c) {
  x0 -= cam_x();
  x1 -= cam_x();
  y0 -= cam_y();
  y1 -= cam_y();
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    plot(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Gfx::rect(int x0, int y0, int x1, int y1, int c) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  x0 -= cam_x();
  x1 -= cam_x();
  y0 -= cam_y();
  y1 -= cam_y();
  span(x0, x1, y0, c);
  if (y1 != y0) span(x0, x1, y1, c);
  for (int y = y0 + 1; y < y1; ++y) {
    plot(x0, y, c);
    if (x1 != x0) plot(x1, y, c);
  }
}

void Gfx::rectfill(int x0, int y0, int x1, int y1, int c) {
  if (y0 > y1) std::swap(y0, y1);
  auto cr = clip_rect();
  int top = std::max(y0 - cam_y(), cr.y0), bottom = std::min(y1 - cam_y(), cr.y1 - 1);
  for (int y = top; y <= bottom; ++y) span(x0 - cam_x(), x1 - cam_x(), y, c);
}

void Gfx::circ(int cx, int cy, int r, int c) {
  if (r < 0) return;
  cx -= cam_x();
  cy -= cam_y();
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    plot(cx + x, cy + y, c);
    plot(cx - x, cy + y, c);
    plot(cx + x, cy - y, c);
    plot(cx - x, cy - y, c);
    plot(cx + y, cy + x, c);
    plot(cx - y, cy + x, c);
    plot(cx + y, cy - x, c);
    plot(cx - y, cy - x, c);
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}

void Gfx::circfill(int cx, int cy, int r, int c) {
  if (r < 0) return;
  cx -= cam_x();
  cy -= cam_y();
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    // Spans can be drawn twice where octants meet; overdraw is harmless here.
    span(cx - x, cx + x, cy + y, c);
    span(cx - x, cx + x, cy - y, c);
    span(cx - y, cx + y, cy + x, c);
    span(cx - y, cx + y, cy - x, c);
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}

namespace {
struct OvalRows {
  int top;
  std::vector<std::pair<int, int>> rows;  // [left, right] per row
};

OvalRows oval_rows(int x0, int y0, int x1, int y1) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
  double a = (x1 - x0) / 2.0 + 0.5, b = (y1 - y0) / 2.0 + 0.5;
  OvalRows out{y0, {}};
  out.rows.reserve(y1 - y0 + 1);
  for (int y = y0; y <= y1; ++y) {
    double t = (y - cy) / b;
    double hw = a * std::sqrt(std::max(0.0, 1.0 - t * t));
    int l = int(std::ceil(cx - hw - 1e-9)), r = int(std::floor(cx + hw + 1e-9));
    if (l > r) l = r = int(std::lround(cx));
    out.rows.emplace_back(l, r);
  }
  return out;
}
}  // namespace

void Gfx::oval(int x0, int y0, int x1, int y1, int c) {
  auto o = oval_rows(x0 - cam_x(), y0 - cam_y(), x1 - cam_x(), y1 - cam_y());
  int n = int(o.rows.size());
  for (int i = 0; i < n; ++i) {
    auto [l, r] = o.rows[i];
    int y = o.top + i;
    if (i == 0 || i == n - 1) {
      span(l, r, y, c);
      continue;
    }
    // Extend each edge toward the neighbouring rows so steep parts stay connected.
    int inner_l = std::max(o.rows[i - 1].first, o.rows[i + 1].first);
    int inner_r = std::min(o.rows[i - 1].second, o.rows[i + 1].second);
    int le = std::clamp(inner_l - 1, l, r), rs = std::clamp(inner_r + 1, l, r);
    if (rs <= le) {
      span(l, r, y, c);
    } else {
      span(l, le, y, c);
      span(rs, r, y, c);
    }
  }
}

void Gfx::ovalfill(int x0, int y0, int x1, int y1, int c) {
  auto o = oval_rows(x0 - cam_x(), y0 - cam_y(), x1 - cam_x(), y1 - cam_y());
  for (size_t i = 0; i < o.rows.size(); ++i) span(o.rows[i].first, o.rows[i].second, o.top + int(i), c);
}

void Gfx::tri(int x0, int y0, int x1, int y1, int x2, int y2, int c) {
  line(x0, y0, x1, y1, c);
  line(x1, y1, x2, y2, c);
  line(x2, y2, x0, y0, c);
}

void Gfx::trifill(int x0, int y0, int x1, int y1, int x2, int y2, int c) {
  x0 -= cam_x();
  x1 -= cam_x();
  x2 -= cam_x();
  y0 -= cam_y();
  y1 -= cam_y();
  y2 -= cam_y();
  if (y0 > y1) std::swap(x0, x1), std::swap(y0, y1);
  if (y1 > y2) std::swap(x1, x2), std::swap(y1, y2);
  if (y0 > y1) std::swap(x0, x1), std::swap(y0, y1);
  auto cr = clip_rect();
  if (y0 == y2) {
    span(std::min({x0, x1, x2}), std::max({x0, x1, x2}), y0, c);
    return;
  }
  auto at = [](int xa, int ya, int xb, int yb, int y) {
    return ya == yb ? double(xa) : xa + double(xb - xa) * (y - ya) / (yb - ya);
  };
  for (int y = std::max(y0, cr.y0); y <= std::min(y2, cr.y1 - 1); ++y) {
    double xa = at(x0, y0, x2, y2, y);
    double xb = y < y1 ? at(x0, y0, x1, y1, y) : at(x1, y1, x2, y2, y);
    span(int(std::lround(xa)), int(std::lround(xb)), y, c);
  }
}

void Gfx::blit_sprite_pixel(int x, int y, int src, const ClipRect& cr) {
  if (x < cr.x0 || y < cr.y0 || x >= cr.x1 || y >= cr.y1) return;
  if (transparent(src)) return;
  screen()[y * kScreenW + x] = map_color(src);
  ++work_;
}

void Gfx::spr(int n, int x, int y, int w, int h, bool flip_x, bool flip_y) {
  if (n < 0 || n >= kSprites) return;
  int sx = (n % 16) * 8, sy = (n / 16) * 8;
  sspr(sx, sy, w * 8, h * 8, x, y, w * 8, h * 8, flip_x, flip_y);
}

void Gfx::sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x,
               bool flip_y) {
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
  dx -= cam_x();
  dy -= cam_y();
  auto cr = clip_rect();
  int i0 = std::max(0, cr.x0 - dx), i1 = std::min(dw, cr.x1 - dx);
  int j0 = std::max(0, cr.y0 - dy), j1 = std::min(dh, cr.y1 - dy);
  for (int j = j0; j < j1; ++j) {
    int v = (flip_y ? dh - 1 - j : j) * sh / dh;
    for (int i = i0; i < i1; ++i) {
      int u = (flip_x ? dw - 1 - i : i) * sw / dw;
      blit_sprite_pixel(dx + i, dy + j, sget(sx + u, sy + v), cr);
    }
  }
}

void Gfx::rspr(int n, int x, int y, double angle, double scale, int w, int h, bool flip_x,
               bool flip_y) {
  if (n < 0 || n >= kSprites || scale <= 0 || w <= 0 || h <= 0) return;
  int sx = (n % 16) * 8, sy = (n / 16) * 8;
  int bw = w * 8, bh = h * 8;
  double cx = x - cam_x() + bw / 2.0, cy = y - cam_y() + bh / 2.0;
  double a = angle * 2.0 * std::numbers::pi;
  double ca = std::cos(a), sa = std::sin(a);
  double reach = std::sqrt(double(bw * bw + bh * bh)) / 2.0 * scale + 1.0;
  auto cr = clip_rect();
  int px0 = std::max(cr.x0, int(std::floor(cx - reach))), px1 = std::min(cr.x1, int(std::ceil(cx + reach)));
  int py0 = std::max(cr.y0, int(std::floor(cy - reach))), py1 = std::min(cr.y1, int(std::ceil(cy + reach)));
  for (int py = py0; py < py1; ++py) {
    for (int px = px0; px < px1; ++px) {
      double ddx = px + 0.5 - cx, ddy = py + 0.5 - cy;
      double u = (ddx * ca + ddy * sa) / scale + bw / 2.0;
      double v = (-ddx * sa + ddy * ca) / scale + bh / 2.0;
      int iu = int(std::floor(u)), iv = int(std::floor(v));
      if (iu < 0 || iv < 0 || iu >= bw || iv >= bh) continue;
      if (flip_x) iu = bw - 1 - iu;
      if (flip_y) iv = bh - 1 - iv;
      blit_sprite_pixel(px, py, sget(sx + iu, sy + iv), cr);
    }
  }
}

void Gfx::map(int cx, int cy, int sx, int sy, int w, int h, uint8_t layers, uint8_t flags) {
  for (int layer = 0; layer < kMapLayers; ++layer) {
    if (!((layers >> layer) & 1)) continue;
    for (int j = 0; j < h; ++j) {
      for (int i = 0; i < w; ++i) {
        int mx = cx + i, my = cy + j;
        if (mx < 0 || my < 0 || mx >= kMapW || my >= kMapH) continue;
        uint16_t cell = mget(layer, mx, my);
        int id = cell & kCellIdMask;
        if (id == 0) continue;
        if (flags && (fget(id) & flags) != flags) continue;
        spr(id, sx + i * 8, sy + j * 8, 1, 1, cell & kCellFlipX, cell & kCellFlipY);
      }
    }
  }
}

int Gfx::text_width(std::string_view text) {
  int w = 0, best = 0;
  for (uint8_t ch : glyphs(text)) {
    if (ch == '\n') {
      best = std::max(best, w);
      w = 0;
    } else {
      w += kCharW;
    }
  }
  return std::max(best, w);
}

int Gfx::print(std::string_view text, int x, int y, int c) {
  auto cr = clip_rect();
  int px = x - cam_x(), py = y - cam_y(), start = px;
  for (uint8_t ch : glyphs(text)) {
    if (ch == '\n') {
      px = start;
      py += kCharH;
      continue;
    }
    for (int row = 0; row < kGlyphRows; ++row) {
      uint8_t bits = glyph_row(uint8_t(ch), row);
      for (int col = 0; col < kGlyphW; ++col) {
        if (!((bits >> (kGlyphW - 1 - col)) & 1)) continue;
        int gx = px + col, gy = py + row;
        if (gx < cr.x0 || gy < cr.y0 || gx >= cr.x1 || gy >= cr.y1) continue;
        screen()[gy * kScreenW + gx] = map_color(c);
        ++work_;
      }
    }
    px += kCharW;
  }
  return px + cam_x();
}

int Gfx::print(std::string_view text, int c) {
  int x = mem_.peeks16(addr::kCursor), y = mem_.peeks16(addr::kCursor + 2);
  int lines = 1 + int(std::count(text.begin(), text.end(), '\n'));
  // Scroll like a terminal once the cursor runs off the bottom.
  int overflow = y + lines * kCharH - kScreenH;
  if (overflow > 0) {
    int rows = ((overflow + kCharH - 1) / kCharH) * kCharH;
    rows = std::min(rows, kScreenH);
    std::copy(screen() + rows * kScreenW, screen() + kScreenW * kScreenH, screen());
    std::fill(screen() + (kScreenH - rows) * kScreenW, screen() + kScreenW * kScreenH, uint8_t(0));
    y -= rows;
  }
  int end = print(text, x, y, c);
  mem_.pokes16(addr::kCursor + 2, y + lines * kCharH);
  return end;
}

void Gfx::clip(int x, int y, int w, int h) {
  mem_.pokes16(addr::kClip, std::clamp(x, 0, kScreenW));
  mem_.pokes16(addr::kClip + 2, std::clamp(y, 0, kScreenH));
  mem_.pokes16(addr::kClip + 4, std::clamp(x + w, 0, kScreenW));
  mem_.pokes16(addr::kClip + 6, std::clamp(y + h, 0, kScreenH));
}

void Gfx::clip() { clip(0, 0, kScreenW, kScreenH); }

void Gfx::camera(int x, int y) {
  mem_.pokes16(addr::kCamera, x);
  mem_.pokes16(addr::kCamera + 2, y);
}

void Gfx::color(int c) { mem_.poke(addr::kPen, uint8_t(c & 31)); }
int Gfx::pen() const { return mem_.peek(addr::kPen) & 31; }

void Gfx::pal(int from, int to, bool screen_pal) {
  mem_.poke((screen_pal ? addr::kScreenPal : addr::kDrawPal) + (from & 31), uint8_t(to & 31));
}

void Gfx::pal() {
  for (int i = 0; i < kColors; ++i) {
    mem_.poke(addr::kDrawPal + i, uint8_t(i));
    mem_.poke(addr::kScreenPal + i, uint8_t(i));
  }
}

void Gfx::palt(int c, bool transparent_color) {
  uint32_t mask = mem_.peek32(addr::kTransparent);
  uint32_t bit = 1u << (c & 31);
  mem_.poke32(addr::kTransparent, transparent_color ? (mask | bit) : (mask & ~bit));
}

void Gfx::palt() { mem_.poke32(addr::kTransparent, 1); }

void Gfx::fillp(uint16_t pattern, int alt) {
  mem_.poke16(addr::kFillPattern, pattern);
  mem_.poke(addr::kFillAlt, alt < 0 ? 0xFF : uint8_t(alt & 31));
}

int Gfx::sget(int x, int y) const {
  if (x < 0 || y < 0 || x >= kSheetW || y >= kSheetH * kSheets) return 0;
  return mem_.peek(addr::kGfx + y * kSheetW + x) & 31;
}

void Gfx::sset(int x, int y, int c) {
  if (x < 0 || y < 0 || x >= kSheetW || y >= kSheetH * kSheets) return;
  mem_.poke(addr::kGfx + y * kSheetW + x, uint8_t(c & 31));
}

uint16_t Gfx::mget(int layer, int x, int y) const {
  if (layer < 0 || layer >= kMapLayers || x < 0 || y < 0 || x >= kMapW || y >= kMapH) return 0;
  return mem_.peek16(addr::kMap + layer * addr::kMapLayerSize + (y * kMapW + x) * 2);
}

void Gfx::mset(int layer, int x, int y, uint16_t cell) {
  if (layer < 0 || layer >= kMapLayers || x < 0 || y < 0 || x >= kMapW || y >= kMapH) return;
  mem_.poke16(addr::kMap + layer * addr::kMapLayerSize + (y * kMapW + x) * 2, cell);
}

uint8_t Gfx::fget(int n) const { return (n >= 0 && n < kSprites) ? mem_.peek(addr::kFlags + n) : 0; }

void Gfx::fset(int n, uint8_t flags) {
  if (n >= 0 && n < kSprites) mem_.poke(addr::kFlags + n, flags);
}

void Gfx::to_rgb(uint32_t* out) const {
  uint32_t lut[kColors];
  for (int c = 0; c < kColors; ++c) {
    int d = mem_.peek(addr::kScreenPal + c) & 31;
    uint32_t p = addr::kPalette + d * 3;
    lut[c] = (uint32_t(mem_.peek(p)) << 16) | (uint32_t(mem_.peek(p + 1)) << 8) | mem_.peek(p + 2);
  }
  const uint8_t* s = mem_.data() + addr::kScreen;
  for (int i = 0; i < kScreenW * kScreenH; ++i) out[i] = lut[s[i] & 31];
}

}  // namespace glim::gfx
