#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cart/cart.hpp"
#include "editors/editor.hpp"
#include "runtime/console.hpp"
#include "ui/ui.hpp"

namespace glim::editors {

class CodeEditor;

// The workspace around a cart: the editor tabs, the cart being edited, and
// switching between editing and playing. Editors change `cart()`; playing
// copies it into the console, so a running game can never corrupt the
// source being edited.
class Studio {
 public:
  static constexpr int kTopH = 9;
  static constexpr int kStatusH = 8;

  struct Host {
    std::function<std::string()> get_clipboard;
    std::function<void(const std::string&)> set_clipboard;
  };

  Studio(Console& console, Host host);
  ~Studio();

  void open(const cart::Cart& cart, const std::string& path);
  void tick();
  const uint32_t* display();

  void play();
  void stop();
  bool playing() const { return playing_; }
  bool save();

  cart::Cart& cart() { return cart_; }
  cart::Cart*& cart_ref() { return cart_ptr_; }
  const std::string& path() const { return path_; }
  Console& console() { return console_; }
  ui::Ui& ui() { return ui_; }

  void message(const std::string& text, bool error = false);
  void mark_dirty() { dirty_ = true; }
  bool dirty() const { return dirty_; }

  std::string clipboard() const { return host_.get_clipboard ? host_.get_clipboard() : clip_fallback_; }
  void set_clipboard(const std::string& text);

  // Sound previews in the editors play the cart being edited, not console RAM.
  void sync_audio();
  void select_editor(int index);
  int active_editor() const { return active_; }
  CodeEditor& code_editor();

 private:
  void tick_editor();
  void tick_game();
  void draw_top_bar();
  void draw_status_bar();
  void shortcuts();
  bool hotkey(const char* name);

  Console& console_;
  Host host_;
  ui::Ui ui_;
  cart::Cart cart_;
  cart::Cart* cart_ptr_ = &cart_;
  std::string path_;
  std::vector<std::unique_ptr<Editor>> editors_;
  int active_ = 0;
  bool playing_ = false;
  bool dirty_ = false;
  bool showing_error_ = false;
  std::string message_;
  bool message_error_ = false;
  int message_frames_ = 0;
  std::string clip_fallback_;
  Memory audio_mem_;
  std::array<bool, input::kKeyCount> hotkey_prev_{};
};

}  // namespace glim::editors
