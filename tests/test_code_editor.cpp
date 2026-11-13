#include <doctest.h>

#include <memory>
#include <string>

#include "editors/code_editor.hpp"
#include "editors/studio.hpp"

using namespace glim;
using namespace glim::editors;

namespace {

// Drives a studio the way a user would: through key and text events.
struct Driver {
  std::unique_ptr<Console> con = std::make_unique<Console>();
  std::string clip;
  Studio studio{*con, {[this] { return clip; }, [this](const std::string& s) { clip = s; }}};

  explicit Driver(const std::string& code) {
    cart::Cart c;
    c.code = code;
    studio.open(c, "");
    frame();
  }
  void frame() { studio.tick(); }
  void key(const char* chord) {
    std::vector<input::Key> keys;
    std::string s = chord;
    size_t start = 0;
    while (true) {
      size_t plus = s.find('+', start);
      std::string part = s.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
      if (part == "ctrl") part = "lctrl";
      if (part == "shift") part = "lshift";
      if (part == "alt") part = "lalt";
      keys.push_back(input::key_from_name(part));
      if (plus == std::string::npos) break;
      start = plus + 1;
    }
    for (auto k : keys) con->input.set_key(k, true);
    frame();
    for (auto k : keys) con->input.set_key(k, false);
    frame();
  }
  void type(const std::string& text) {
    for (char c : text) {
      if (c == '\n') {
        key("return");
        continue;
      }
      con->input.add_text(std::string(1, c));
      frame();
    }
  }
  const std::string& code() { return studio.cart().code; }
  CodeEditor& ed() { return studio.code_editor(); }
};

}  // namespace

TEST_CASE("typing inserts text and blocks auto-indent") {
  Driver d("");
  d.type("function _draw()\ncls(1)\nend");
  CHECK(d.code() == "function _draw()\n  cls(1)\nend");
  CHECK(d.studio.dirty());
}

TEST_CASE("typing is undone in one step and redone") {
  Driver d("x = 1");
  d.ed().set_cursor(0, 5);
  d.type("23");
  CHECK(d.code() == "x = 123");
  d.key("ctrl+z");
  CHECK(d.code() == "x = 1");
  d.key("ctrl+y");
  CHECK(d.code() == "x = 123");
}

TEST_CASE("backspace removes a whole indent step in leading whitespace") {
  Driver d("if a then\n    b()\nend");
  d.ed().set_cursor(1, 4);
  d.key("backspace");
  CHECK(d.code() == "if a then\n  b()\nend");
}

TEST_CASE("ctrl+/ toggles comments and ctrl+d duplicates a line") {
  Driver d("a()\nb()");
  d.key("ctrl+/");
  CHECK(d.code() == "-- a()\nb()");
  d.key("ctrl+/");
  CHECK(d.code() == "a()\nb()");
  d.key("ctrl+d");
  CHECK(d.code() == "a()\na()\nb()");
}

TEST_CASE("cut, copy and paste go through the clipboard") {
  Driver d("one\ntwo");
  d.key("ctrl+x");  // nothing selected: cuts the line
  CHECK(d.code() == "two");
  CHECK(d.clip == "one\n");
  d.key("ctrl+v");
  CHECK(d.code() == "one\ntwo");
  d.clip = "\tz\r\n";
  d.key("ctrl+end");
  d.key("ctrl+v");
  CHECK(d.code() == "one\ntwo  z\n");
}

TEST_CASE("tab markers split the code into tabs") {
  Driver d("main()\n--8<-- enemies\nfoe()\n--8<-- ui\nhud()");
  CHECK(d.ed().tab_count() == 3);
  d.key("ctrl+pagedown");
  CHECK(d.ed().tab() == 1);
  CHECK(d.ed().cursor_pos().first == 2);
  // Cursor movement stays inside the tab.
  d.key("down");
  d.key("down");
  CHECK(d.ed().cursor_pos().first == 2);
}

TEST_CASE("runtime errors jump to their line in the right tab") {
  Driver d("x = 1\n--8<-- other\nfunction _update()\n  local y = nil + 1\nend");
  d.key("ctrl+r");
  for (int i = 0; i < 5; ++i) d.frame();
  CHECK(d.studio.playing());
  d.key("space");
  CHECK_FALSE(d.studio.playing());
  CHECK(d.ed().tab() == 1);
  CHECK(d.ed().cursor_pos().first == 3);
  CHECK(d.ed().status().find("line 4") != std::string::npos);
}

TEST_CASE("autocomplete offers api names and accepts with tab") {
  Driver d("");
  d.type("circf");
  d.key("tab");
  CHECK(d.code() == "circfill(");
}

TEST_CASE("find selects the next match") {
  Driver d("a = 1\nb = a + a");
  d.key("ctrl+f");
  d.type("a");
  d.key("return");
  d.key("return");
  auto [line, col] = d.ed().cursor_pos();
  CHECK(line == 1);
  CHECK(col == 5);
}

TEST_CASE("esc pauses a running cart and resumes it with edited code") {
  Driver d("function _init() n = 0 end\nfunction _update() n = n + 1 end\nfunction _draw() cls(n > 0 and 3 or 0) end");
  d.key("ctrl+r");
  for (int i = 0; i < 6; ++i) d.frame();
  d.key("escape");
  CHECK(d.studio.paused());
  CHECK_FALSE(d.studio.playing());
  // Change what _draw does while the game is paused, then go back.
  std::string draw_line = "function _draw() cls(n > 0 and 3 or 0) end";
  d.ed().set_cursor(2, int(draw_line.size()) - 4);  // just before " end"
  d.type(" pset(0, 0, 8)");
  d.key("escape");
  CHECK(d.studio.playing());
  for (int i = 0; i < 3; ++i) d.frame();
  CHECK(d.con->mem.peek(addr::kScreen) == 8);
  CHECK(d.con->mem.peek(addr::kScreen + 1) == 3);  // n kept counting: no restart
}
