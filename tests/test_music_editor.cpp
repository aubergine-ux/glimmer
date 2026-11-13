#include <doctest.h>

#include <memory>

#include "editors/music_editor.hpp"
#include "editors/studio.hpp"

using namespace glim;
using namespace glim::editors;

namespace {
struct Fixture {
  std::unique_ptr<Console> con = std::make_unique<Console>();
  std::string clip;
  Studio studio{*con, {[this] { return clip; }, [this](const std::string& s) { clip = s; }}};
  MusicEditor ed{studio};
  Fixture() {
    studio.open(cart::Cart(), "");
    ed.reset();
  }
};
}  // namespace

TEST_CASE("entering a note enables the channel, writes its sfx and advances") {
  Fixture f;
  CHECK(f.ed.channel_byte(0) == 0x40);  // blank patterns start disabled
  f.ed.set_channel_byte(0, 0x40 | 3);   // channel 0 plays sfx 3, still off
  f.ed.set_cursor(0, 4);
  f.ed.enter_note(33);
  CHECK(f.ed.channel_byte(0) == 3);
  auto s = f.ed.step(0, 4);
  CHECK(s.pitch == 33);
  CHECK(s.volume == 5);
  CHECK(f.ed.row() == 5);
  // The note lives in sfx 3's data.
  size_t a = 3 * kSfxBytes + 4 + 4 * 2;
  CHECK(audio::Step::decode(uint16_t(f.studio.cart().sfx[a] | f.studio.cart().sfx[a + 1] << 8)).pitch == 33);
}

TEST_CASE("clearing a note turns it into a rest and undo brings it back") {
  Fixture f;
  f.ed.set_channel_byte(1, 2);
  f.ed.set_cursor(1, 0);
  f.ed.enter_note(20);
  f.ed.set_cursor(1, 0);
  f.ed.clear_note();
  CHECK(f.ed.step(1, 0).volume == 0);
  f.ed.undo();
  CHECK(f.ed.step(1, 0).volume == 5);
}

TEST_CASE("patterns copy and paste through the clipboard") {
  Fixture f;
  f.ed.set_channel_byte(0, 0x80 | 7);
  f.ed.copy();
  f.ed.set_pattern(9);
  f.ed.paste();
  CHECK(f.ed.channel_byte(0) == (0x80 | 7));
  CHECK(f.studio.cart().music[9 * 4] == (0x80 | 7));
}
