// Lua bindings for the Glimmer API. Each function charges a small fixed cost
// plus the pixels or bytes it touched, so API-heavy carts are budgeted fairly.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string>

#include "lauxlib.h"
#include "lua.h"
#include "gfx/font.hpp"
#include "script/vm.hpp"

namespace glim::script {

namespace {

constexpr uint64_t kCallCost = 4;
constexpr int kPersistSlots = addr::kPersistSize / 4;
constexpr int kMaxPeekCount = 8192;

Vm& vm_of(lua_State* L) { return **static_cast<Vm**>(lua_getextraspace(L)); }

int to_int(double v) {
  if (std::isnan(v)) return 0;
  return int(std::clamp(std::floor(v), -1e9, 1e9));
}

int iarg(lua_State* L, int i) { return to_int(luaL_checknumber(L, i)); }
int iopt(lua_State* L, int i, int def) { return lua_isnoneornil(L, i) ? def : iarg(L, i); }
double nopt(lua_State* L, int i, double def) { return lua_isnoneornil(L, i) ? def : luaL_checknumber(L, i); }
bool bopt(lua_State* L, int i) { return lua_toboolean(L, i); }

// Optional trailing color argument, defaulting to the pen color.
int color_arg(lua_State* L, int i) { return lua_isnoneornil(L, i) ? vm_of(L).host().gfx.pen() : iarg(L, i); }

void charge_gfx(lua_State* L) {
  Vm& vm = vm_of(L);
  vm.charge(kCallCost + vm.host().gfx.take_work() / 4);
}

gfx::Gfx& G(lua_State* L) { return vm_of(L).host().gfx; }

std::string number_string(lua_State* L, int i, bool hex) {
  char buf[64];
  if (lua_isinteger(L, i)) {
    lua_Integer n = lua_tointeger(L, i);
    if (hex) std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)n);
    else std::snprintf(buf, sizeof buf, "%lld", (long long)n);
    return buf;
  }
  double d = lua_tonumber(L, i);
  if (hex) {
    std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)(long long)d);
    return buf;
  }
  if (std::isnan(d)) return "nan";
  if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
  if (d == std::floor(d) && std::abs(d) < 1e15) {
    std::snprintf(buf, sizeof buf, "%.0f", d);
    return buf;
  }
  if (std::abs(d) >= 1e9 || std::abs(d) < 1e-4) {
    std::snprintf(buf, sizeof buf, "%g", d);
    return buf;
  }
  // Four decimals is plenty on a 256-pixel screen; trim trailing zeros.
  std::snprintf(buf, sizeof buf, "%.4f", d);
  std::string s = buf;
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s;
}

std::string to_str(lua_State* L, int i, bool hex = false) {
  switch (lua_type(L, i)) {
    case LUA_TNONE:
    case LUA_TNIL:
      return "nil";
    case LUA_TBOOLEAN:
      return lua_toboolean(L, i) ? "true" : "false";
    case LUA_TNUMBER:
      return number_string(L, i, hex);
    case LUA_TSTRING: {
      size_t n = 0;
      const char* s = lua_tolstring(L, i, &n);
      return std::string(s, n);
    }
    default: {
      size_t n = 0;
      const char* s = luaL_tolstring(L, i, &n);
      std::string out(s, n);
      lua_pop(L, 1);
      return out;
    }
  }
}

// ---- graphics ----------------------------------------------------------

int l_cls(lua_State* L) {
  G(L).cls(iopt(L, 1, 0));
  charge_gfx(L);
  return 0;
}

int l_pset(lua_State* L) {
  G(L).pset(iarg(L, 1), iarg(L, 2), color_arg(L, 3));
  charge_gfx(L);
  return 0;
}

int l_pget(lua_State* L) {
  lua_pushinteger(L, G(L).pget(iarg(L, 1), iarg(L, 2)));
  charge_gfx(L);
  return 1;
}

int l_line(lua_State* L) {
  G(L).line(iarg(L, 1), iarg(L, 2), iarg(L, 3), iarg(L, 4), color_arg(L, 5));
  charge_gfx(L);
  return 0;
}

template <void (gfx::Gfx::*Fn)(int, int, int, int, int)>
int l_box(lua_State* L) {
  (G(L).*Fn)(iarg(L, 1), iarg(L, 2), iarg(L, 3), iarg(L, 4), color_arg(L, 5));
  charge_gfx(L);
  return 0;
}

template <void (gfx::Gfx::*Fn)(int, int, int, int)>
int l_circle(lua_State* L) {
  (G(L).*Fn)(iarg(L, 1), iarg(L, 2), iopt(L, 3, 4), color_arg(L, 4));
  charge_gfx(L);
  return 0;
}

template <void (gfx::Gfx::*Fn)(int, int, int, int, int, int, int)>
int l_triangle(lua_State* L) {
  (G(L).*Fn)(iarg(L, 1), iarg(L, 2), iarg(L, 3), iarg(L, 4), iarg(L, 5), iarg(L, 6), color_arg(L, 7));
  charge_gfx(L);
  return 0;
}

int l_spr(lua_State* L) {
  G(L).spr(iarg(L, 1), iarg(L, 2), iarg(L, 3), iopt(L, 4, 1), iopt(L, 5, 1), bopt(L, 6), bopt(L, 7));
  charge_gfx(L);
  return 0;
}

int l_sspr(lua_State* L) {
  int sw = iarg(L, 3), sh = iarg(L, 4);
  G(L).sspr(iarg(L, 1), iarg(L, 2), sw, sh, iarg(L, 5), iarg(L, 6), iopt(L, 7, sw), iopt(L, 8, sh), bopt(L, 9),
            bopt(L, 10));
  charge_gfx(L);
  return 0;
}

int l_rspr(lua_State* L) {
  G(L).rspr(iarg(L, 1), iarg(L, 2), iarg(L, 3), nopt(L, 4, 0.0), nopt(L, 5, 1.0), iopt(L, 6, 1), iopt(L, 7, 1),
            bopt(L, 8), bopt(L, 9));
  charge_gfx(L);
  return 0;
}

int l_map(lua_State* L) {
  int w = iopt(L, 5, kScreenW / 8), h = iopt(L, 6, kScreenH / 8);
  G(L).map(iopt(L, 1, 0), iopt(L, 2, 0), iopt(L, 3, 0), iopt(L, 4, 0), w, h, uint8_t(iopt(L, 7, 0x0F)),
           uint8_t(iopt(L, 8, 0)));
  vm_of(L).charge(uint64_t(std::max(0, w)) * std::max(0, h));
  charge_gfx(L);
  return 0;
}

int l_print(lua_State* L) {
  std::string s = to_str(L, 1);
  auto& g = G(L);
  int end;
  if (lua_gettop(L) >= 3) {
    int x = iarg(L, 2), y = iarg(L, 3);
    end = g.print(s, x, y, color_arg(L, 4));
    vm_of(L).host().mem.pokes16(addr::kCursor, x);
    vm_of(L).host().mem.pokes16(addr::kCursor + 2, y + gfx::kCharH);
  } else {
    end = g.print(s, color_arg(L, 2));
  }
  charge_gfx(L);
  lua_pushinteger(L, end);
  return 1;
}

int l_cursor(lua_State* L) {
  auto& mem = vm_of(L).host().mem;
  mem.pokes16(addr::kCursor, iopt(L, 1, 0));
  mem.pokes16(addr::kCursor + 2, iopt(L, 2, 0));
  if (!lua_isnoneornil(L, 3)) G(L).color(iarg(L, 3));
  return 0;
}

int l_textwidth(lua_State* L) {
  lua_pushinteger(L, gfx::Gfx::text_width(to_str(L, 1)));
  return 1;
}

int l_clip(lua_State* L) {
  if (lua_isnoneornil(L, 1)) G(L).clip();
  else G(L).clip(iarg(L, 1), iarg(L, 2), iarg(L, 3), iarg(L, 4));
  return 0;
}

int l_camera(lua_State* L) {
  // Read the arguments before pushing results: with no arguments, stack
  // slot 1 would otherwise be the old x we just pushed.
  int x = iopt(L, 1, 0), y = iopt(L, 2, 0);
  auto& mem = vm_of(L).host().mem;
  lua_pushinteger(L, mem.peeks16(addr::kCamera));
  lua_pushinteger(L, mem.peeks16(addr::kCamera + 2));
  G(L).camera(x, y);
  return 2;
}

int l_color(lua_State* L) {
  int c = iopt(L, 1, 31);
  lua_pushinteger(L, G(L).pen());
  G(L).color(c);
  return 1;
}

int l_pal(lua_State* L) {
  auto& g = G(L);
  if (lua_isnoneornil(L, 1)) {
    g.pal();
  } else if (lua_istable(L, 1)) {
    bool screen = bopt(L, 2);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
      if (lua_isnumber(L, -2) && lua_isnumber(L, -1))
        g.pal(to_int(lua_tonumber(L, -2)), to_int(lua_tonumber(L, -1)), screen);
      lua_pop(L, 1);
    }
  } else {
    g.pal(iarg(L, 1), iarg(L, 2), bopt(L, 3));
  }
  return 0;
}

int l_palt(lua_State* L) {
  auto& g = G(L);
  if (lua_isnoneornil(L, 1)) {
    g.palt();
  } else if (lua_isnoneornil(L, 2)) {
    vm_of(L).host().mem.poke32(addr::kTransparent, uint32_t(luaL_checkinteger(L, 1)));
  } else {
    g.palt(iarg(L, 1), bopt(L, 2));
  }
  return 0;
}

int l_fillp(lua_State* L) {
  G(L).fillp(uint16_t(iopt(L, 1, 0)), lua_isnoneornil(L, 2) ? -1 : iarg(L, 2));
  return 0;
}

int l_sget(lua_State* L) {
  lua_pushinteger(L, G(L).sget(iarg(L, 1), iarg(L, 2)));
  return 1;
}

int l_sset(lua_State* L) {
  G(L).sset(iarg(L, 1), iarg(L, 2), color_arg(L, 3));
  return 0;
}

int l_mget(lua_State* L) {
  lua_pushinteger(L, G(L).mget(iopt(L, 3, 0), iarg(L, 1), iarg(L, 2)));
  return 1;
}

int l_mset(lua_State* L) {
  G(L).mset(iopt(L, 4, 0), iarg(L, 1), iarg(L, 2), uint16_t(iarg(L, 3)));
  return 0;
}

int l_fget(lua_State* L) {
  uint8_t f = G(L).fget(iarg(L, 1));
  if (lua_isnoneornil(L, 2)) lua_pushinteger(L, f);
  else lua_pushboolean(L, (f >> (iarg(L, 2) & 7)) & 1);
  return 1;
}

int l_fset(lua_State* L) {
  auto& g = G(L);
  int n = iarg(L, 1);
  if (lua_gettop(L) >= 3) {
    uint8_t bit = uint8_t(1u << (iarg(L, 2) & 7));
    uint8_t f = g.fget(n);
    g.fset(n, bopt(L, 3) ? (f | bit) : (f & ~bit));
  } else {
    g.fset(n, uint8_t(iarg(L, 2)));
  }
  return 0;
}

// ---- audio --------------------------------------------------------------

int l_sfx(lua_State* L) {
  vm_of(L).host().audio.sfx(iarg(L, 1), iopt(L, 2, -1), iopt(L, 3, 0), iopt(L, 4, kSfxSteps));
  vm_of(L).charge(kCallCost);
  return 0;
}

int l_music(lua_State* L) {
  vm_of(L).host().audio.music(iopt(L, 1, -1), iopt(L, 2, 0), uint8_t(iopt(L, 3, 0x0F)));
  vm_of(L).charge(kCallCost);
  return 0;
}

int l_note(lua_State* L) {
  int pitch = lua_isnoneornil(L, 2) ? -1 : iarg(L, 2);
  vm_of(L).host().audio.note(iarg(L, 1), pitch, iopt(L, 3, 0), iopt(L, 4, 5), iopt(L, 5, 0));
  vm_of(L).charge(kCallCost);
  return 0;
}

// ---- input ----------------------------------------------------------------

int button_arg(lua_State* L, int i) {
  if (lua_type(L, i) == LUA_TSTRING) return input::button_from_name(lua_tostring(L, i));
  return iarg(L, i);
}

int l_btn(lua_State* L) {
  auto& in = vm_of(L).host().input;
  if (lua_isnoneornil(L, 1)) {
    lua_pushinteger(L, in.buttons(iopt(L, 2, 0)));
    return 1;
  }
  lua_pushboolean(L, in.btn(button_arg(L, 1), iopt(L, 2, 0)));
  return 1;
}

int l_btnp(lua_State* L) {
  auto& in = vm_of(L).host().input;
  if (lua_isnoneornil(L, 1)) {
    int p = iopt(L, 2, 0), mask = 0;
    for (int b = 0; b < input::kButtonCount; ++b) mask |= in.btnp(b, p) << b;
    lua_pushinteger(L, mask);
    return 1;
  }
  lua_pushboolean(L, in.btnp(button_arg(L, 1), iopt(L, 2, 0)));
  return 1;
}

int l_key(lua_State* L) {
  lua_pushboolean(L, vm_of(L).host().input.key(input::key_from_name(luaL_checkstring(L, 1))));
  return 1;
}

int l_keyp(lua_State* L) {
  lua_pushboolean(L, vm_of(L).host().input.keyp(input::key_from_name(luaL_checkstring(L, 1))));
  return 1;
}

int l_mouse(lua_State* L) {
  const auto& m = vm_of(L).host().input.mouse();
  lua_pushinteger(L, m.x);
  lua_pushinteger(L, m.y);
  lua_pushinteger(L, m.buttons);
  lua_pushinteger(L, m.wheel);
  return 4;
}

int l_textin(lua_State* L) {
  const auto& t = vm_of(L).host().input.text();
  lua_pushlstring(L, t.data(), t.size());
  return 1;
}

// ---- memory ----------------------------------------------------------------

Memory& M(lua_State* L) { return vm_of(L).host().mem; }

uint32_t addr_arg(lua_State* L, int i) {
  lua_Integer a = luaL_checkinteger(L, i);
  return a < 0 ? addr::kRamSize : uint32_t(std::min<lua_Integer>(a, addr::kRamSize));
}

int l_peek(lua_State* L) {
  uint32_t a = addr_arg(L, 1);
  int n = std::clamp(iopt(L, 2, 1), 0, kMaxPeekCount);
  luaL_checkstack(L, n, "too many values");
  for (int i = 0; i < n; ++i) lua_pushinteger(L, M(L).peek(a + i));
  vm_of(L).charge(kCallCost + n);
  return n;
}

int l_poke(lua_State* L) {
  uint32_t a = addr_arg(L, 1);
  int n = lua_gettop(L);
  for (int i = 2; i <= n; ++i) M(L).poke(a + (i - 2), uint8_t(luaL_checkinteger(L, i)));
  vm_of(L).charge(kCallCost + n);
  return 0;
}

int l_peek2(lua_State* L) {
  lua_pushinteger(L, M(L).peek16(addr_arg(L, 1)));
  return 1;
}

int l_poke2(lua_State* L) {
  M(L).poke16(addr_arg(L, 1), uint16_t(luaL_checkinteger(L, 2)));
  return 0;
}

int l_peek4(lua_State* L) {
  lua_pushinteger(L, int32_t(M(L).peek32(addr_arg(L, 1))));
  return 1;
}

int l_poke4(lua_State* L) {
  M(L).poke32(addr_arg(L, 1), uint32_t(luaL_checkinteger(L, 2)));
  return 0;
}

int l_memcpy(lua_State* L) {
  uint32_t len = uint32_t(std::max<lua_Integer>(0, luaL_checkinteger(L, 3)));
  M(L).copy(addr_arg(L, 1), addr_arg(L, 2), len);
  vm_of(L).charge(kCallCost + len / 4);
  return 0;
}

int l_memset(lua_State* L) {
  uint32_t len = uint32_t(std::max<lua_Integer>(0, luaL_checkinteger(L, 3)));
  M(L).fill(addr_arg(L, 1), uint8_t(luaL_checkinteger(L, 2)), len);
  vm_of(L).charge(kCallCost + len / 4);
  return 0;
}

int l_dget(lua_State* L) {
  int i = iarg(L, 1);
  if (i < 0 || i >= kPersistSlots) return luaL_argerror(L, 1, "slot must be 0-63");
  float f = std::bit_cast<float>(M(L).peek32(addr::kPersist + i * 4));
  if (f == std::floor(f) && std::abs(f) < 16777216.f) lua_pushinteger(L, lua_Integer(f));
  else lua_pushnumber(L, f);
  return 1;
}

int l_dset(lua_State* L) {
  int i = iarg(L, 1);
  if (i < 0 || i >= kPersistSlots) return luaL_argerror(L, 1, "slot must be 0-63");
  M(L).poke32(addr::kPersist + i * 4, std::bit_cast<uint32_t>(float(luaL_checknumber(L, 2))));
  if (auto& cb = vm_of(L).host().persist_changed) cb();
  return 0;
}

// ---- math ---------------------------------------------------------------

int l_flr(lua_State* L) {
  lua_pushinteger(L, lua_Integer(std::floor(nopt(L, 1, 0))));
  return 1;
}

int l_ceil(lua_State* L) {
  lua_pushinteger(L, lua_Integer(std::ceil(nopt(L, 1, 0))));
  return 1;
}

int l_sgn(lua_State* L) {
  lua_pushinteger(L, nopt(L, 1, 0) < 0 ? -1 : 1);
  return 1;
}

int l_mid(lua_State* L) {
  double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2), c = luaL_checknumber(L, 3);
  lua_pushnumber(L, std::max(std::min(a, b), std::min(std::max(a, b), c)));
  return 1;
}

int l_sin(lua_State* L) {
  lua_pushnumber(L, std::sin(luaL_checknumber(L, 1) * 2.0 * std::numbers::pi));
  return 1;
}

int l_cos(lua_State* L) {
  lua_pushnumber(L, std::cos(luaL_checknumber(L, 1) * 2.0 * std::numbers::pi));
  return 1;
}

int l_atan2(lua_State* L) {
  double t = std::atan2(luaL_checknumber(L, 2), luaL_checknumber(L, 1)) / (2.0 * std::numbers::pi);
  lua_pushnumber(L, t < 0 ? t + 1.0 : t);
  return 1;
}

int l_rnd(lua_State* L) {
  Vm& vm = vm_of(L);
  double unit = double(vm.next_random() >> 11) * 0x1.0p-53;
  if (lua_istable(L, 1)) {
    lua_Integer n = luaL_len(L, 1);
    if (n <= 0) return 0;
    lua_geti(L, 1, 1 + lua_Integer(unit * double(n)));
    return 1;
  }
  lua_pushnumber(L, unit * nopt(L, 1, 1.0));
  return 1;
}

int l_srand(lua_State* L) {
  vm_of(L).seed_random(uint64_t(luaL_checkinteger(L, 1)));
  return 0;
}

int l_tostr(lua_State* L) {
  auto s = to_str(L, 1, bopt(L, 2));
  lua_pushlstring(L, s.data(), s.size());
  return 1;
}

// ---- system ---------------------------------------------------------------

int l_time(lua_State* L) {
  auto& fc = vm_of(L).host().frame_count;
  lua_pushnumber(L, fc ? double(fc()) / kFps : 0.0);
  return 1;
}

int l_stat(lua_State* L) {
  Vm& vm = vm_of(L);
  auto& audio = vm.host().audio;
  int n = iarg(L, 1);
  switch (n) {
    case 0: lua_pushnumber(L, double(vm.heap_bytes()) / 1024.0); break;
    case 1: lua_pushnumber(L, vm.cpu_usage()); break;
    case 2: lua_pushinteger(L, vm.host().frame_count ? lua_Integer(vm.host().frame_count()) : 0); break;
    case 3: lua_pushinteger(L, kFps); break;
    case 4: case 5: case 6: case 7: lua_pushinteger(L, audio.status(n - 4).sfx); break;
    case 8: case 9: case 10: case 11: lua_pushinteger(L, audio.status(n - 8).step); break;
    case 12: lua_pushinteger(L, audio.music_pattern()); break;
    case 13: lua_pushnumber(L, double(vm.cycles()) / double(kCyclesPerFrame)); break;
    default: lua_pushnil(L); break;
  }
  return 1;
}

int l_printh(lua_State* L) {
  std::string line;
  for (int i = 1; i <= lua_gettop(L); ++i) {
    if (i > 1) line += ' ';
    line += to_str(L, i);
  }
  if (auto& log = vm_of(L).host().log) log(line);
  return 0;
}

int l_reset(lua_State* L) {
  G(L).reset_state();
  return 0;
}

int l_trace(lua_State* L) {
  luaL_traceback(L, L, lua_isnoneornil(L, 1) ? nullptr : lua_tostring(L, 1), 1);
  return 1;
}

}  // namespace

void register_api(Vm& vm) {
  static const luaL_Reg api[] = {
      {"cls", l_cls},
      {"pset", l_pset},
      {"pget", l_pget},
      {"line", l_line},
      {"rect", l_box<&gfx::Gfx::rect>},
      {"rectfill", l_box<&gfx::Gfx::rectfill>},
      {"oval", l_box<&gfx::Gfx::oval>},
      {"ovalfill", l_box<&gfx::Gfx::ovalfill>},
      {"circ", l_circle<&gfx::Gfx::circ>},
      {"circfill", l_circle<&gfx::Gfx::circfill>},
      {"tri", l_triangle<&gfx::Gfx::tri>},
      {"trifill", l_triangle<&gfx::Gfx::trifill>},
      {"spr", l_spr},
      {"sspr", l_sspr},
      {"rspr", l_rspr},
      {"map", l_map},
      {"print", l_print},
      {"cursor", l_cursor},
      {"textwidth", l_textwidth},
      {"clip", l_clip},
      {"camera", l_camera},
      {"color", l_color},
      {"pal", l_pal},
      {"palt", l_palt},
      {"fillp", l_fillp},
      {"sget", l_sget},
      {"sset", l_sset},
      {"mget", l_mget},
      {"mset", l_mset},
      {"fget", l_fget},
      {"fset", l_fset},
      {"sfx", l_sfx},
      {"music", l_music},
      {"note", l_note},
      {"btn", l_btn},
      {"btnp", l_btnp},
      {"key", l_key},
      {"keyp", l_keyp},
      {"mouse", l_mouse},
      {"textin", l_textin},
      {"peek", l_peek},
      {"poke", l_poke},
      {"peek2", l_peek2},
      {"poke2", l_poke2},
      {"peek4", l_peek4},
      {"poke4", l_poke4},
      {"memcpy", l_memcpy},
      {"memset", l_memset},
      {"dget", l_dget},
      {"dset", l_dset},
      {"flr", l_flr},
      {"ceil", l_ceil},
      {"sgn", l_sgn},
      {"mid", l_mid},
      {"sin", l_sin},
      {"cos", l_cos},
      {"atan2", l_atan2},
      {"rnd", l_rnd},
      {"srand", l_srand},
      {"tostr", l_tostr},
      {"time", l_time},
      {"t", l_time},
      {"stat", l_stat},
      {"printh", l_printh},
      {"reset", l_reset},
      {"trace", l_trace},
      {nullptr, nullptr},
  };
  lua_State* L = vm.state();
  lua_pushglobaltable(L);
  luaL_setfuncs(L, api, 0);
  lua_pop(L, 1);
}

}  // namespace glim::script
