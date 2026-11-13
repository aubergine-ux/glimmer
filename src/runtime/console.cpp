#include "runtime/console.hpp"

#include <algorithm>

#include "gfx/font.hpp"

namespace glim {

namespace {
constexpr int kErrorBg = 1;
constexpr int kErrorBanner = 10;
constexpr int kErrorText = 5;
constexpr int kErrorDim = 6;
constexpr int kWrapColumns = (kScreenW - 8) / gfx::kCharW;
}  // namespace

Console::Console() {
  gfx.reset_palette();
  gfx.reset_state();
}

Console::~Console() = default;

script::Host Console::make_host() {
  return script::Host{
      mem,
      gfx,
      audio,
      input,
      [this](std::string_view s) {
        if (on_log) on_log(s);
      },
      [this] {
        if (on_persist) on_persist();
      },
      [this] { return frames_; },
  };
}

bool Console::run(const cart::Cart& cart, uint64_t seed) {
  stop();
  cart_ = cart;
  // Save data belongs to the player, not the cart image, so it survives reloads.
  auto saved = std::vector<uint8_t>(persist().begin(), persist().end());
  mem.clear();
  cart_.load_into(mem);
  set_persist(saved);
  gfx.reset_state();
  gfx.cls(0);
  audio.reset();
  audio.sync(mem);
  frames_ = 0;
  error_ = {};

  vm_ = std::make_unique<script::Vm>(make_host());
  if (!vm_->load(cart_.code, seed)) {
    error_ = vm_->error();
    state_ = State::kError;
    show_error();
    return false;
  }
  state_ = State::kRunning;
  return true;
}

void Console::stop() {
  vm_.reset();
  audio.reset();
  state_ = State::kEmpty;
}

void Console::set_persist(std::span<const uint8_t> data) {
  std::copy_n(data.begin(), std::min<size_t>(data.size(), addr::kPersistSize), mem.data() + addr::kPersist);
}

void Console::sync_registers() {
  for (int c = 0; c < kChannels; ++c) {
    auto st = audio.status(c);
    mem.poke(addr::kChanSfx + c, uint8_t(int8_t(st.sfx)));
    mem.poke(addr::kChanStep + c, uint8_t(st.step));
  }
  mem.poke(addr::kMusicPattern, uint8_t(int8_t(audio.music_pattern())));
}

void Console::tick() {
  if (state_ != State::kRunning) return;

  bool new_frame = vm_->booted() && !vm_->frame_pending();
  if (new_frame) {
    input.begin_frame();
    input.write_registers(mem);
    sync_registers();
  }

  auto status = vm_->tick();
  // Sound data may have been poked; the synth picks it up at frame granularity.
  audio.sync(mem);

  switch (status) {
    case script::Vm::Status::kDone:
      if (new_frame) ++frames_;
      present();
      break;
    case script::Vm::Status::kYielded:
      break;
    case script::Vm::Status::kError:
      error_ = vm_->error();
      state_ = State::kError;
      audio.sfx(-1);
      audio.music(-1);
      show_error();
      break;
  }
}

void Console::present() { gfx.to_rgb(display_.data()); }

void Console::show_error() {
  // Drawn with a clean draw state and palette so a cart's palette tricks can't hide it.
  gfx.reset_state();
  gfx.reset_palette();
  gfx.cls(kErrorBg);
  gfx.rectfill(0, 0, kScreenW - 1, 8, kErrorBanner);
  std::string title = "runtime error";
  if (error_.line > 0) title += " on line " + std::to_string(error_.line);
  gfx.print(title, 4, 2, kErrorText);

  int y = 14;
  auto wrap = [&](std::string_view text, int color) {
    while (!text.empty() && y < kScreenH - gfx::kCharH) {
      auto nl = text.find('\n');
      auto line = text.substr(0, nl);
      while (!line.empty() && y < kScreenH - gfx::kCharH) {
        auto part = line.substr(0, kWrapColumns);
        gfx.print(part, 4, y, color);
        line.remove_prefix(part.size());
        y += gfx::kCharH;
      }
      if (nl == std::string_view::npos) break;
      text.remove_prefix(nl + 1);
    }
  };
  wrap(error_.message, kErrorText);
  y += 4;
  wrap(error_.traceback, kErrorDim);
  present();
}

}  // namespace glim
