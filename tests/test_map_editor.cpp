#include <doctest.h>

#include <memory>

#include "editors/map_editor.hpp"
#include "editors/studio.hpp"

using namespace glim;
using namespace glim::editors;

namespace {
struct Fixture {
  std::unique_ptr<Console> con = std::make_unique<Console>();
  std::string clip;
  Studio studio{*con, {[this] { return clip; }, [this](const std::string& s) { clip = s; }}};
  MapEditor ed{studio};
  Fixture() {
    studio.open(cart::Cart(), "");
    ed.reset();
  }
};
}  // namespace

TEST_CASE("autotile bases and picks follow the 4x4 block layout") {
  CHECK(autotile_base(64) == 64);
  CHECK(autotile_base(115) == 64);  // last tile of the block at sprite 64
  CHECK(autotile_base(68) == 68);
  CHECK(autotile_base(256 + 83) == 256 + 64);
  CHECK(autotile_pick(64, 0) == 64);
  CHECK(autotile_pick(64, 5) == 64 + 1 + 16);  // north + south
  CHECK(autotile_pick(64, 15) == 64 + 3 + 48);
}

TEST_CASE("auto-tiling picks tiles from neighbours and updates them") {
  Fixture f;
  f.ed.set_tile(64);
  f.ed.autotile(10, 10);
  CHECK(f.ed.cell(0, 10, 10) == 64);  // alone: mask 0
  f.ed.autotile(11, 10);
  CHECK(f.ed.cell(0, 10, 10) == autotile_pick(64, 2));  // east neighbour
  CHECK(f.ed.cell(0, 11, 10) == autotile_pick(64, 8));  // west neighbour
  f.ed.autotile(10, 11);
  CHECK(f.ed.cell(0, 10, 10) == autotile_pick(64, 2 | 4));
  CHECK(f.ed.cell(0, 10, 11) == autotile_pick(64, 1));
}

TEST_CASE("rectangles tile multi-cell stamps and flood fill stays in its region") {
  Fixture f;
  f.clip = "glimmer cells 2 1\n00010002\n";
  f.ed.paste();
  f.ed.fill_rect(0, 0, 3, 1);
  CHECK(f.ed.cell(0, 0, 0) == 1);
  CHECK(f.ed.cell(0, 1, 0) == 2);
  CHECK(f.ed.cell(0, 2, 1) == 1);
  f.ed.set_tile(5);
  f.ed.flood(10, 10);
  CHECK(f.ed.cell(0, 127, 63) == 5);
  CHECK(f.ed.cell(0, 1, 0) == 2);
}

TEST_CASE("layers are independent") {
  Fixture f;
  f.ed.set_tile(7);
  f.ed.set_layer(2);
  f.ed.paint(3, 3);
  CHECK(f.ed.cell(2, 3, 3) == 7);
  CHECK(f.ed.cell(0, 3, 3) == 0);
  CHECK(f.studio.cart().map[size_t(2) * addr::kMapLayerSize + (3 * kMapW + 3) * 2] == 7);
}
