#include <doctest.h>

#include "gfx/gfx.hpp"

using namespace glim;

namespace {
struct Fixture {
  Memory mem;
  gfx::Gfx g{mem};
  Fixture() {
    g.reset_palette();
    g.reset_state();
    g.cls(0);
  }
  int px(int x, int y) const { return mem.peek(addr::kScreen + y * kScreenW + x); }
  int count(int c) const {
    int n = 0;
    for (int i = 0; i < kScreenW * kScreenH; ++i) n += mem.peek(addr::kScreen + i) == c;
    return n;
  }
};
}  // namespace

TEST_CASE("pset and pget respect camera and screen bounds") {
  Fixture f;
  f.g.pset(10, 20, 7);
  CHECK(f.px(10, 20) == 7);
  CHECK(f.g.pget(10, 20) == 7);
  f.g.camera(5, 5);
  f.g.pset(15, 25, 9);
  CHECK(f.px(10, 20) == 9);
  f.g.camera(0, 0);
  f.g.pset(-1, 0, 3);
  f.g.pset(kScreenW, 0, 3);
  CHECK(f.count(3) == 0);
  CHECK(f.g.pget(-5, 999) == 0);
}

TEST_CASE("clip rectangle limits drawing") {
  Fixture f;
  f.g.clip(10, 10, 4, 4);
  f.g.rectfill(0, 0, 255, 143, 5);
  CHECK(f.count(5) == 16);
  CHECK(f.px(10, 10) == 5);
  CHECK(f.px(13, 13) == 5);
  CHECK(f.px(14, 13) == 0);
}

TEST_CASE("lines are continuous and include both endpoints") {
  Fixture f;
  f.g.line(0, 0, 9, 4, 1);
  CHECK(f.px(0, 0) == 1);
  CHECK(f.px(9, 4) == 1);
  CHECK(f.count(1) == 10);
  f.g.line(20, 5, 20, 5, 2);
  CHECK(f.count(2) == 1);
}

TEST_CASE("rect outline and fill accept swapped corners") {
  Fixture f;
  f.g.rect(9, 9, 0, 0, 4);
  CHECK(f.count(4) == 36);
  CHECK(f.px(5, 5) == 0);
  f.g.rectfill(30, 30, 20, 20, 6);
  CHECK(f.count(6) == 121);
}

TEST_CASE("circles are symmetric and filled circles have no holes") {
  Fixture f;
  f.g.circfill(50, 50, 6, 8);
  for (int dy = -6; dy <= 6; ++dy)
    for (int dx = -6; dx <= 6; ++dx) CHECK(f.px(50 + dx, 50 + dy) == f.px(50 - dx, 50 - dy));
  CHECK(f.px(50, 50) == 8);
  CHECK(f.px(56, 50) == 8);
  CHECK(f.px(57, 50) == 0);

  Fixture g;
  g.g.circ(50, 50, 6, 8);
  CHECK(g.px(50, 50) == 0);
  CHECK(g.px(56, 50) == 8);
  CHECK(g.px(50, 44) == 8);
}

TEST_CASE("ovals fit their bounding box exactly") {
  Fixture f;
  f.g.ovalfill(10, 10, 30, 20, 3);
  CHECK(f.px(10, 15) == 3);
  CHECK(f.px(30, 15) == 3);
  CHECK(f.px(20, 10) == 3);
  CHECK(f.px(20, 20) == 3);
  CHECK(f.px(9, 15) == 0);
  CHECK(f.px(20, 21) == 0);
  CHECK(f.px(10, 10) == 0);
}

TEST_CASE("triangle fill covers its vertices") {
  Fixture f;
  f.g.trifill(10, 10, 40, 10, 10, 40, 2);
  CHECK(f.px(10, 10) == 2);
  CHECK(f.px(40, 10) == 2);
  CHECK(f.px(10, 40) == 2);
  CHECK(f.px(30, 30) == 0);
  CHECK(f.px(15, 15) == 2);
}

TEST_CASE("fill patterns alternate colors and can be transparent") {
  Fixture f;
  f.g.fillp(0b1010'0101'1010'0101, 9);
  f.g.rectfill(0, 0, 3, 3, 1);
  CHECK(f.px(0, 0) == 9);
  CHECK(f.px(1, 0) == 1);
  CHECK(f.px(0, 1) == 1);
  CHECK(f.px(1, 1) == 9);

  f.g.cls(0);
  f.g.fillp(0xFFFF);
  f.g.rectfill(0, 0, 3, 3, 1);
  CHECK(f.count(1) == 0);
}

TEST_CASE("sprites honor transparency, palette swaps and flips") {
  Fixture f;
  // Sprite 1 is at sheet x 8..15, row 0..7: a single colored column on the left.
  for (int y = 0; y < 8; ++y) f.g.sset(8, y, 12);
  f.g.sset(9, 0, 0);
  f.g.cls(4);
  f.g.spr(1, 100, 50);
  CHECK(f.px(100, 50) == 12);
  CHECK(f.px(101, 50) == 4);  // color 0 is transparent by default
  f.g.spr(1, 120, 50, 1, 1, true);
  CHECK(f.px(127, 50) == 12);
  CHECK(f.px(120, 50) == 4);

  f.g.pal(12, 20);
  f.g.spr(1, 0, 0);
  CHECK(f.px(0, 0) == 20);
  f.g.pal();
  f.g.palt(0, false);
  f.g.spr(1, 0, 0);
  CHECK(f.px(1, 0) == 0);
}

TEST_CASE("sprites above 255 come from the second sheet") {
  Fixture f;
  f.g.sset(0, 128, 17);
  f.g.spr(256, 10, 10);
  CHECK(f.px(10, 10) == 17);
}

TEST_CASE("sspr stretches its source") {
  Fixture f;
  f.g.sset(0, 0, 5);
  f.g.sset(1, 0, 6);
  f.g.sspr(0, 0, 2, 1, 0, 0, 8, 4);
  CHECK(f.px(0, 0) == 5);
  CHECK(f.px(3, 3) == 5);
  CHECK(f.px(4, 0) == 6);
  CHECK(f.px(7, 3) == 6);
}

TEST_CASE("rspr at zero angle matches spr and quarter turns rotate clockwise") {
  Fixture f;
  for (int i = 0; i < 8; ++i) f.g.sset(8 + i, 0, 7);  // top row of sprite 1
  f.g.rspr(1, 0, 0, 0.0);
  for (int i = 0; i < 8; ++i) CHECK(f.px(i, 0) == 7);
  CHECK(f.count(7) == 8);

  f.g.cls(0);
  f.g.rspr(1, 0, 0, 0.25);
  // The top row ends up on the right column.
  for (int i = 0; i < 8; ++i) CHECK(f.px(7, i) == 7);
}

TEST_CASE("map draws cells from enabled layers and skips tile zero") {
  Fixture f;
  f.g.sset(8, 0, 3);    // sprite 1 pixel
  f.g.sset(16, 0, 11);  // sprite 2 pixel
  f.g.mset(0, 0, 0, 1);
  f.g.mset(1, 1, 0, 2);
  f.g.map(0, 0, 0, 0, 2, 1);
  CHECK(f.px(0, 0) == 3);
  CHECK(f.px(8, 0) == 11);

  f.g.cls(0);
  f.g.map(0, 0, 0, 0, 2, 1, 0b0001);
  CHECK(f.px(0, 0) == 3);
  CHECK(f.px(8, 0) == 0);

  f.g.cls(0);
  f.g.fset(2, 0b100);
  f.g.map(0, 0, 0, 0, 2, 1, 0x0F, 0b100);
  CHECK(f.px(0, 0) == 0);
  CHECK(f.px(8, 0) == 11);
}

TEST_CASE("print uses the built-in font and returns the end position") {
  Fixture f;
  int end = f.g.print("HI", 0, 0, 7);
  CHECK(end == 8);
  CHECK(f.px(0, 0) == 7);  // H top-left
  CHECK(f.px(1, 0) == 0);
  CHECK(f.px(1, 2) == 7);  // H crossbar
  CHECK(gfx::Gfx::text_width("ab\nlonger") == 24);

  f.g.cls(0);
  f.g.print("g", 0, 0, 7);
  CHECK(f.px(0, 5) == 7);  // descender hook on the sixth row
  f.g.print("a", 4, 0, 7);
  CHECK(f.px(4, 5) == 0);
}

TEST_CASE("screen palette remaps colors at presentation time") {
  Fixture f;
  f.g.cls(1);
  f.g.pal(1, 31, true);
  uint32_t out[kScreenW * kScreenH];
  f.g.to_rgb(out);
  CHECK(out[0] == 0xFFFFFF);
  CHECK(f.px(0, 0) == 1);
}
