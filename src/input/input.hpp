#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory.hpp"

namespace glim::input {

enum Button : uint8_t { kLeft, kRight, kUp, kDown, kA, kB, kX, kY, kL, kR, kButtonCount };

// Keys are USB HID usage ids, which is also what SDL scancodes use, so the
// platform layer can pass them through unchanged.
using Key = uint16_t;
inline constexpr int kKeyCount = 256;

Key key_from_name(std::string_view name);  // 0 when unknown
std::string_view key_name(Key k);
int button_from_name(std::string_view name);  // -1 when unknown
std::string_view button_name(int b);

struct Binding {
  Key key;
  uint8_t player;
  uint8_t button;
};

class KeyMap {
 public:
  KeyMap() { reset(); }
  void reset();
  void bind(int player, int button, Key key);
  void clear(int player, int button);
  const std::vector<Binding>& bindings() const { return bindings_; }

  // "p1.a = z, space" per line; '#' starts a comment. Unknown lines are skipped.
  void load(std::string_view text);
  std::string save() const;

 private:
  std::vector<Binding> bindings_;
};

struct Mouse {
  int x = 0, y = 0;
  uint8_t buttons = 0;  // bit 0 left, 1 right, 2 middle
  int wheel = 0;
};

class Input {
 public:
  // Latches the state gathered since the last frame; call once before _update.
  void begin_frame();

  void set_key(Key k, bool down);
  void set_pad_button(int player, int button, bool down);
  void set_mouse(int x, int y, uint8_t buttons);
  void add_wheel(int delta) { wheel_accum_ += delta; }
  void add_text(std::string_view text) { text_accum_ += text; }
  void release_all();

  bool btn(int button, int player = 0) const;
  bool btnp(int button, int player = 0) const;
  uint16_t buttons(int player) const;
  bool key(Key k) const { return k < kKeyCount && keys_[k]; }
  bool keyp(Key k) const { return k < kKeyCount && keys_[k] && !prev_keys_[k]; }
  bool any_key_pressed() const;
  const Mouse& mouse() const { return mouse_; }
  bool mouse_pressed(int b) const { return (mouse_.buttons & ~prev_mouse_buttons_) & (1 << b); }
  const std::string& text() const { return text_; }

  KeyMap& keymap() { return keymap_; }

  void write_registers(Memory& mem) const;

 private:
  std::array<bool, kKeyCount> live_keys_{};
  std::array<bool, kKeyCount> keys_{};
  std::array<bool, kKeyCount> prev_keys_{};
  std::array<uint16_t, kPlayers> pad_live_{};
  std::array<uint16_t, kPlayers> cur_{};
  std::array<uint16_t, kPlayers> prev_{};
  std::array<std::array<uint16_t, kButtonCount>, kPlayers> held_frames_{};
  Mouse live_mouse_;
  Mouse mouse_;
  uint8_t prev_mouse_buttons_ = 0;
  int wheel_accum_ = 0;
  std::string text_accum_;
  std::string text_;
  KeyMap keymap_;
};

}  // namespace glim::input
