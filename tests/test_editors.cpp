#include <doctest.h>

#include "editors/clipboard.hpp"
#include "editors/history.hpp"

using namespace glim;
using namespace glim::editors;

TEST_CASE("history undoes and redoes code edits with cursors") {
  cart::Cart c;
  cart::Cart* cp = &c;
  History h(cp);
  c.code = "hello world";

  h.begin(Target::kCode, 5);
  c.code.insert(5, ",");
  CHECK(h.commit(6));

  h.begin(Target::kCode, 12);
  c.code += "!";
  h.commit(13);

  CHECK(c.code == "hello, world!");
  CHECK(h.undo() == 12);
  CHECK(c.code == "hello, world");
  CHECK(h.undo() == 5);
  CHECK(c.code == "hello world");
  CHECK(h.undo() == -1);
  CHECK(h.redo() == 6);
  CHECK(c.code == "hello, world");
  CHECK(h.redo() == 13);
  CHECK(c.code == "hello, world!");
}

TEST_CASE("history coalesces an open step and skips no-op edits") {
  cart::Cart c;
  cart::Cart* cp = &c;
  History h(cp);
  h.begin(Target::kCode);
  c.code += "a";
  h.begin(Target::kCode);  // still the same step
  c.code += "b";
  h.commit();
  h.begin(Target::kGfx);
  CHECK_FALSE(h.commit());
  h.undo();
  CHECK(c.code.empty());
  CHECK_FALSE(h.can_undo());
}

TEST_CASE("history handles fixed-size data regions") {
  cart::Cart c;
  cart::Cart* cp = &c;
  History h(cp);
  h.begin(Target::kMap);
  c.map[100] = 7;
  c.map[5000] = 9;
  h.commit();
  h.begin(Target::kGfx);
  c.gfx[0] = 3;
  h.commit();
  h.undo();
  h.undo();
  CHECK(c.map[100] == 0);
  CHECK(c.map[5000] == 0);
  CHECK(c.gfx[0] == 0);
  h.redo();
  CHECK(c.map[5000] == 9);
  CHECK(c.map.size() == addr::kMapSize);
}

TEST_CASE("history cancel restores the snapshot") {
  cart::Cart c;
  cart::Cart* cp = &c;
  History h(cp);
  h.begin(Target::kSfx);
  c.sfx[10] = 99;
  h.cancel();
  CHECK(c.sfx[10] == 0);
  CHECK_FALSE(h.can_undo());
}

TEST_CASE("pixel and cell clips round-trip through text") {
  PixelClip p{3, 2, {0, 1, 31, 4, 5, 6}};
  PixelClip p2;
  REQUIRE(decode_pixels(encode_pixels(p), p2));
  CHECK(p2.w == 3);
  CHECK(p2.px == p.px);

  CellClip c{2, 1, {0x1ff, 0x0601}};
  CellClip c2;
  REQUIRE(decode_cells(encode_cells(c), c2));
  CHECK(c2.cells == c.cells);

  std::vector<uint8_t> bytes = {0, 255, 16}, back;
  REQUIRE(decode_bytes(encode_bytes("sfx", bytes), "sfx", back));
  CHECK(back == bytes);
  CHECK_FALSE(decode_bytes(encode_bytes("sfx", bytes), "pattern", back));
  CHECK_FALSE(decode_pixels("glimmer pixels 2 2\n00\n", p2));
  CHECK_FALSE(decode_pixels("hello", p2));
}
