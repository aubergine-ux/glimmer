#include "script/vm.hpp"

#include <cstdlib>
#include <cstring>

// Lua is compiled as C++ (see third_party/CMakeLists.txt), so its headers are
// included without extern "C".
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace glim::script {

namespace {

constexpr int kHookInterval = 1000;  // instructions between budget checks
// A job that cannot yield (deep inside a C call such as table.sort) is allowed
// to overrun this many frames' worth of cycles before it is aborted.
constexpr uint64_t kUnyieldableOverrun = 64;

constexpr const char* kChunkName = "=cart";
constexpr const char* kFrameSource = R"(
return function()
  local u, d = _update, _draw
  if u then u() end
  if d then d() end
end
)";

uint64_t splitmix(uint64_t& x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

}  // namespace

Vm::Vm(Host host) : host_(std::move(host)) {
  L_ = lua_newstate(&Vm::alloc, this);
  *static_cast<Vm**>(lua_getextraspace(L_)) = this;
  open_libs();
  register_api(*this);

  if (luaL_loadbufferx(L_, kPrelude, std::strlen(kPrelude), "=prelude", "t") != LUA_OK || lua_pcall(L_, 0, 0, 0) != LUA_OK) {
    host_.log(std::string("prelude failed: ") + lua_tostring(L_, -1));
    lua_pop(L_, 1);
  }
  luaL_loadbufferx(L_, kFrameSource, std::strlen(kFrameSource), "=frame", "t");
  lua_call(L_, 0, 1);
  frame_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);

  thread_ = lua_newthread(L_);
  thread_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
  // Coroutines created by the cart inherit this hook from the frame thread.
  lua_sethook(thread_, &Vm::hook, LUA_MASKCOUNT, kHookInterval);
}

Vm::~Vm() {
  if (L_) lua_close(L_);
}

void* Vm::alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* vm = static_cast<Vm*>(ud);
  size_t old = ptr ? osize : 0;
  if (nsize == 0) {
    vm->heap_bytes_ -= old;
    std::free(ptr);
    return nullptr;
  }
  if (nsize > old && vm->heap_bytes_ + (nsize - old) > kLuaHeapLimit) return nullptr;
  void* p = std::realloc(ptr, nsize);
  if (p) vm->heap_bytes_ = vm->heap_bytes_ - old + nsize;
  return p;
}

void Vm::hook(lua_State* L, lua_Debug*) {
  Vm* vm = *static_cast<Vm**>(lua_getextraspace(L));
  vm->cycles_ += kHookInterval;
  if (vm->cycles_ < vm->budget_) return;
  // Only the frame thread yields; yielding inside a cart coroutine would look
  // like a spurious coroutine.yield() to the cart.
  if (L == vm->thread_ && lua_isyieldable(L)) {
    lua_yield(L, 0);
    return;
  }
  if (vm->cycles_ > vm->budget_ * kUnyieldableOverrun) luaL_error(L, "cpu budget exceeded");
}

void Vm::open_libs() {
  static const luaL_Reg libs[] = {
      {LUA_GNAME, luaopen_base},          {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
      {LUA_MATHLIBNAME, luaopen_math},    {LUA_COLIBNAME, luaopen_coroutine}, {LUA_UTF8LIBNAME, luaopen_utf8},
  };
  for (const auto& lib : libs) {
    luaL_requiref(L_, lib.name, lib.func, 1);
    lua_pop(L_, 1);
  }
  // Carts are sandboxed: no file access and no precompiled bytecode.
  for (const char* name : {"dofile", "loadfile", "load"}) {
    lua_pushnil(L_);
    lua_setglobal(L_, name);
  }
  lua_getglobal(L_, "string");
  lua_pushnil(L_);
  lua_setfield(L_, -2, "dump");
  lua_pop(L_, 1);
}

void Vm::seed_random(uint64_t seed) {
  rng_[0] = splitmix(seed);
  rng_[1] = splitmix(seed);
}

uint64_t Vm::next_random() {
  uint64_t s0 = rng_[0], s1 = rng_[1];
  uint64_t r = s0 + s1;
  s1 ^= s0;
  rng_[0] = rotl(s0, 24) ^ s1 ^ (s1 << 16);
  rng_[1] = rotl(s1, 37);
  return r;
}

bool Vm::load(std::string_view code, uint64_t seed) {
  seed_random(seed);
  error_ = {};
  int status = luaL_loadbufferx(L_, code.data(), code.size(), kChunkName, "t");
  if (status != LUA_OK) {
    set_error(L_, status);
    lua_pop(L_, 1);
    return false;
  }
  if (boot_ref_ != -1) luaL_unref(L_, LUA_REGISTRYINDEX, boot_ref_);
  boot_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
  stage_ = Stage::kBoot;
  job_active_ = false;
  return true;
}

bool Vm::reload(std::string_view code) {
  error_ = {};
  int status = luaL_loadbufferx(L_, code.data(), code.size(), kChunkName, "t");
  if (status == LUA_OK) {
    // Run on the main state, which never yields; the count hook still stops
    // a runaway top level once it overruns its allowance.
    cycles_ = 0;
    budget_ = kCyclesPerFrame;
    lua_sethook(L_, &Vm::hook, LUA_MASKCOUNT, kHookInterval);
    status = lua_pcall(L_, 0, 0, 0);
    lua_sethook(L_, nullptr, 0, 0);
    if (status == LUA_OK) return true;
  }
  set_error(L_, status);
  lua_pop(L_, 1);
  return false;
}

bool Vm::has_function(const char* name) {
  int t = lua_getglobal(L_, name);
  lua_pop(L_, 1);
  return t == LUA_TFUNCTION;
}

bool Vm::start_job() {
  switch (stage_) {
    case Stage::kBoot:
      if (boot_ref_ == -1) return false;
      lua_rawgeti(thread_, LUA_REGISTRYINDEX, boot_ref_);
      break;
    case Stage::kInit:
      lua_getglobal(thread_, "_init");
      break;
    case Stage::kFrames:
      lua_rawgeti(thread_, LUA_REGISTRYINDEX, frame_ref_);
      break;
  }
  job_active_ = true;
  job_cycles_ = 0;
  return true;
}

Vm::Status Vm::tick(uint64_t budget) {
  cycles_ = 0;
  budget_ = budget;
  if (!job_active_ && !start_job()) return Status::kDone;

  int nres = 0;
  int status = lua_resume(thread_, L_, 0, &nres);
  job_cycles_ += cycles_;

  if (status == LUA_YIELD) {
    // A bare coroutine.yield() in frame code also lands here: it waits a tick.
    lua_pop(thread_, nres);
    return Status::kYielded;
  }
  if (status != LUA_OK) {
    set_error(thread_, status);
    lua_closethread(thread_, L_);
    job_active_ = false;
    return Status::kError;
  }
  lua_settop(thread_, 0);
  job_active_ = false;
  if (stage_ == Stage::kFrames) cpu_usage_ = double(job_cycles_) / double(kCyclesPerFrame);
  if (stage_ == Stage::kBoot) {
    stage_ = has_function("_init") ? Stage::kInit : Stage::kFrames;
  } else if (stage_ == Stage::kInit) {
    stage_ = Stage::kFrames;
  }
  return Status::kDone;
}

void Vm::set_error(lua_State* from, int status) {
  const char* msg = luaL_tolstring(from, -1, nullptr);
  std::string full = msg ? msg : "unknown error";
  lua_pop(from, 1);
  if (status == LUA_ERRMEM) full = "out of memory (carts may use 16 MiB of Lua heap)";

  error_ = {};
  // Lua prefixes errors with "cart:LINE: " when raised from cart code.
  if (full.rfind("cart:", 0) == 0) {
    size_t colon = full.find(':', 5);
    if (colon != std::string::npos) {
      error_.line = std::atoi(full.c_str() + 5);
      full = full.substr(colon + 1);
      if (!full.empty() && full[0] == ' ') full.erase(0, 1);
    }
  }
  error_.message = full;

  lua_Debug ar;
  for (int level = 0; lua_getstack(from, level, &ar); ++level) {
    lua_getinfo(from, "Sl", &ar);
    if (std::strcmp(ar.source, kChunkName) == 0 && ar.currentline > 0) {
      if (!error_.line) error_.line = ar.currentline;
      break;
    }
  }
  if (from != L_) {
    luaL_traceback(L_, from, nullptr, 0);
    error_.traceback = lua_tostring(L_, -1);
    lua_pop(L_, 1);
  }
}

}  // namespace glim::script
