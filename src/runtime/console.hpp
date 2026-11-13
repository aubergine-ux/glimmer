#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "audio/audio.hpp"
#include "cart/cart.hpp"
#include "core/memory.hpp"
#include "gfx/gfx.hpp"
#include "input/input.hpp"
#include "script/vm.hpp"

namespace glim {

// The whole virtual machine: RAM, the subsystems that operate on it, and the
// Lua VM running the current cart. Platform code feeds it input, calls tick()
// at 60 Hz, presents display() and pulls audio from audio.render().
class Console {
 public:
  enum class State { kEmpty, kRunning, kError };

  Console();
  ~Console();

  bool run(const cart::Cart& cart, uint64_t seed);
  void stop();
  void tick();

  State state() const { return state_; }
  const script::ScriptError& error() const { return error_; }
  const cart::Cart& cart() const { return cart_; }
  uint64_t frames() const { return frames_; }
  double cpu_usage() const { return vm_ ? vm_->cpu_usage() : 0.0; }
  size_t heap_bytes() const { return vm_ ? vm_->heap_bytes() : 0; }

  // RGB of the last completed frame; mid-frame draws never tear onto it.
  const uint32_t* display() const { return display_.data(); }

  std::span<const uint8_t> persist() const { return mem.region(addr::kPersist, addr::kPersistSize); }
  void set_persist(std::span<const uint8_t> data);

  std::function<void(std::string_view)> on_log;
  std::function<void()> on_persist;

  Memory mem;
  gfx::Gfx gfx{mem};
  audio::Audio audio;
  input::Input input;

 private:
  void present();
  void show_error();
  void sync_registers();
  script::Host make_host();

  State state_ = State::kEmpty;
  cart::Cart cart_;
  std::unique_ptr<script::Vm> vm_;
  script::ScriptError error_;
  uint64_t frames_ = 0;
  std::array<uint32_t, kScreenW * kScreenH> display_{};
};

}  // namespace glim
