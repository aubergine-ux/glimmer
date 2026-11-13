#include <doctest.h>

#include <set>
#include <string>

#include "editors/api_docs.hpp"
#include "editors/clipboard.hpp"
#include "editors/history.hpp"
#include "editors/lua_lexer.hpp"
#include "runtime/console.hpp"

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

namespace {
std::string classes(std::string_view line, LexState& st) {
  std::vector<Tok> toks;
  st = lex_line(line, st, toks);
  std::string out;
  for (Tok t : toks) out += char('0' + int(t));
  return out;
}
}  // namespace

TEST_CASE("lexer classifies keywords, api calls, strings, numbers and comments") {
  LexState st;
  // local x = spr(1) -- hi
  auto c = classes("local x = spr(1) -- hi", st);
  CHECK(c.substr(0, 5) == "11111");           // local
  CHECK(c[6] == '0');                         // x
  CHECK(c.substr(10, 3) == "222");            // spr
  CHECK(c[14] == '6');                        // 1
  CHECK(c.substr(17) == "77777");             // comment
  CHECK(classes("s = \"a\\\"b\" .. 0x1f", st).substr(4, 6) == "555555");
  CHECK(classes("p.spr = 1", st).substr(2, 3) == "000");  // fields aren't the api
  CHECK(classes("function _draw()", st).substr(9, 5) == "33333");
}

TEST_CASE("lexer carries long strings and comments across lines") {
  LexState st;
  classes("x = [==[ start", st);
  CHECK(st.mode == LexState::kLongString);
  CHECK(st.level == 2);
  CHECK(classes("still ]] inside", st) == std::string(15, '5'));
  auto c = classes("done ]==] y", st);
  CHECK(st.mode == LexState::kNormal);
  CHECK(c.back() == '0');
  classes("--[[ block", st);
  CHECK(st.mode == LexState::kLongComment);
  classes("]] cls()", st);
  CHECK(st.mode == LexState::kNormal);
}

TEST_CASE("every function a cart can call is documented for the editor") {
  Console con;
  std::set<std::string> globals;
  con.on_log = [&](std::string_view s) { globals.emplace(s); };
  cart::Cart c;
  // Lua's own libraries are documented upstream; skip them.
  c.code = R"(
    local std = {assert=1, error=1, getmetatable=1, ipairs=1, next=1, pairs=1, pcall=1,
      rawequal=1, rawget=1, rawlen=1, rawset=1, select=1, setmetatable=1,
      tonumber=1, tostring=1, type=1, xpcall=1, collectgarbage=1, warn=1}
    for k, v in pairs(_G) do
      if type(v) == "function" and not std[k] then printh(k) end
    end
  )";
  con.run(c, 1);
  con.tick();
  REQUIRE(globals.size() > 50);
  for (const auto& name : globals) CHECK_MESSAGE(find_api(name) != nullptr, "undocumented: " << name);
}
