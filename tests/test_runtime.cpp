#include <doctest.h>

#include <string>
#include <vector>

#include "runtime/console.hpp"

using namespace glim;

namespace {

struct Runner {
  Console con;
  std::vector<std::string> log;

  explicit Runner(const std::string& code, uint64_t seed = 1) {
    con.on_log = [this](std::string_view s) { log.emplace_back(s); };
    cart::Cart c;
    c.code = code;
    con.run(c, seed);
  }
  void ticks(int n) {
    for (int i = 0; i < n; ++i) con.tick();
  }
  int px(int x, int y) const { return con.mem.peek(addr::kScreen + y * kScreenW + x); }
};

}  // namespace

TEST_CASE("callbacks run in order: top level, _init once, then frames") {
  Runner r(R"(
    printh("top")
    function _init() printh("init") end
    function _update() printh("update") end
    function _draw() printh("draw") end
  )");
  r.ticks(4);
  REQUIRE(r.log.size() >= 6);
  CHECK(r.log[0] == "top");
  CHECK(r.log[1] == "init");
  CHECK(r.log[2] == "update");
  CHECK(r.log[3] == "draw");
  CHECK(r.log[4] == "update");
  CHECK(r.con.state() == Console::State::kRunning);
}

TEST_CASE("drawing reaches the display only when a frame completes") {
  Runner r("function _draw() cls(3) pset(5, 6, 9) end");
  r.ticks(3);
  CHECK(r.px(5, 6) == 9);
  CHECK(r.px(0, 0) == 3);
  CHECK(r.con.display()[6 * kScreenW + 5] == 0x5c1f2e);
}

TEST_CASE("runtime errors report the cart line") {
  Runner r("function _update()\n  local t = nil\n  return t.x\nend\n");
  r.ticks(3);
  CHECK(r.con.state() == Console::State::kError);
  CHECK(r.con.error().line == 3);
  CHECK(r.con.error().message.find("nil") != std::string::npos);
}

TEST_CASE("syntax errors are reported at load") {
  Runner r("function _draw(\n  cls(\nend");
  CHECK(r.con.state() == Console::State::kError);
  CHECK(r.con.error().line > 0);
}

TEST_CASE("over-budget frames spill into later ticks instead of hanging") {
  Runner r(R"(
    n = 0
    function _update()
      for i = 1, 600000 do n = n + 1 end
      printh("done")
    end
  )");
  r.ticks(2);  // boot, _init
  int ticks = 0;
  while (r.log.empty() && ticks < 20) {
    r.con.tick();
    ++ticks;
  }
  CHECK(ticks > 1);
  CHECK(r.log.size() == 1);
  CHECK(r.con.cpu_usage() > 1.0);
}

TEST_CASE("an infinite loop never blocks the host") {
  Runner r("function _update() while true do end end");
  r.ticks(10);
  CHECK(r.con.state() == Console::State::kRunning);
  CHECK(r.con.frames() == 0);
}

TEST_CASE("cart coroutines are unaffected by budget yields") {
  Runner r(R"(
    function _update()
      local co = coroutine.create(function()
        local s = 0
        for i = 1, 3000000 do s = s + 1 end
        coroutine.yield(s)
      end)
      local ok, v = coroutine.resume(co)
      printh(tostr(v))
    end
  )");
  r.ticks(40);
  REQUIRE_FALSE(r.log.empty());
  CHECK(r.log[0] == "3000000");
}

TEST_CASE("the sandbox hides files, the OS and bytecode") {
  Runner r(R"(
    printh(tostr(io), tostr(os), tostr(load), tostr(dofile), tostr(string.dump), tostr(require))
  )");
  r.ticks(1);
  REQUIRE(r.log.size() == 1);
  CHECK(r.log[0] == "nil nil nil nil nil nil");
}

TEST_CASE("rnd is reproducible with srand") {
  Runner r(R"(
    srand(42) local a = rnd(100)
    srand(42) local b = rnd(100)
    printh(tostr(a == b), tostr(a >= 0 and a < 100), tostr(rnd({7})))
  )");
  r.ticks(1);
  CHECK(r.log[0] == "true true 7");
}

TEST_CASE("tostr formats numbers for a small screen") {
  Runner r(R"(printh(tostr(3), tostr(2.0), tostr(1/3), tostr(0.5), tostr(255, true), tostr(nil), tostr(true)))");
  r.ticks(1);
  CHECK(r.log[0] == "3 2 0.3333 0.5 0xff nil true");
}

TEST_CASE("math helpers use turns and floor to integers") {
  Runner r(R"(printh(tostr(flr(-1.5)), tostr(sin(0.25)), tostr(cos(0.5)), tostr(atan2(0, 1)), tostr(mid(5, 1, 3)), tostr(sgn(-2))))");
  r.ticks(1);
  CHECK(r.log[0] == "-2 1 -1 0.25 3 -1");
}

TEST_CASE("table helpers tolerate deletion during iteration") {
  Runner r(R"(
    local t = {1, 2, 3, 4, 5}
    for v in all(t) do if v % 2 == 0 then del(t, v) end end
    printh(tostr(count(t)), tostr(t[1]), tostr(t[2]), tostr(t[3]))
    local p = split("1,a,3")
    printh(math.type(p[1]), p[2], tostr(p[3] + 1))
  )");
  r.ticks(1);
  CHECK(r.log[0] == "3 1 3 5");
  CHECK(r.log[1] == "integer a 4");
}

TEST_CASE("buttons are latched at the start of each frame") {
  Runner r("function _update() printh(tostr(btn(4)), tostr(btn('a'))) end");
  r.ticks(2);
  r.con.input.set_pad_button(0, input::kA, true);
  r.ticks(1);
  CHECK(r.log.back() == "true true");
}

TEST_CASE("peek and poke reach console memory") {
  Runner r(R"(
    poke(0x23000, 1, 2, 3)
    local a, b, c = peek(0x23000, 3)
    poke4(0x23010, -5)
    printh(tostr(a + b + c), tostr(peek4(0x23010)), tostr(peek(-1)))
  )");
  r.ticks(1);
  CHECK(r.log[0] == "6 -5 0");
}

TEST_CASE("dset values persist across reloads") {
  Console con;
  int saves = 0;
  con.on_persist = [&] { ++saves; };
  cart::Cart c;
  c.code = "dset(3, 1234.5)";
  con.run(c, 1);
  con.tick();
  CHECK(saves == 1);
  c.code = "printh(tostr(dget(3)))";
  std::string out;
  con.on_log = [&](std::string_view s) { out = s; };
  con.run(c, 1);
  con.tick();
  CHECK(out == "1234.5");
}

TEST_CASE("time advances one sixtieth per completed frame") {
  Runner r("function _update() printh(tostr(t() * 60)) end");
  r.ticks(4);
  CHECK(r.log.back() == "2");
}

TEST_CASE("hot reload swaps functions but keeps state from _init") {
  Console con;
  std::vector<std::string> log;
  con.on_log = [&](std::string_view s) { log.emplace_back(s); };
  cart::Cart c;
  c.code = "function _init() n = 0 end\nfunction _update() n = n + 1 printh('a' .. n) end";
  con.run(c, 1);
  for (int i = 0; i < 4; ++i) con.tick();
  c.code = "function _init() n = 0 end\nfunction _update() n = n + 10 printh('b' .. n) end";
  c.gfx[0] = 9;
  REQUIRE(con.reload(c));
  con.tick();
  CHECK(log.back() == "b12");  // boot and _init take two of the four ticks
  CHECK(con.mem.peek(addr::kGfx) == 9);

  c.code = "function _update( broken";
  CHECK_FALSE(con.reload(c));
  CHECK(con.state() == Console::State::kRunning);
  CHECK(con.error().line == 1);
  con.tick();
  CHECK(log.back() == "b22");
}

TEST_CASE("hot reload keeps runtime map changes unless the map was edited") {
  Console con;
  cart::Cart c;
  c.code = "mset(1, 1, 7)";
  con.run(c, 1);
  con.tick();
  REQUIRE(con.gfx.mget(0, 1, 1) == 7);
  REQUIRE(con.reload(c));
  CHECK(con.gfx.mget(0, 1, 1) == 7);
}

TEST_CASE("breakpoints stop the cart and expose locals and globals") {
  Console con;
  cart::Cart c;
  c.code = "score = 7\nfunction add(a, b)\n  local sum = a + b\n  return sum\nend\nfunction _update()\n  local r = add(2, 3)\n  score = score + r\nend";
  con.set_breakpoints({4});
  con.run(c, 1);
  for (int i = 0; i < 4 && !con.stopped(); ++i) con.tick();
  REQUIRE(con.stopped());
  const auto* d = con.debug_state();
  REQUIRE(d != nullptr);
  CHECK(d->line == 4);
  REQUIRE(!d->frames.empty());
  CHECK(d->frames[0].function == "add");
  bool saw_sum = false;
  for (const auto& v : d->frames[0].vars) saw_sum |= v.name == "sum" && v.value == "5";
  CHECK(saw_sum);
  bool saw_score = false;
  for (const auto& v : d->globals) saw_score |= v.name == "score";
  CHECK(saw_score);

  // Stopped: ticking doesn't advance.
  uint64_t frames = con.frames();
  con.tick();
  CHECK(con.frames() == frames);

  // Step over the return: next stop is back in _update, line 8.
  con.resume_debug(script::StepMode::kOver);
  con.tick();
  REQUIRE(con.stopped());
  CHECK(con.debug_state()->line == 8);

  con.set_breakpoints({});
  con.resume_debug(script::StepMode::kNone);
  con.tick();
  CHECK_FALSE(con.stopped());
}
