#pragma once

#include <cstdint>
#include <string_view>

#include "core/memory.hpp"

namespace glim::gfx {

// Software rasterizer drawing into the screen region of console RAM.
// All draw state (camera, clip, palettes, fill pattern) lives in RAM too, so
// poking those addresses is equivalent to calling the API.
class Gfx {
 public:
  explicit Gfx(Memory& mem) : mem_(mem) {}

  void reset_state();
  void reset_palette();

  void cls(int c = 0);
  void pset(int x, int y, int c);
  int pget(int x, int y) const;
  void line(int x0, int y0, int x1, int y1, int c);
  void rect(int x0, int y0, int x1, int y1, int c);
  void rectfill(int x0, int y0, int x1, int y1, int c);
  void circ(int cx, int cy, int r, int c);
  void circfill(int cx, int cy, int r, int c);
  void oval(int x0, int y0, int x1, int y1, int c);
  void ovalfill(int x0, int y0, int x1, int y1, int c);
  void tri(int x0, int y0, int x1, int y1, int x2, int y2, int c);
  void trifill(int x0, int y0, int x1, int y1, int x2, int y2, int c);

  // Sprites are numbered 0-511 across both sheets; n >= 256 is sheet 1.
  void spr(int n, int x, int y, int w = 1, int h = 1, bool flip_x = false, bool flip_y = false);
  // Source coordinates address the 128x256 strip of both sheets stacked.
  void sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x = false,
            bool flip_y = false);
  // Rotates around the center of the w*8 x h*8 block whose top-left is (x, y).
  // Angle is in turns, clockwise on screen.
  void rspr(int n, int x, int y, double angle, double scale = 1.0, int w = 1, int h = 1,
            bool flip_x = false, bool flip_y = false);
  void map(int cx, int cy, int sx, int sy, int w, int h, uint8_t layers = 0x0F, uint8_t flags = 0);

  // Returns the x coordinate after the last character.
  int print(std::string_view text, int x, int y, int c);
  int print(std::string_view text, int c);
  static int text_width(std::string_view text);

  void clip(int x, int y, int w, int h);
  void clip();
  void camera(int x, int y);
  void color(int c);
  int pen() const;
  void pal(int from, int to, bool screen = false);
  void pal();
  void palt(int c, bool transparent);
  void palt();
  void fillp(uint16_t pattern, int alt = -1);

  int sget(int x, int y) const;
  void sset(int x, int y, int c);
  uint16_t mget(int layer, int x, int y) const;
  void mset(int layer, int x, int y, uint16_t cell);
  uint8_t fget(int n) const;
  void fset(int n, uint8_t flags);

  // Resolves screen and RGB palettes into 0x00RRGGBB pixels, row-major.
  void to_rgb(uint32_t* out) const;

  // Pixels touched since the last call; the runtime charges CPU cycles for them.
  uint64_t take_work() {
    uint64_t w = work_;
    work_ = 0;
    return w;
  }

 private:
  struct ClipRect {
    int x0, y0, x1, y1;
  };
  ClipRect clip_rect() const;
  int cam_x() const { return mem_.peeks16(addr::kCamera); }
  int cam_y() const { return mem_.peeks16(addr::kCamera + 2); }
  uint8_t* screen() { return mem_.data() + addr::kScreen; }
  uint8_t map_color(int c) const { return mem_.peek(addr::kDrawPal + (c & 31)) & 31; }
  bool transparent(int c) const { return (mem_.peek32(addr::kTransparent) >> (c & 31)) & 1; }

  // Screen-space primitives: camera already applied, clip and fill pattern not yet.
  void plot(int x, int y, int c);
  void span(int x0, int x1, int y, int c);
  void blit_sprite_pixel(int x, int y, int src, const ClipRect& cr);

  Memory& mem_;
  uint64_t work_ = 0;
};

}  // namespace glim::gfx
