#include <doctest.h>

#include <memory>

#include "editors/sfx_editor.hpp"
#include "editors/studio.hpp"

using namespace glim;
using namespace glim::editors;

TEST_CASE("note names count octaves from C2") {
  CHECK(note_name(0) == "c-2");
  CHECK(note_name(33) == "a-4");
  CHECK(note_name(13) == "c#3");
  CHECK(note_name(63) == "d#7");
}

TEST_CASE("sfx edits write the cart and copy between slots") {
  auto con = std::make_unique<Console>();
  std::string clip;
  Studio studio(*con, {[&] { return clip; }, [&](const std::string& s) { clip = s; }});
  studio.open(cart::Cart(), "");
  SfxEditor ed(studio);
  ed.reset();

  audio::Sfx s = ed.read();
  CHECK(s.speed == 16);  // blank sfx default
  s.speed = 9;
  s.steps[3] = {40, 2, 6, 1};
  ed.write(s);
  CHECK(studio.cart().sfx[0] == 9);
  CHECK(audio::Step::decode(uint16_t(studio.cart().sfx[4 + 6] | studio.cart().sfx[4 + 7] << 8)).pitch == 40);

  ed.copy();
  ed.set_sfx(5);
  ed.paste();
  audio::Sfx t = ed.read();
  CHECK(t.speed == 9);
  CHECK(t.steps[3].wave == 2);
  ed.undo();
  CHECK(ed.read().speed == 16);
}
