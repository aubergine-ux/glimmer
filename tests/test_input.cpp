#include <doctest.h>

#include "input/input.hpp"

using namespace glim;
using namespace glim::input;

TEST_CASE("key names round-trip") {
  for (auto name : {"a", "z", "0", "5", "f1", "f12", "space", "left", "rshift", "/"}) {
    Key k = key_from_name(name);
    REQUIRE(k != 0);
    CHECK(key_name(k) == name);
  }
  CHECK(key_from_name("nope") == 0);
  CHECK(key_from_name("f13") == 0);
}

TEST_CASE("default bindings drive player one") {
  Input in;
  in.set_key(key_from_name("z"), true);
  in.begin_frame();
  CHECK(in.btn(kA));
  CHECK(in.btnp(kA));
  CHECK_FALSE(in.btn(kB));
  in.begin_frame();
  CHECK(in.btn(kA));
  CHECK_FALSE(in.btnp(kA));
}

TEST_CASE("btnp repeats after a delay") {
  Input in;
  in.set_pad_button(1, kUp, true);
  int presses = 0;
  for (int f = 0; f < 23; ++f) {
    in.begin_frame();
    presses += in.btnp(kUp, 1);
  }
  // Initial press, then repeats on frames 16 and 20 past the 15-frame delay.
  CHECK(presses == 3);
}

TEST_CASE("key map config replaces bindings and survives a round trip") {
  KeyMap km;
  km.load("# custom\np1.a = space, return\np2.left = f\nbogus line\n");
  Input in;
  in.keymap() = km;
  in.set_key(key_from_name("return"), true);
  in.set_key(key_from_name("f"), true);
  in.begin_frame();
  CHECK(in.btn(kA, 0));
  CHECK(in.btn(kLeft, 1));
  in.set_key(key_from_name("z"), true);
  in.set_key(key_from_name("return"), false);
  in.begin_frame();
  CHECK_FALSE(in.btn(kA, 0));

  KeyMap copy;
  copy.load(km.save());
  CHECK(copy.bindings().size() == km.bindings().size());
}

TEST_CASE("input registers mirror button and mouse state") {
  Memory mem;
  Input in;
  in.set_pad_button(0, kB, true);
  in.set_mouse(100, -3, 0b101);
  in.add_wheel(-2);
  in.begin_frame();
  in.write_registers(mem);
  CHECK(mem.peek16(addr::kButtons) == (1 << kB));
  CHECK(mem.peeks16(addr::kMouse) == 100);
  CHECK(mem.peeks16(addr::kMouse + 2) == -3);
  CHECK(mem.peek(addr::kMouse + 4) == 0b101);
  CHECK(int8_t(mem.peek(addr::kMouse + 5)) == -2);
}

TEST_CASE("keyr repeats held keys for text editing") {
  Input in;
  Key k = key_from_name("backspace");
  in.set_key(k, true);
  int fires = 0;
  for (int f = 0; f < 30; ++f) {
    in.begin_frame();
    fires += in.keyr(k);
  }
  // Press on frame 1, then every 2 frames after the 24-frame delay: 25, 27, 29.
  CHECK(fires == 4);
  in.set_key(key_from_name("rctrl"), true);
  in.begin_frame();
  CHECK(in.ctrl());
  CHECK_FALSE(in.shift());
}
