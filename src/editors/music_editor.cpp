#include "editors/music_editor.hpp"

#include <algorithm>
#include <cstdio>

#include "editors/clipboard.hpp"
#include "editors/sfx_editor.hpp"
#include "editors/studio.hpp"
#include "ui/i18n.hpp"

namespace glim::editors {

using ui::tr;

namespace {

using input::key_from_name;
constexpr uint8_t kDisabled = 0x40;
constexpr uint8_t kFlag = 0x80;
constexpr int kColW = 61;
constexpr int kGridX = 12;
constexpr int kRowH = 6;
constexpr int kVisibleRows = 13;
constexpr int kPreviewChannel = 3;

// Tracker keyboard: the bottom row plays the lower octave, the top row the upper.
struct NoteKey {
  const char* key;
  int semitone;
};
constexpr NoteKey kNoteKeys[] = {
    {"z", 0},  {"s", 1},  {"x", 2},  {"d", 3},  {"c", 4},  {"v", 5},  {"g", 6},  {"b", 7},  {"h", 8},
    {"n", 9},  {"j", 10}, {"m", 11}, {",", 12}, {"q", 12}, {"2", 13}, {"w", 14}, {"3", 15}, {"e", 16},
    {"r", 17}, {"5", 18}, {"t", 19}, {"6", 20}, {"y", 21}, {"7", 22}, {"u", 23}, {"i", 24},
};
constexpr int kWhiteSemis[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kBlackSemis[5] = {1, 3, 6, 8, 10};
constexpr int kBlackAfterWhite[5] = {0, 1, 3, 4, 5};
constexpr char kEffectGlyph[8] = {'-', '/', '~', '\\', '<', '>', 'a', 'b'};

}  // namespace

MusicEditor::MusicEditor(Studio& studio) : Editor(studio) {}

void MusicEditor::reset() {
  Editor::reset();
  pattern_ = channel_ = row_ = scroll_ = 0;
}

uint8_t MusicEditor::channel_byte(int ch) const { return studio_.cart().music[size_t(pattern_ * kChannels + ch)]; }

void MusicEditor::set_channel_byte(int ch, uint8_t v) {
  history_.begin(Target::kMusic);
  studio_.cart().music[size_t(pattern_ * kChannels + ch)] = v;
  if (history_.commit()) studio_.mark_dirty();
}

audio::Step MusicEditor::step(int ch, int row) const {
  const auto& b = studio_.cart().sfx;
  size_t a = size_t(channel_sfx(ch)) * kSfxBytes + 4 + size_t(row) * 2;
  return audio::Step::decode(uint16_t(b[a] | (b[a + 1] << 8)));
}

void MusicEditor::set_step(int ch, int row, const audio::Step& s) {
  auto& b = studio_.cart().sfx;
  size_t a = size_t(channel_sfx(ch)) * kSfxBytes + 4 + size_t(row) * 2;
  history_.begin(Target::kSfx);
  uint16_t v = s.encode();
  b[a] = uint8_t(v);
  b[a + 1] = uint8_t(v >> 8);
  if (history_.commit()) studio_.mark_dirty();
}

void MusicEditor::enter_note(int pitch) {
  if (!channel_on(channel_)) set_channel_byte(channel_, uint8_t(channel_byte(channel_) & ~kDisabled));
  audio::Step s{uint8_t(std::clamp(pitch, 0, 63)), uint8_t(pen_wave_), uint8_t(pen_volume_), uint8_t(pen_effect_)};
  set_step(channel_, row_, s);
  studio_.sync_audio();
  studio_.console().audio.sfx(channel_sfx(channel_), kPreviewChannel, row_, 1);
  row_ = std::min(row_ + advance_, kSfxSteps - 1);
}

void MusicEditor::clear_note() {
  audio::Step s = step(channel_, row_);
  s.volume = 0;
  set_step(channel_, row_, s);
  row_ = std::min(row_ + advance_, kSfxSteps - 1);
}

void MusicEditor::play(bool from_here) {
  auto& a = studio_.console().audio;
  studio_.sync_audio();
  if (a.music_pattern() >= 0) {
    a.music(-1);
    return;
  }
  if (from_here) {
    a.music(pattern_);
    return;
  }
  // Just this pattern, once.
  for (int ch = 0; ch < kChannels; ++ch)
    if (channel_on(ch)) a.sfx(channel_sfx(ch), ch);
}

void MusicEditor::follow_playback() {
  int p = studio_.console().audio.music_pattern();
  if (p < 0 || !follow_) return;
  pattern_ = p;
  auto st = studio_.console().audio.status(0);
  if (st.sfx >= 0 && (st.step < scroll_ || st.step >= scroll_ + kVisibleRows)) scroll_ = std::clamp(st.step - 2, 0, kSfxSteps - kVisibleRows);
}

void MusicEditor::copy() {
  const auto& m = studio_.cart().music;
  std::vector<uint8_t> bytes(m.begin() + pattern_ * kChannels, m.begin() + (pattern_ + 1) * kChannels);
  studio_.set_clipboard(encode_bytes("pattern", bytes));
  studio_.message(tr("copied pattern {0}", std::to_string(pattern_)));
}

void MusicEditor::paste() {
  std::vector<uint8_t> bytes;
  if (!decode_bytes(studio_.clipboard(), "pattern", bytes) || bytes.size() != size_t(kChannels)) {
    studio_.message(tr("the clipboard has no pattern"), true);
    return;
  }
  history_.begin(Target::kMusic);
  std::copy(bytes.begin(), bytes.end(), studio_.cart().music.begin() + pattern_ * kChannels);
  history_.commit();
  studio_.mark_dirty();
}

void MusicEditor::keys(ui::Ui& ui) {
  auto& in = ui.in();
  if (in.ctrl() || in.alt()) return;
  auto kp = [&](const char* k) { return in.keyp(key_from_name(k)); };
  auto kr = [&](const char* k) { return in.keyr(key_from_name(k)); };
  if (kp("space")) play(!in.shift());
  if (kp("return")) play(false);
  if (in.shift()) {
    // Transpose the note under the cursor.
    int d = kr("up") ? 1 : (kr("down") ? -1 : 0);
    if (d) {
      audio::Step s = step(channel_, row_);
      s.pitch = uint8_t(std::clamp(s.pitch + d, 0, 63));
      set_step(channel_, row_, s);
      studio_.sync_audio();
      studio_.console().audio.sfx(channel_sfx(channel_), kPreviewChannel, row_, 1);
    }
  } else {
    if (kr("up")) row_ = std::max(0, row_ - 1);
    if (kr("down")) row_ = std::min(kSfxSteps - 1, row_ + 1);
  }
  if (kr("left")) channel_ = (channel_ + kChannels - 1) % kChannels;
  if (kr("right") || kp("tab")) channel_ = (channel_ + 1) % kChannels;
  if (kr("pageup")) row_ = std::max(0, row_ - 8);
  if (kr("pagedown")) row_ = std::min(kSfxSteps - 1, row_ + 8);
  if (kr("[")) set_pattern(pattern_ - 1);
  if (kr("]")) set_pattern(pattern_ + 1);
  if (kp("-")) octave_ = std::max(0, octave_ - 1);
  if (kp("=")) octave_ = std::min(4, octave_ + 1);
  if (kr("delete") || kr("backspace")) clear_note();
  for (const auto& nk : kNoteKeys)
    if (kp(nk.key)) enter_note(octave_ * 12 + nk.semitone);
}

void MusicEditor::draw_header(ui::Ui& ui, int y) {
  const auto& th = ui.theme();
  ui.fill({0, y, kScreenW, 10}, th.raised);
  int p = pattern_;
  if (ui.spinner(2, y + 2, "pattern", p, 0, kPatterns - 1, tr("pattern ([ and ])"))) pattern_ = p;
  struct Flag {
    int ch;
    const char* label;
    const char* tip;
  };
  static constexpr Flag kFlags[] = {
      {0, "loop<", "loop start: a loop end jumps back here"},
      {1, ">loop", "loop end: jump back to the last loop start"},
      {2, "stop", "stop the music after this pattern"},
  };
  int x = 66;
  for (const auto& f : kFlags) {
    bool on = channel_byte(f.ch) & kFlag;
    int w = int(std::string_view(f.label).size()) * 4 + 5;
    if (ui.button({x, y + 1, w, 8}, f.label, on, tr(f.tip))) set_channel_byte(f.ch, uint8_t(channel_byte(f.ch) ^ kFlag));
    x += w + 2;
  }
  bool playing = studio_.console().audio.music_pattern() >= 0;
  if (ui.icon_button(x + 2, y, playing ? ui::Icon::kStop : ui::Icon::kPlay, playing, tr("play song from here (space)"))) play(true);
  if (ui.button({x + 14, y + 1, 30, 8}, "pattern", false, tr("play this pattern once (enter)"))) play(false);
  if (ui.button({x + 46, y + 1, 26, 8}, "follow", follow_, tr("scroll with the music while it plays"))) follow_ = !follow_;
}

void MusicEditor::draw_channels(ui::Ui& ui, int y) {
  const auto& th = ui.theme();
  ui.fill({0, y, kScreenW, 9}, th.panel);
  for (int ch = 0; ch < kChannels; ++ch) {
    int x = kGridX + ch * kColW;
    bool on = channel_on(ch);
    if (ui.toggle(x, y + 1, on, th.ok, tr("channel {0} on/off", std::to_string(ch))))
      set_channel_byte(ch, uint8_t(channel_byte(ch) ^ kDisabled));
    int n = channel_sfx(ch);
    if (ui.spinner(x + 10, y + 1, "sfx", n, 0, kSfxCount - 1, tr("sound effect this channel plays")))
      set_channel_byte(ch, uint8_t((channel_byte(ch) & 0xC0) | n));
  }
}

void MusicEditor::draw_grid(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  ui.fill(r, th.bg);
  auto& audio = studio_.console().audio;
  bool music_here = audio.music_pattern() == pattern_;
  if (row_ < scroll_) scroll_ = row_;
  if (row_ >= scroll_ + kVisibleRows) scroll_ = row_ - kVisibleRows + 1;
  if (ui.hovered(r) && ui.wheel()) scroll_ = std::clamp(scroll_ - ui.wheel() * 2, 0, kSfxSteps - kVisibleRows);

  hover_row_ = hover_ch_ = -1;
  if (ui.hovered(r)) {
    hover_row_ = std::clamp(scroll_ + (ui.my() - r.y) / kRowH, 0, kSfxSteps - 1);
    hover_ch_ = std::clamp((ui.mx() - kGridX) / kColW, 0, kChannels - 1);
    if (ui.clicked(0)) {
      row_ = hover_row_;
      channel_ = hover_ch_;
    }
  }

  for (int i = 0; i < kVisibleRows; ++i) {
    int row = scroll_ + i;
    int y = r.y + i * kRowH;
    if (row == row_) g.rectfill(r.x, y, r.right(), y + kRowH - 1, th.panel);
    char num[4];
    std::snprintf(num, sizeof num, "%02d", row);
    g.print(num, r.x + 1, y, row % 8 == 0 ? th.text : th.subtle);
    for (int ch = 0; ch < kChannels; ++ch) {
      int x = kGridX + ch * kColW;
      auto st = audio.status(ch);
      bool playing_row = st.sfx == channel_sfx(ch) && st.step == row && (music_here || ch == kPreviewChannel);
      if (playing_row && channel_on(ch)) g.rectfill(x - 1, y, x + kColW - 3, y + kRowH - 1, th.raised);
      if (row == row_ && ch == channel_) g.rect(x - 1, y - 1, x + kColW - 3, y + kRowH - 1, th.accent);
      audio::Step s = step(ch, row);
      bool on = channel_on(ch);
      if (s.volume == 0) {
        g.print("...", x + 1, y, th.raised);
        continue;
      }
      int dim = on ? 0 : 1;
      g.print(note_name(s.pitch), x + 1, y, dim ? th.subtle : th.text);
      g.print(std::string(1, "0123456789abcdef"[s.wave]), x + 17, y, dim ? th.raised : wave_color(s.wave));
      g.print(std::to_string(s.volume), x + 25, y, dim ? th.raised : 17);
      char fx[2] = {kEffectGlyph[s.effect & 7], 0};
      g.print(fx, x + 33, y, dim ? th.raised : (s.effect ? 25 : th.subtle));
    }
  }
  for (int ch = 1; ch < kChannels; ++ch) g.line(kGridX + ch * kColW - 3, r.y, kGridX + ch * kColW - 3, r.bottom(), th.panel);
}

void MusicEditor::draw_piano(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  ui.fill(r, th.panel);
  ui.text(tr("octave"), r.x + 2, r.y + 3, th.dim);
  if (ui.icon_button(r.x, r.y + 9, ui::Icon::kMinus, false, tr("octave down (-)"))) octave_ = std::max(0, octave_ - 1);
  ui.text(std::to_string(octave_ + 2), r.x + 12, r.y + 11, th.text);
  if (ui.icon_button(r.x + 17, r.y + 9, ui::Icon::kPlus, false, tr("octave up (=)"))) octave_ = std::min(4, octave_ + 1);

  constexpr int kWhiteW = 9, kWhiteH = 18, kBlackW = 5, kBlackH = 11;
  int kx = r.x + 34, ky = r.y;
  int playing = -1;
  auto st = step(channel_, row_);
  if (st.volume) playing = st.pitch;
  // Hit-test black keys first: they sit on top of the white ones.
  int hit = -1;
  for (int o = 0; o < 2 && ui.hovered(r); ++o)
    for (int b = 0; b < 5; ++b) {
      ui::Rect k{kx + (o * 7 + kBlackAfterWhite[b]) * kWhiteW + kWhiteW - kBlackW / 2 - 1, ky, kBlackW, kBlackH};
      if (k.contains(ui.mx(), ui.my())) hit = (octave_ + o) * 12 + kBlackSemis[b];
    }
  for (int o = 0; o < 2 && hit < 0 && ui.hovered(r); ++o)
    for (int w = 0; w < 7; ++w) {
      ui::Rect k{kx + (o * 7 + w) * kWhiteW, ky, kWhiteW, kWhiteH};
      if (k.contains(ui.mx(), ui.my())) hit = (octave_ + o) * 12 + kWhiteSemis[w];
    }

  for (int o = 0; o < 2; ++o)
    for (int w = 0; w < 7; ++w) {
      int pitch = (octave_ + o) * 12 + kWhiteSemis[w];
      int x = kx + (o * 7 + w) * kWhiteW;
      int col = pitch == held_note_ ? th.accent : (pitch == playing ? 17 : (pitch == hit ? 26 : 31));
      g.rectfill(x, ky, x + kWhiteW - 2, ky + kWhiteH - 1, col);
      if (w == 0) g.print(std::to_string(octave_ + o + 2), x + 2, ky + kWhiteH - 6, 6);
    }
  for (int o = 0; o < 2; ++o)
    for (int b = 0; b < 5; ++b) {
      int pitch = (octave_ + o) * 12 + kBlackSemis[b];
      int x = kx + (o * 7 + kBlackAfterWhite[b]) * kWhiteW + kWhiteW - kBlackW / 2 - 1;
      int col = pitch == held_note_ ? th.accent : (pitch == playing ? 16 : (pitch == hit ? 3 : 0));
      g.rectfill(x, ky, x + kBlackW - 1, ky + kBlackH - 1, col);
    }

  if (hit >= 0 && ui.clicked(0)) {
    held_note_ = std::min(hit, 63);
    studio_.console().audio.note(kPreviewChannel, held_note_, pen_wave_, pen_volume_, pen_effect_);
    enter_note(held_note_);
  }
  if (held_note_ >= 0 && !ui.mouse_down(0)) {
    studio_.console().audio.note(kPreviewChannel, -1, 0, 0);
    held_note_ = -1;
  }

  int px = kx + 14 * kWhiteW + 6;
  ui.text("step", px, r.y + 3, th.dim);
  ui.spinner(px, r.y + 10, "", advance_, 0, 8, tr("rows the cursor moves after each note"));
}

void MusicEditor::draw_pens(ui::Ui& ui, int y) {
  const auto& th = ui.theme();
  ui.fill({0, y, kScreenW, 9}, th.raised);
  ui.text("wave", 2, y + 2, th.dim);
  for (int w = 0; w < 16; ++w) {
    ui::Rect r{20 + w * 8, y + 1, 7, 7};
    bool sel = w == pen_wave_;
    ui.fill(r, sel ? th.accent : th.bg);
    ui.text(std::string(1, "0123456789abcdef"[w]), r.x + 2, r.y + 1, sel ? th.accent_text : wave_color(w));
    if (ui.hovered(r)) ui.tooltip(tr(wave_name(w)));
    if (ui.clicked_in(r)) pen_wave_ = w;
  }
  ui.text("vol", 152, y + 2, th.dim);
  for (int v = 1; v <= 7; ++v) {
    ui::Rect r{166 + (v - 1) * 6, y + 1, 5, 7};
    ui.fill(r, v <= pen_volume_ ? 17 : th.bg);
    if (ui.clicked_in(r)) pen_volume_ = v;
  }
  ui.text("fx", 210, y + 2, th.dim);
  ui::Rect fr{220, y + 1, 34, 7};
  ui.fill(fr, th.bg);
  ui.text(tr(effect_name(pen_effect_)), fr.x + 2, fr.y + 1, th.text);
  if (ui.hovered(fr)) {
    ui.tooltip(tr("click or scroll to change the effect"));
    if (ui.clicked(0)) pen_effect_ = (pen_effect_ + 1) % 8;
    if (ui.wheel()) pen_effect_ = (pen_effect_ + 8 - ui.wheel()) % 8;
  }
}

void MusicEditor::update(ui::Ui& ui, const ui::Rect& body) {
  keys(ui);
  follow_playback();
  int y = body.y;
  draw_header(ui, y);
  draw_channels(ui, y + 10);
  draw_grid(ui, {0, y + 20, kScreenW, kVisibleRows * kRowH});
  draw_piano(ui, {0, y + 20 + kVisibleRows * kRowH + 1, kScreenW, 19});
  draw_pens(ui, body.bottom() - 8);
}

std::string MusicEditor::status() const {
  char buf[128];
  int row = hover_row_ >= 0 ? hover_row_ : row_, ch = hover_ch_ >= 0 ? hover_ch_ : channel_;
  audio::Step s = step(ch, row);
  if (s.volume) {
    return tr("ch{0} sfx {1} row {2}: {3} {4} vol {5} {6}", std::to_string(ch), std::to_string(channel_sfx(ch)),
              std::to_string(row), note_name(s.pitch), tr(wave_name(s.wave)), std::to_string(s.volume),
              tr(effect_name(s.effect)));
  } else {
    return tr("ch{0} sfx {1} row {2}  keys z-m and q-i play notes, space plays", std::to_string(ch),
              std::to_string(channel_sfx(ch)), std::to_string(row));
  }
  return buf;
}

}  // namespace glim::editors
