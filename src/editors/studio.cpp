#include "editors/studio.hpp"
#include "ui/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "editors/code_editor.hpp"
#include "editors/shell.hpp"
#include "editors/map_editor.hpp"
#include "editors/music_editor.hpp"
#include "editors/sfx_editor.hpp"
#include "editors/sprite_editor.hpp"
#include "cart/image.hpp"
#include "gfx/font.hpp"

namespace glim::editors {

using ui::tr;

namespace {
using input::key_from_name;
constexpr int kMessageFrames = 150;
constexpr int kToastFrames = 120;
constexpr int kMaxRecordTicks = 30 * kFps;
constexpr int kScreenshotScale = 4;
constexpr int kShellTab = 0;
constexpr int kCodeTab = 1;

constexpr const char* kStarterCode = R"lua(-- a new glimmer cart
-- ctrl+r runs it, esc comes back here.

function _init()
  t0 = 0
end

function _update()
  t0 = t0 + 1
end

function _draw()
  cls(1)
  local msg = "hello!"
  print(msg, 128 - textwidth(msg) / 2, 68 + sin(t0 / 60) * 4, 17)
end
)lua";

// Text straight onto an RGB frame, for overlays that sit above everything.
void rgb_text(std::vector<uint32_t>& buf, std::string_view s, int x, int y, uint32_t color, uint32_t shadow) {
  for (int pass = 0; pass < 2; ++pass) {
    int cx = x + (pass == 0 ? 1 : 0), cy = y + (pass == 0 ? 1 : 0);
    for (uint8_t ch : gfx::glyphs(s)) {
      for (int row = 0; row < gfx::kGlyphRows; ++row) {
        uint8_t bits = gfx::glyph_row(uint8_t(ch), row);
        for (int col = 0; col < gfx::kGlyphW; ++col) {
          int px = cx + col, py = cy + row;
          if (!((bits >> (gfx::kGlyphW - 1 - col)) & 1) || px < 0 || py < 0 || px >= kScreenW || py >= kScreenH) continue;
          buf[size_t(py) * kScreenW + px] = pass == 0 ? shadow : color;
        }
      }
      cx += gfx::kCharW;
    }
  }
}

void rgb_rect(std::vector<uint32_t>& buf, int x0, int y0, int x1, int y1, uint32_t color) {
  for (int y = std::max(0, y0); y <= std::min(kScreenH - 1, y1); ++y)
    for (int x = std::max(0, x0); x <= std::min(kScreenW - 1, x1); ++x) buf[size_t(y) * kScreenW + x] = color;
}

uint64_t seed() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()); }
}  // namespace

Studio::Studio(Console& console, Host host)
    : console_(console), host_(std::move(host)), ui_(console.input) {
  editors_.push_back(std::make_unique<Shell>(*this));
  editors_.push_back(std::make_unique<CodeEditor>(*this));
  editors_.push_back(std::make_unique<SpriteEditor>(*this));
  editors_.push_back(std::make_unique<MapEditor>(*this));
  editors_.push_back(std::make_unique<SfxEditor>(*this));
  editors_.push_back(std::make_unique<MusicEditor>(*this));
}

Studio::~Studio() = default;

CodeEditor& Studio::code_editor() { return static_cast<CodeEditor&>(*editors_[kCodeTab]); }
Shell& Studio::shell() { return static_cast<Shell&>(*editors_[kShellTab]); }

void Studio::set_high_contrast(bool on) {
  high_contrast_ = on;
  ui_.set_theme(on ? ui::high_contrast_theme() : ui::default_theme());
}

void Studio::new_cart() {
  cart::Cart c;
  c.code = kStarterCode;
  open(c, "", false);
}

void Studio::boot() {
  shell().boot();
  select_editor(kShellTab);
}

void Studio::open(const cart::Cart& cart, const std::string& path, bool show_code) {
  if (playing_ || paused_) {
    playing_ = true;
    stop();
  }
  cart_ = cart;
  path_ = path;
  dirty_ = false;
  for (auto& e : editors_) e->reset();
  if (show_code) active_ = kCodeTab;
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
  paused_ = false;
  showing_error_ = false;
}

void Studio::pause() {
  playing_ = false;
  paused_ = true;
  console_.input.release_all();
  message(tr("paused: esc resumes with your changes, ctrl+r restarts"));
}

void Studio::resume() {
  if (!paused_ || console_.state() != Console::State::kRunning) {
    play();
    return;
  }
  if (!console_.reload(cart_)) {
    const auto& e = console_.error();
    code_editor().show_error(e.line, e.message);
    select_editor(kCodeTab);
    message(tr("can't resume, line {0}: {1}", std::to_string(e.line), e.message), true);
    return;
  }
  playing_ = true;
  paused_ = false;
  console_.input.release_all();
}

void Studio::debug_continue(script::StepMode mode) {
  debugging_ = false;
  code_editor().show_exec_line(0);
  console_.resume_debug(mode);
  playing_ = true;
}

void Studio::stop() {
  if (!playing_) return;
  playing_ = false;
  paused_ = false;
  debugging_ = false;
  code_editor().show_exec_line(0);
  bool crashed = console_.state() == Console::State::kError;
  if (crashed) {
    const auto& e = console_.error();
    code_editor().show_error(e.line, e.message);
    select_editor(kCodeTab);
    message(tr("runtime error on line {0}: {1}", std::to_string(e.line), e.message), true);
  }
  console_.stop();
  console_.input.release_all();
  sync_audio();
}

bool Studio::save_as(const std::string& path) {
  std::string old = path_;
  path_ = path;
  if (save()) return true;
  path_ = old;
  return false;
}

bool Studio::save() {
  std::string target = path_.empty() ? (shell().dir() / "untitled.glim").string() : path_;
  auto r = cart::save_file(target, cart_);
  if (!r.ok) {
    message(r.error, true);
    return false;
  }
  path_ = target;
  dirty_ = false;
  message(tr("saved {0}", std::filesystem::path(target).filename().string()));
  return true;
}

void Studio::tick() {
  // Capture keys work in every mode.
  bool f3 = hotkey("f3"), f6 = hotkey("f6"), f8 = hotkey("f8");
  if (playing_) tick_game();
  else tick_editor();
  if (f3) perf_ = !perf_;
  if (f6) screenshot();
  if (f8) toggle_recording();
  if (gif_) {
    // 30 fps: every other tick, alternating 3 and 4 centisecond delays.
    if (gif_ticks_ % 2 == 0) gif_->add_frame(frame(), (gif_ticks_ / 2) % 3 == 2 ? 4 : 3);
    if (++gif_ticks_ >= kMaxRecordTicks) toggle_recording();
  }

  if (++fps_ticks_ >= kFps) {
    fps_ = int(console_.frames() - last_frames_);
    last_frames_ = console_.frames();
    fps_ticks_ = 0;
  }
  cpu_history_[size_t(cpu_head_)] = playing_ ? float(console_.cpu_usage()) : 0.f;
  cpu_head_ = (cpu_head_ + 1) % int(cpu_history_.size());
  if (toast_frames_ > 0) --toast_frames_;
}

const uint32_t* Studio::frame() { return playing_ ? console_.display() : ui_.display(); }

const uint32_t* Studio::display() {
  const uint32_t* base = frame();
  std::copy(base, base + out_.size(), out_.begin());
  draw_overlays();
  return out_.data();
}

std::string Studio::capture_path(const char* ext) const {
  char stamp[32];
  std::time_t now = std::time(nullptr);
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
  std::string name = cart_.get_meta("title");
  if (name.empty()) name = path_.empty() ? "glimmer" : std::filesystem::path(path_).stem().string();
  std::filesystem::path dir = host_.capture_dir.empty() ? std::filesystem::path(".") : std::filesystem::path(host_.capture_dir);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return (dir / (name + "-" + stamp + ext)).string();
}

void Studio::screenshot() {
  const uint32_t* px = frame();
  cart::Image img(kScreenW * kScreenshotScale, kScreenH * kScreenshotScale);
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x) {
      uint32_t c = px[size_t(y / kScreenshotScale) * kScreenW + x / kScreenshotScale];
      uint8_t* o = img.at(x, y);
      o[0] = uint8_t(c >> 16);
      o[1] = uint8_t(c >> 8);
      o[2] = uint8_t(c);
      o[3] = 255;
    }
  std::string path = capture_path(".png");
  bool ok = cart::write_file(path, cart::encode_png(img));
  toast_ = ok ? tr("saved {0}", std::filesystem::path(path).filename().string()) : tr("can't save screenshot");
  toast_frames_ = kToastFrames;
}

void Studio::toggle_recording() {
  if (gif_) {
    bool ok = cart::write_file(gif_path_, gif_->finish());
    gif_.reset();
    toast_ = ok ? tr("saved {0}", std::filesystem::path(gif_path_).filename().string()) : tr("can't save recording");
    toast_frames_ = kToastFrames;
    return;
  }
  gif_path_ = capture_path(".gif");
  gif_ = std::make_unique<cart::GifWriter>(kScreenW, kScreenH, 2);
  gif_ticks_ = 0;
}

void Studio::draw_overlays() {
  if (player_ && player_paused_) {
    // Dim the frozen game and show what the keys do.
    for (auto& px : out_) px = (px >> 2) & 0x3F3F3F;
    std::string lines[] = {tr("paused"), tr("esc or enter: resume"), tr("ctrl+r: restart")};
    for (int i = 0; i < 3; ++i) {
      int w = gfx::Gfx::text_width(lines[i]);
      rgb_text(out_, lines[i], (kScreenW - w) / 2, 58 + i * 10, i == 0 ? 0xffd166 : 0xf2e9ff, 0x0b0a12);
    }
  }
  if (gif_ && (gif_ticks_ / 20) % 2 == 0) {
    rgb_rect(out_, kScreenW - 7, 2, kScreenW - 3, 6, 0x000000);
    rgb_rect(out_, kScreenW - 6, 3, kScreenW - 4, 5, 0xe8534a);
  }
  if (toast_frames_ > 0) {
    int w = gfx::Gfx::text_width(toast_) + 6;
    rgb_rect(out_, (kScreenW - w) / 2, kScreenH - 16, (kScreenW + w) / 2, kScreenH - 7, 0x1f1a33);
    rgb_text(out_, toast_, (kScreenW - w) / 2 + 3, kScreenH - 14, 0xf2e9ff, 0x0b0a12);
  }
  if (perf_ && playing_) {
    int x0 = kScreenW - 70, y0 = 2;
    rgb_rect(out_, x0, y0, kScreenW - 2, y0 + 33, 0x0b0a12);
    char line[40];
    std::snprintf(line, sizeof line, "cpu %3d%%  fps %d", int(console_.cpu_usage() * 100), fps_);
    rgb_text(out_, line, x0 + 2, y0 + 2, console_.cpu_usage() > 1.0 ? 0xe8534a : 0xf2e9ff, 0x0b0a12);
    std::snprintf(line, sizeof line, "lua %zuk", console_.heap_bytes() / 1024);
    rgb_text(out_, line, x0 + 2, y0 + 9, 0x8a8fa8, 0x0b0a12);
    // Frame cost history; the faint line marks 100% of the budget.
    int gy = y0 + 31;
    for (int i = 0; i < 64; ++i) {
      float v = cpu_history_[size_t((cpu_head_ + i) % 64)];
      int h = v > 0.f ? std::clamp(int(v * 12), 1, 14) : 0;
      if (h > 0) rgb_rect(out_, x0 + 2 + i, gy - h, x0 + 2 + i, gy, v > 1.f ? 0xe8534a : 0x6cc551);
    }
    rgb_rect(out_, x0 + 2, gy - 12, x0 + 65, gy - 12, 0x3a2f5b);
  }
}

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
    // Any key or click dismisses the error screen; published games restart.
    in.begin_frame();
    if (in.any_key_pressed() || in.mouse().buttons || esc) {
      if (player_) play();
      else stop();
    }
    return;
  }
  if (player_) {
    if (esc) {
      player_paused_ = !player_paused_;
      in.release_all();
    }
    if (ctrl && r) {
      player_paused_ = false;
      play();
      return;
    }
    if (player_paused_) {
      in.begin_frame();
      if (in.keyp(key_from_name("return")) || in.mouse_pressed(0)) player_paused_ = false;
      return;
    }
    console_.tick();
    return;
  }
  if (esc) {
    pause();
    return;
  }
  if (ctrl && r) {
    play();
    return;
  }
  console_.tick();
  if (console_.stopped()) {
    // Hit a breakpoint: show where, with the debugger beside the code.
    playing_ = false;
    debugging_ = true;
    console_.input.release_all();
    code_editor().show_exec_line(console_.debug_state()->line);
    select_editor(kCodeTab);
    return;
  }
  if (f7) {
    // Capture the current screen as the cart's label, like snapping a photo for the box art.
    cart_.label.resize(addr::kScreenSize);
    for (uint32_t i = 0; i < addr::kScreenSize; ++i) {
      uint8_t px = console_.mem.peek(addr::kScreen + i) & 31;
      cart_.label[i] = console_.mem.peek(addr::kScreenPal + px) & 31;
    }
    dirty_ = true;
    message(tr("label captured"));
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
    std::string tip = tr(e.name()) + " (alt+" + std::to_string(i + 1) + ")";
    if (ui_.icon_button(1 + i * 10, 0, e.icon(), i == active_, tip, kTopH)) select_editor(i);
  }
  std::string title = cart_.get_meta("title");
  if (title.empty()) title = path_.empty() ? tr("untitled") : std::filesystem::path(path_).stem().string();
  if (dirty_) title += "*";
  ui_.text_centered(title, {0, 0, kScreenW, kTopH}, th.text);
  if (ui_.icon_button(kScreenW - 22, 0, ui::Icon::kSave, false, tr("save (ctrl+s)"), kTopH)) save();
  if (paused_) {
    ui_.text(tr("paused"), kScreenW - 26 - gfx::Gfx::text_width(tr("paused")), 2, th.warn);
    if (ui_.icon_button(kScreenW - 11, 0, ui::Icon::kPlay, true, tr("resume with changes (esc)"), kTopH)) resume();
  } else if (ui_.icon_button(kScreenW - 11, 0, ui::Icon::kPlay, false, tr("run (ctrl+r)"), kTopH)) {
    play();
  }
}

void Studio::draw_debug_panel(const ui::Rect& r) {
  const auto& th = ui_.theme();
  auto& g = ui_.g();
  ui_.fill(r, th.panel);
  g.line(r.x, r.y, r.x, r.bottom(), th.shadow);
  int x = r.x + 2, y = r.y + 1;
  if (ui_.icon_button(x, y, ui::Icon::kPlay, false, tr("continue (f5)"))) debug_continue(script::StepMode::kNone);
  if (ui_.button({x + 11, y + 1, 17, 8}, "over", false, tr("step over (f10)"))) debug_continue(script::StepMode::kOver);
  if (ui_.button({x + 29, y + 1, 17, 8}, "into", false, tr("step into (f11)"))) debug_continue(script::StepMode::kInto);
  if (ui_.button({x + 47, y + 1, 13, 8}, "out", false, tr("step out (shift+f11)"))) debug_continue(script::StepMode::kOut);
  if (ui_.icon_button(x + 61, y, ui::Icon::kStop, false, tr("stop the game"))) {
    playing_ = true;
    stop();
  }
  if (!debugging_ || !console_.debug_state()) return;

  const auto& d = *console_.debug_state();
  // Flatten frames, variables and expanded tables into rows, then scroll.
  struct Row {
    std::string text;
    int color;
    std::string key;  // expandable when not empty
  };
  std::vector<Row> rows;
  auto add_vars = [&](const std::vector<script::DebugVar>& vars, const std::string& scope) {
    for (const auto& v : vars) {
      std::string key = v.fields.empty() ? "" : scope + "." + v.name;
      bool open = !key.empty() && debug_open_.count(key);
      std::string mark = key.empty() ? " " : (open ? "\x83" : "\x81");
      rows.push_back({mark + v.name + " " + v.value, v.name[0] == '^' ? th.dim : th.text, key});
      if (open)
        for (const auto& [k, val] : v.fields) rows.push_back({"   " + k + " " + val, th.dim, ""});
    }
  };
  for (size_t i = 0; i < d.frames.size(); ++i) {
    const auto& f = d.frames[i];
    rows.push_back({f.function + "() :" + std::to_string(f.line), i == 0 ? 17 : th.dim, ""});
    add_vars(f.vars, std::to_string(i));
  }
  if (!d.globals.empty()) {
    rows.push_back({"globals", 25, ""});
    add_vars(d.globals, "g");
  }
  ui::Rect list{r.x + 1, r.y + 12, r.w - 1, r.h - 12};
  int visible = list.h / gfx::kCharH;
  if (ui_.hovered(list) && ui_.wheel()) debug_scroll_ -= ui_.wheel() * 2;
  debug_scroll_ = std::clamp(debug_scroll_, 0, std::max(0, int(rows.size()) - visible));
  g.clip(list.x, list.y, list.w, list.h);
  for (int i = 0; i < visible && debug_scroll_ + i < int(rows.size()); ++i) {
    const Row& row = rows[size_t(debug_scroll_ + i)];
    int ry = list.y + i * gfx::kCharH;
    ui::Rect hit{list.x, ry, list.w, gfx::kCharH};
    if (!row.key.empty() && ui_.hovered(hit)) ui_.fill(hit, th.raised);
    ui_.text(row.text, list.x + 2, ry, row.color);
    if (!row.key.empty() && ui_.clicked_in(hit) && !debug_open_.erase(row.key)) debug_open_.insert(row.key);
  }
  g.clip();
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
  bool esc = hotkey("escape");
  hotkey("f7");
  hotkey("r");
  bool wants_escape = editors_[size_t(active_)]->wants_escape();
  console_.input.begin_frame();
  ui_.begin_frame();
  shortcuts();
  if (playing_) return;
  if (shell().booting()) {
    std::copy(cart_.palette.begin(), cart_.palette.end(), ui_.mem().data() + addr::kPalette);
    if (shell().update_boot(ui_)) return;
  }
  if (debugging_) {
    auto& in = console_.input;
    auto kp = [&](const char* k) { return in.keyp(key_from_name(k)); };
    if (kp("f5")) debug_continue(script::StepMode::kNone);
    else if (kp("f10")) debug_continue(script::StepMode::kOver);
    else if (kp("f11")) debug_continue(in.shift() ? script::StepMode::kOut : script::StepMode::kInto);
    if (playing_) return;
  }
  if (paused_ && ((esc && !wants_escape) || (console_.input.ctrl() && console_.input.keyp(key_from_name("return"))))) {
    resume();
    if (playing_) return;
  }
  // The editors show art in the cart's own colors; the high-contrast theme
  // relies on the default ones, so it keeps them.
  if (high_contrast_) ui_.g().reset_palette();
  else std::copy(cart_.palette.begin(), cart_.palette.end(), ui_.mem().data() + addr::kPalette);
  ui_.fill({0, 0, kScreenW, kScreenH}, ui_.theme().panel);
  ui::Rect body{0, kTopH, kScreenW, kScreenH - kTopH - kStatusH};
  if (debugging_ && active_ == kCodeTab) {
    constexpr int kPanelW = 96;
    editors_[size_t(active_)]->update(ui_, {body.x, body.y, body.w - kPanelW, body.h});
    draw_debug_panel({body.right() - kPanelW + 1, body.y, kPanelW, body.h});
  } else {
    editors_[size_t(active_)]->update(ui_, body);
  }
  draw_top_bar();
  draw_status_bar();
  ui_.end_frame();
  sync_audio();
}

}  // namespace glim::editors
