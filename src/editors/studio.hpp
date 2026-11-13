#pragma once

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cart/cart.hpp"
#include "cart/gif.hpp"
#include "editors/editor.hpp"
#include "runtime/console.hpp"
#include "ui/ui.hpp"

namespace glim::editors {

class CodeEditor;
class Shell;

// The workspace around a cart: the editor tabs, the cart being edited, and
// switching between editing and playing. Editors change `cart()`; playing
// copies it into the console, so a running game can never corrupt the
// source being edited.
class Studio {
 public:
  static constexpr int kTopH = 8;
  static constexpr int kStatusH = 8;

  struct Host {
    std::function<std::string()> get_clipboard;
    std::function<void(const std::string&)> set_clipboard;
    std::string capture_dir = {};  // where F6 screenshots and F8 recordings go
    std::string runtime = {};      // this program, copied by the shell's bundle command
  };

  Studio(Console& console, Host host);
  ~Studio();

  void open(const cart::Cart& cart, const std::string& path, bool show_code = true);
  // Player mode is for published games: no editors, and Esc shows a pause
  // overlay instead.
  void set_player_mode(bool on) { player_ = on; }
  bool player_mode() const { return player_; }
  void new_cart();
  // Shows the boot animation, then the shell.
  void boot();
  void tick();
  const uint32_t* display();

  void play();
  void stop();
  // Esc during play pauses; resuming applies edits to the running game.
  void pause();
  void resume();
  bool paused() const { return paused_; }
  bool debugging() const { return debugging_; }
  void set_breakpoints(const std::set<int>& lines) { console_.set_breakpoints(lines); }
  void debug_continue(script::StepMode mode);
  void screenshot();
  void toggle_recording();
  bool recording() const { return gif_ != nullptr; }
  void set_capture_dir(const std::string& dir) { host_.capture_dir = dir; }
  void set_runtime(const std::string& path) { host_.runtime = path; }
  const std::string& runtime() const { return host_.runtime; }
  bool playing() const { return playing_; }
  bool save();
  bool save_as(const std::string& path);

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
  Shell& shell();

 private:
  void tick_editor();
  void tick_game();
  void draw_top_bar();
  void draw_status_bar();
  void draw_debug_panel(const ui::Rect& r);
  void shortcuts();
  bool hotkey(const char* name);
  const uint32_t* frame();
  void draw_overlays();
  std::string capture_path(const char* ext) const;

  Console& console_;
  Host host_;
  ui::Ui ui_;
  cart::Cart cart_;
  cart::Cart* cart_ptr_ = &cart_;
  std::string path_;
  std::vector<std::unique_ptr<Editor>> editors_;
  int active_ = 0;
  bool playing_ = false;
  bool paused_ = false;
  bool debugging_ = false;
  bool player_ = false;
  bool player_paused_ = false;
  int debug_scroll_ = 0;
  std::set<std::string> debug_open_;  // expanded table variables
  bool dirty_ = false;
  bool showing_error_ = false;
  std::string message_;
  bool message_error_ = false;
  int message_frames_ = 0;
  std::string clip_fallback_;
  Memory audio_mem_;
  std::array<bool, input::kKeyCount> hotkey_prev_{};

  std::unique_ptr<cart::GifWriter> gif_;
  std::string gif_path_;
  int gif_ticks_ = 0;
  std::vector<uint32_t> out_ = std::vector<uint32_t>(kScreenW * kScreenH);
  std::string toast_;
  int toast_frames_ = 0;
  bool perf_ = false;
  std::array<float, 64> cpu_history_{};
  int cpu_head_ = 0;
  uint64_t last_frames_ = 0;
  int fps_ = 60, fps_ticks_ = 0;
};

}  // namespace glim::editors
