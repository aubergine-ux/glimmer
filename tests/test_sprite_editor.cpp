#include <doctest.h>

#include <memory>

#include "editors/sheet.hpp"
#include "editors/sprite_editor.hpp"
#include "editors/studio.hpp"

using namespace glim;
using namespace glim::editors;

namespace {

struct Fixture {
  std::unique_ptr<Console> con = std::make_unique<Console>();
  std::string clip;
  Studio studio{*con, {[this] { return clip; }, [this](const std::string& s) { clip = s; }}};
  SpriteEditor ed{studio};

  Fixture() {
    cart::Cart c;
    // Sprite 1: a vertical bar in column 0 and a dot at (7, 0).
    for (int y = 0; y < 8; ++y) set_sheet_px(c, 8, y, 12);
    set_sheet_px(c, 15, 0, 3);
    studio.open(c, "");
    ed.reset();
    ed.set_sprite(1);
  }
  int px(int x, int y) { return sheet_px(studio.cart(), 8 + x, y); }
};

}  // namespace

TEST_CASE("flips mirror the sprite and undo restores it") {
  Fixture f;
  f.ed.flip(true);
  CHECK(f.px(7, 3) == 12);
  CHECK(f.px(0, 3) == 0);
  CHECK(f.px(0, 0) == 3);
  f.ed.flip(false);
  CHECK(f.px(0, 7) == 3);
  f.ed.undo();
  f.ed.undo();
  CHECK(f.px(0, 3) == 12);
  CHECK(f.px(7, 0) == 3);
  CHECK(f.studio.dirty());
}

TEST_CASE("rotating clockwise turns the left column into the top row") {
  Fixture f;
  f.ed.rotate();
  for (int x = 0; x < 8; ++x) CHECK(f.px(x, 0) == 12);
  CHECK(f.px(7, 7) == 3);
}

TEST_CASE("copy and paste move pixels between sprites") {
  Fixture f;
  f.ed.copy();
  CHECK(f.clip.starts_with("glimmer pixels 8 8"));
  f.ed.set_sprite(2);
  f.ed.paste();
  f.ed.set_sprite(1);  // switching sprites drops the floating paste
  CHECK(sheet_px(f.studio.cart(), 16, 4) == 12);
  CHECK(sheet_px(f.studio.cart(), 23, 0) == 3);
}

TEST_CASE("cut clears the block") {
  Fixture f;
  f.ed.cut();
  for (int y = 0; y < 8; ++y) CHECK(f.px(0, y) == 0);
  f.ed.undo();
  CHECK(f.px(0, 0) == 12);
}

TEST_CASE("sprite selection keeps multi-sprite blocks inside the sheet") {
  Fixture f;
  f.ed.set_sprite(15);
  CHECK(f.ed.sprite() == 15);
  f.ed.set_sprite(511);
  CHECK(f.ed.sprite() == 511);
}
