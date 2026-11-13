#include "editors/studio.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "editors/code_editor.hpp"
#include "gfx/font.hpp"

namespace glim::editors {

namespace {
using input::key_from_name;
constexpr int kMessageFrames = 150;

uint64_t seed() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()); }
}  // namespace

Studio::Studio(Console& console, Host host)
    : console_(console), host_(std::move(host)), ui_(console.input) {
  editors_.push_back(std::make_unique<CodeEditor>(*this));
}

Studio::~Studio() = default;

CodeEditor& Studio::code_editor() { return static_cast<CodeEditor&>(*editors_[0]); }

void Studio::open(const cart::Cart& cart, const std::string& path) {
  if (playing_) stop();
  cart_ = cart;
  path_ = path;
  dirty_ = false;
  for (auto& e : editors_) e->reset();
  active_ = 0;
  sync_audio();
}

void Studio::select_editor(int index) { active_ = std::clamp(index, 0, int(editors_.size()) - 1); }

void Studio::message(const std::string& text, bool error) {
  message_ = text;
  message_error_ = error;
  message_frames_ = kMessageFrames;
}

void Studio::set_clipboard(const std::string& text) {
  if (host_.set_clipboard) host_.set_clipboard(text);
  clip_fallback_ = text;
}

void Studio::sync_audio() {
  cart_.load_into(audio_mem_);
  console_.audio.sync(audio_mem_);
}

void Studio::play() {
  console_.audio.reset();
  console_.run(cart_, seed());
  playing_ = true;
  showing_error_ = false;
}

void Studio::stop() {
  if (!playing_) return;
  playing_ = false;
  bool crashed = console_.state() == Console::State::kError;
  if (crashed) {
    const auto& e = console_.error();
    code_editor().show_error(e.line, e.message);
    select_editor(0);
    message("runtime error on line " + std::to_string(e.line) + ": " + e.message, true);
  }
  console_.stop();
  console_.input.release_all();
  sync_audio();
}

bool Studio::save() {
  std::string target = path_.empty() ? "untitled.glim" : path_;
  auto r = cart::save_file(target, cart_);
  if (!r.ok) {
    message(r.error, true);
    return false;
  }
  path_ = target;
  dirty_ = false;
  message("saved " + std::filesystem::path(target).filename().string());
  return true;
}

void Studio::tick() {
  if (playing_) tick_game();
  else tick_editor();
}

const uint32_t* Studio::display() { return playing_ ? console_.display() : ui_.display(); }

bool Studio::hotkey(const char* name) {
  // Studio hotkeys watch live key state with their own edge detection: the
  // cart's input is only latched at frame starts, which may be rare (or
  // never, if the cart crashed while booting).
  input::Key k = key_from_name(name);
  bool down = console_.input.live_key(k), was = hotkey_prev_[k];
  hotkey_prev_[k] = down;
  return down && !was;
}

void Studio::tick_game() {
  auto& in = console_.input;
  bool esc = hotkey("escape"), f7 = hotkey("f7"), r = hotkey("r");
  bool ctrl = in.live_key(key_from_name("lctrl")) || in.live_key(key_from_name("rctrl")) ||
              in.live_key(key_from_name("lgui")) || in.live_key(key_from_name("rgui"));
  if (console_.state() != Console::State::kRunning) {
    // Any key or click dismisses the error screen.
    in.begin_frame();
    if (in.any_key_pressed() || in.mouse().buttons || esc) stop();
    return;
  }
  if (esc) {
    stop();
    return;
  }
  if (ctrl && r) {
    play();
    return;
  }
  console_.tick();
  if (f7) {
    // Capture the current screen as the cart's label, like snapping a photo for the box art.
    cart_.label.resize(addr::kScreenSize);
    for (uint32_t i = 0; i < addr::kScreenSize; ++i) {
      uint8_t px = console_.mem.peek(addr::kScreen + i) & 31;
      cart_.label[i] = console_.mem.peek(addr::kScreenPal + px) & 31;
    }
    dirty_ = true;
    message("label captured");
  }
}

void Studio::shortcuts() {
  auto& in = console_.input;
  bool ctrl = in.ctrl();
  auto kp = [&](const char* k) { return in.keyp(key_from_name(k)); };
  if (ctrl && kp("r")) {
    play();
    return;
  }
  if (ctrl && kp("s")) save();
  if (ctrl && kp("z")) {
    if (in.shift()) editors_[size_t(active_)]->redo();
    else editors_[size_t(active_)]->undo();
  }
  if (ctrl && kp("y")) editors_[size_t(active_)]->redo();
  if (ctrl && kp("c")) editors_[size_t(active_)]->copy();
  if (ctrl && kp("x")) editors_[size_t(active_)]->cut();
  if (ctrl && kp("v")) editors_[size_t(active_)]->paste();
  if (ctrl && kp("tab")) select_editor((active_ + (in.shift() ? int(editors_.size()) - 1 : 1)) % int(editors_.size()));
  if (in.alt()) {
    for (int i = 0; i < int(editors_.size()); ++i)
      if (kp(std::to_string(i + 1).c_str())) select_editor(i);
  }
}

void Studio::draw_top_bar() {
  const auto& th = ui_.theme();
  ui_.fill({0, 0, kScreenW, kTopH}, th.raised);
  for (int i = 0; i < int(editors_.size()); ++i) {
    auto& e = *editors_[size_t(i)];
    std::string tip = std::string(e.name()) + " (alt+" + std::to_string(i + 1) + ")";
    if (ui_.icon_button(1 + i * 10, -1, e.icon(), i == active_, tip)) select_editor(i);
  }
  std::string title = cart_.get_meta("title");
  if (title.empty()) title = path_.empty() ? "untitled" : std::filesystem::path(path_).stem().string();
  if (dirty_) title += "*";
  ui_.text_centered(title, {0, 0, kScreenW, kTopH}, th.text);
  if (ui_.icon_button(kScreenW - 22, -1, ui::Icon::kSave, false, "save (ctrl+s)")) save();
  if (ui_.icon_button(kScreenW - 11, -1, ui::Icon::kPlay, false, "run (ctrl+r)")) play();
}

void Studio::draw_status_bar() {
  const auto& th = ui_.theme();
  int y = kScreenH - kStatusH;
  ui_.fill({0, y, kScreenW, kStatusH}, th.raised);
  std::string text = message_frames_ > 0 ? message_ : editors_[size_t(active_)]->status();
  bool error = message_frames_ > 0 && message_error_;
  int max_chars = (kScreenW - 4) / gfx::kCharW;
  if (int(text.size()) > max_chars) text = text.substr(0, size_t(max_chars - 1)) + ".";
  ui_.text(text, 2, y + 2, error ? th.error : th.dim);
  if (message_frames_ > 0) --message_frames_;
}

void Studio::tick_editor() {
  for (auto k : {"escape", "f7", "r"}) hotkey(k);
  console_.input.begin_frame();
  ui_.begin_frame();
  shortcuts();
  if (playing_) return;
  ui_.fill({0, 0, kScreenW, kScreenH}, ui_.theme().panel);
  ui::Rect body{0, kTopH, kScreenW, kScreenH - kTopH - kStatusH};
  editors_[size_t(active_)]->update(ui_, body);
  draw_top_bar();
  draw_status_bar();
  ui_.end_frame();
  sync_audio();
}

}  // namespace glim::editors
