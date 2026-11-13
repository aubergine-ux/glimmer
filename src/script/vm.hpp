#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio/audio.hpp"
#include "core/memory.hpp"
#include "gfx/gfx.hpp"
#include "input/input.hpp"

struct lua_State;
struct lua_Debug;

namespace glim::script {

// Cycles a frame may spend across _update and _draw (SPEC.md, "CPU").
inline constexpr uint64_t kCyclesPerFrame = 1'000'000;
inline constexpr size_t kLuaHeapLimit = 16u << 20;

struct Host {
  Memory& mem;
  gfx::Gfx& gfx;
  audio::Audio& audio;
  input::Input& input;
  std::function<void(std::string_view)> log;
  std::function<void()> persist_changed;
  std::function<uint64_t()> frame_count;
};

struct ScriptError {
  std::string message;    // first line, e.g. "attempt to index a nil value"
  std::string traceback;  // full stack
  int line = 0;           // line in the cart's code, 0 when unknown
};

// What the debugger shows when a cart is stopped at a breakpoint.
struct DebugVar {
  std::string name, value;
  std::vector<std::pair<std::string, std::string>> fields;  // first level of a table
};

struct DebugFrame {
  std::string function;
  int line = 0;
  std::vector<DebugVar> vars;  // locals, then upvalues
};

struct DebugState {
  int line = 0;  // where execution stopped
  std::vector<DebugFrame> frames;  // innermost first
  std::vector<DebugVar> globals;   // the cart's own globals, not the API
};

enum class StepMode { kNone, kInto, kOver, kOut };

// One Lua state running a cart. Work is split into jobs (loading the chunk,
// _init, each frame); a job that exceeds the cycle budget yields from the
// instruction hook and resumes on the next tick, so slow carts slow down the
// same way on every machine.
class Vm {
 public:
  enum class Status { kDone, kYielded, kError, kBreak };

  explicit Vm(Host host);
  ~Vm();
  Vm(const Vm&) = delete;
  Vm& operator=(const Vm&) = delete;

  // Compiles the cart code and queues it as the first job, followed by _init.
  bool load(std::string_view code, uint64_t seed);
  // Hot reload: runs new code's top level in the live state, redefining
  // functions while keeping globals set up by _init. A frame suspended
  // mid-way keeps running its old code until it finishes.
  bool reload(std::string_view code);
  // Runs the pending job, starting the next frame job if nothing is pending.
  // Returns kDone when a whole frame (or boot) finished within this tick.
  Status tick(uint64_t budget = kCyclesPerFrame);
  bool booted() const { return stage_ == Stage::kFrames; }
  bool frame_pending() const { return job_active_; }

  const ScriptError& error() const { return error_; }
  uint64_t cycles() const { return cycles_; }
  // Fraction of the budget the last completed frame used; above 1 when it spilled over.
  double cpu_usage() const { return cpu_usage_; }
  size_t heap_bytes() const { return heap_bytes_; }
  bool has_function(const char* name);

  // Debugging. Breakpoints are cart line numbers. While stopped, tick() does
  // nothing until resume_debug() picks a way to continue.
  void set_breakpoints(std::set<int> lines);
  bool stopped() const { return stopped_; }
  const DebugState& debug_state() const { return debug_; }
  void resume_debug(StepMode mode);

  // Called by the bindings for work done outside the VM (pixels, memory copies).
  void charge(uint64_t cycles) { cycles_ += cycles; }
  Host& host() { return host_; }
  lua_State* state() { return L_; }
  uint64_t next_random();
  void seed_random(uint64_t seed);

 private:
  enum class Stage { kBoot, kInit, kFrames };

  static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);
  static void hook(lua_State* L, lua_Debug* ar);
  void open_libs();
  void set_error(lua_State* from, int status);
  bool start_job();
  void update_hook_mask();
  void on_line(lua_State* L, lua_Debug* ar);
  int cart_depth(lua_State* L) const;
  void capture_debug(lua_State* L);

  Host host_;
  lua_State* L_ = nullptr;
  lua_State* thread_ = nullptr;
  int thread_ref_ = -1;
  int boot_ref_ = -1;
  int frame_ref_ = -1;
  Stage stage_ = Stage::kBoot;
  bool job_active_ = false;
  uint64_t cycles_ = 0;
  uint64_t budget_ = kCyclesPerFrame;
  uint64_t job_cycles_ = 0;
  double cpu_usage_ = 0.0;
  size_t heap_bytes_ = 0;
  uint64_t rng_[2] = {1, 2};
  std::set<int> breakpoints_;
  StepMode step_ = StepMode::kNone;
  int step_depth_ = 0;
  bool stopped_ = false;
  bool just_resumed_ = false;
  DebugState debug_;
  std::set<std::string> builtin_globals_;
  ScriptError error_;
};

void register_api(Vm& vm);
extern const char* const kPrelude;

}  // namespace glim::script
