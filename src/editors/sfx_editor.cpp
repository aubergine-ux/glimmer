#include "editors/sfx_editor.hpp"

#include <algorithm>
#include <cstdio>

#include "editors/clipboard.hpp"
#include "editors/studio.hpp"
#include "ui/i18n.hpp"

namespace glim::editors {

using ui::tr;

namespace {

using input::key_from_name;
constexpr int kStepW = 8;
constexpr int kPreviewChannel = 3;
constexpr int kDrawId = 0x5f0;

constexpr const char* kNotes[12] = {"c-", "c#", "d-", "d#", "e-", "f-", "f#", "g-", "g#", "a-", "a#", "b-"};
constexpr const char* kWaveNames[16] = {"square", "pulse", "triangle", "saw", "noise", "organ", "sine", "soft",
                                        "wave 0", "wave 1", "wave 2", "wave 3", "wave 4", "wave 5", "wave 6", "wave 7"};
constexpr const char* kEffectNames[8] = {"none", "slide", "vibrato", "drop", "fade in", "fade out", "arp fast", "arp slow"};
constexpr char kEffectGlyph[8] = {'-', '/', '~', '\\', '<', '>', 'a', 'b'};
constexpr int kWaveColors[16] = {30, 29, 25, 17, 6, 21, 28, 12, 4, 4, 4, 4, 4, 4, 4, 4};

std::string hex_digit(int v) { return std::string(1, "0123456789abcdef"[v & 15]); }

}  // namespace

std::string note_name(int pitch) { return std::string(kNotes[pitch % 12]) + std::to_string(2 + pitch / 12); }
const char* wave_name(int wave) { return kWaveNames[wave & 15]; }
const char* effect_name(int effect) { return kEffectNames[effect & 7]; }
int wave_color(int wave) { return kWaveColors[wave & 15]; }

SfxEditor::SfxEditor(Studio& studio) : Editor(studio) {}

void SfxEditor::reset() {
  Editor::reset();
  sfx_ = 0;
  wavetable_ = false;
}

audio::Sfx SfxEditor::read() const {
  audio::Sfx s;
  const auto& b = studio_.cart().sfx;
  size_t a = size_t(sfx_) * kSfxBytes;
  s.speed = b[a];
  s.loop_start = b[a + 1];
  s.loop_end = b[a + 2];
  s.flags = b[a + 3];
  for (int i = 0; i < kSfxSteps; ++i) s.steps[size_t(i)] = audio::Step::decode(uint16_t(b[a + 4 + i * 2] | (b[a + 5 + i * 2] << 8)));
  return s;
}

void SfxEditor::write(const audio::Sfx& s) {
  auto& b = studio_.cart().sfx;
  size_t a = size_t(sfx_) * kSfxBytes;
  b[a] = s.speed;
  b[a + 1] = s.loop_start;
  b[a + 2] = s.loop_end;
  b[a + 3] = s.flags;
  for (int i = 0; i < kSfxSteps; ++i) {
    uint16_t v = s.steps[size_t(i)].encode();
    b[a + 4 + i * 2] = uint8_t(v);
    b[a + 5 + i * 2] = uint8_t(v >> 8);
  }
}

template <class Fn>
void SfxEditor::for_each_step(int step, int value, Fn&& fn) {
  // Fill every step between the previous mouse sample and this one, so fast
  // drags draw continuous curves.
  if (last_step_ < 0 || last_step_ == step) {
    fn(step, value);
  } else {
    int dir = step > last_step_ ? 1 : -1;
    for (int i = last_step_ + dir;; i += dir) {
      fn(i, last_value_ + (value - last_value_) * (i - last_step_) / (step - last_step_));
      if (i == step) break;
    }
  }
  last_step_ = step;
  last_value_ = value;
}

void SfxEditor::preview_step(int step) {
  studio_.sync_audio();
  studio_.console().audio.sfx(sfx_, kPreviewChannel, step, 1);
}

void SfxEditor::play() {
  studio_.sync_audio();
  auto& a = studio_.console().audio;
  if (a.status(kPreviewChannel).sfx == sfx_) a.sfx(-1, kPreviewChannel);
  else a.sfx(sfx_, kPreviewChannel);
}

int SfxEditor::playing_step() const {
  auto st = studio_.console().audio.status(kPreviewChannel);
  return st.sfx == sfx_ ? st.step : -1;
}

void SfxEditor::copy() {
  const auto& b = studio_.cart().sfx;
  std::vector<uint8_t> bytes(b.begin() + sfx_ * kSfxBytes, b.begin() + (sfx_ + 1) * kSfxBytes);
  studio_.set_clipboard(encode_bytes("sfx", bytes));
  studio_.message(tr("copied sfx {0}", std::to_string(sfx_)));
}

void SfxEditor::paste() {
  std::vector<uint8_t> bytes;
  if (!decode_bytes(studio_.clipboard(), "sfx", bytes) || bytes.size() != size_t(kSfxBytes)) {
    studio_.message(tr("the clipboard has no sound effect"), true);
    return;
  }
  history_.begin(Target::kSfx);
  std::copy(bytes.begin(), bytes.end(), studio_.cart().sfx.begin() + sfx_ * kSfxBytes);
  history_.commit();
  studio_.mark_dirty();
}

void SfxEditor::draw_header(ui::Ui& ui, int y) {
  const auto& th = ui.theme();
  ui.fill({0, y, kScreenW, 10}, th.raised);
  audio::Sfx s = read();
  int n = sfx_;
  if (ui.spinner(2, y + 2, "sfx", n, 0, kSfxCount - 1, tr("sound effect number ([ and ])"))) sfx_ = n;
  int speed = s.speed, ls = s.loop_start, le = s.loop_end;
  bool changed = false;
  changed |= ui.spinner(52, y + 2, "speed", speed, 1, 255, tr("step length: speed x 7.5ms"));
  changed |= ui.spinner(110, y + 2, "loop", ls, 0, kSfxSteps, tr("loop start step"));
  changed |= ui.spinner(152, y + 2, "", le, 0, kSfxSteps, tr("loop end step (loops while end > start)"));
  if (changed) {
    history_.begin(Target::kSfx);
    s.speed = uint8_t(speed);
    s.loop_start = uint8_t(ls);
    s.loop_end = uint8_t(le);
    write(s);
    history_.commit();
    studio_.mark_dirty();
  }
  bool playing = playing_step() >= 0;
  if (ui.icon_button(188, y, playing ? ui::Icon::kStop : ui::Icon::kPlay, playing, tr("play (space)"))) play();
  if (ui.icon_button(200, y, ui::Icon::kWave, wavetable_, tr("draw custom wavetables (w)"))) wavetable_ = !wavetable_;
  ui.text(std::to_string(int(s.speed) * kSfxSteps * audio::kSamplesPerSpeedUnit * 1000 / audio::kSampleRate) + "ms",
          214, y + 3, th.dim);
}

void SfxEditor::draw_pitch(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  audio::Sfx s = read();
  ui.fill(r, th.bg);
  // Octave guides; C notes get a slightly brighter line.
  for (int p = 0; p < 64; p += 12) g.line(r.x, r.bottom() - p, r.right(), r.bottom() - p, th.panel);
  int play = playing_step();
  bool loops = s.loop_end > s.loop_start;
  for (int i = 0; i < kSfxSteps; ++i) {
    const auto& st = s.steps[size_t(i)];
    int x = r.x + i * kStepW;
    if (loops && i >= s.loop_start && i < s.loop_end) g.rectfill(x, r.y, x + kStepW - 1, r.y + 1, th.raised);
    if (i == play) g.rectfill(x, r.y, x + kStepW - 1, r.bottom(), th.panel);
    if (st.volume == 0) continue;
    int top = r.bottom() - st.pitch;
    int col = wave_color(st.wave);
    g.rectfill(x + 1, top, x + kStepW - 2, r.bottom(), th.raised);
    g.rectfill(x + 1, top, x + kStepW - 2, top + 1, col);
  }
  if (ui.hovered(r) && hover_step_ >= 0) {
    int x = r.x + hover_step_ * kStepW;
    g.rect(x, r.y, x + kStepW - 1, r.bottom(), th.dim);
  }

  // Drawing: drag across steps to set pitch. New notes take the current pens.
  if (ui.clicked_in(r)) {
    ui.capture(kDrawId);
    history_.begin(Target::kSfx);
    drawing_ = true;
    last_step_ = -1;
  }
  if (drawing_ && ui.captured(kDrawId) && ui.mouse_down(0) && ui.my() >= r.y - 20) {
    int step = std::clamp((ui.mx() - r.x) / kStepW, 0, kSfxSteps - 1);
    int pitch = std::clamp(r.bottom() - ui.my(), 0, 63);
    bool moved = step != last_step_;
    for_each_step(step, pitch, [&](int i, int v) {
      auto& st = s.steps[size_t(i)];
      if (st.volume == 0) {
        st.volume = uint8_t(pen_volume_);
        st.wave = uint8_t(pen_wave_);
        st.effect = uint8_t(pen_effect_);
      }
      st.pitch = uint8_t(v);
    });
    write(s);
    if (moved) preview_step(step);
  }
  // Right-drag erases.
  if (ui.hovered(r) && ui.mouse_down(1)) {
    int step = std::clamp((ui.mx() - r.x) / kStepW, 0, kSfxSteps - 1);
    if (s.steps[size_t(step)].volume) {
      history_.begin(Target::kSfx);
      s.steps[size_t(step)].volume = 0;
      write(s);
      if (history_.commit()) studio_.mark_dirty();
    }
  }
}

void SfxEditor::draw_volume(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  audio::Sfx s = read();
  ui.fill(r, th.bg);
  for (int i = 0; i < kSfxSteps; ++i) {
    const auto& st = s.steps[size_t(i)];
    int x = r.x + i * kStepW;
    for (int v = 0; v < 7; ++v) {
      int y = r.bottom() - v * 3 - 2;
      g.rectfill(x + 2, y, x + kStepW - 3, y + 1, v < st.volume ? 17 : th.panel);
    }
  }
  if (ui.clicked_in(r)) {
    ui.capture(kDrawId + 1);
    history_.begin(Target::kSfx);
    drawing_ = true;
    last_step_ = -1;
  }
  if (ui.captured(kDrawId + 1) && ui.mouse_down(0)) {
    int step = std::clamp((ui.mx() - r.x) / kStepW, 0, kSfxSteps - 1);
    int vol = std::clamp((r.bottom() - ui.my() + 2) / 3, 0, 7);
    for_each_step(step, vol, [&](int i, int v) { s.steps[size_t(i)].volume = uint8_t(v); });
    write(s);
  }
}

void SfxEditor::draw_rows(ui::Ui& ui, int y) {
  const auto& th = ui.theme();
  audio::Sfx s = read();
  ui.fill({0, y, kScreenW, 15}, th.panel);
  for (int i = 0; i < kSfxSteps; ++i) {
    auto& st = s.steps[size_t(i)];
    int x = i * kStepW;
    ui::Rect wr{x, y, kStepW, 7}, er{x, y + 8, kStepW, 7};
    bool on = st.volume > 0;
    ui.text(hex_digit(st.wave), x + 2, y + 1, on ? wave_color(st.wave) : th.raised);
    char glyph[2] = {kEffectGlyph[st.effect & 7], 0};
    ui.text(glyph, x + 2, y + 9, on && st.effect ? th.text : th.raised);
    // Click stamps the current pen; the wheel cycles through values.
    auto edit = [&](const ui::Rect& rr, uint8_t& field, int pen, int count) {
      if (!ui.hovered(rr)) return;
      int v = field;
      if (ui.clicked(0)) v = pen;
      if (ui.wheel()) v = (v + count - ui.wheel()) % count;
      if (v == field) return;
      history_.begin(Target::kSfx);
      field = uint8_t(v);
      write(s);
      history_.commit();
      studio_.mark_dirty();
      if (on) preview_step(i);
    };
    edit(wr, st.wave, pen_wave_, 16);
    edit(er, st.effect, pen_effect_, 8);
  }
}

void SfxEditor::draw_pens(ui::Ui& ui, int y) {
  const auto& th = ui.theme();
  ui.fill({0, y, kScreenW, 10}, th.raised);
  ui.text("wave", 2, y + 3, th.dim);
  for (int w = 0; w < 16; ++w) {
    ui::Rect r{20 + w * 9, y + 1, 8, 8};
    bool sel = w == pen_wave_;
    ui.fill(r, sel ? th.accent : (ui.hovered(r) ? th.hot : th.bg));
    ui.text(hex_digit(w), r.x + 2, r.y + 2, sel ? th.accent_text : wave_color(w));
    if (ui.hovered(r)) ui.tooltip(tr(wave_name(w)));
    if (ui.clicked_in(r)) pen_wave_ = w;
  }
  ui.text("fx", 167, y + 3, th.dim);
  for (int e = 0; e < 8; ++e) {
    ui::Rect r{177 + e * 9, y + 1, 8, 8};
    bool sel = e == pen_effect_;
    ui.fill(r, sel ? th.accent : (ui.hovered(r) ? th.hot : th.bg));
    char glyph[2] = {kEffectGlyph[e], 0};
    ui.text(glyph, r.x + 2, r.y + 2, sel ? th.accent_text : th.text);
    if (ui.hovered(r)) ui.tooltip(tr(effect_name(e)));
    if (ui.clicked_in(r)) pen_effect_ = e;
  }
}

void SfxEditor::draw_wavetable(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  int table = pen_wave_ >= 8 ? pen_wave_ - 8 : 0;
  auto& w = studio_.cart().wave;
  ui.fill(r, th.bg);
  int mid = r.y + r.h / 2;
  g.line(r.x, mid, r.right(), mid, th.panel);
  for (int i = 0; i < kWaveSamples; ++i) {
    int v = int8_t(w[size_t(table * kWaveSamples + i)]);
    int h = v * (r.h / 2 - 1) / 128;
    int x = r.x + i * kStepW;
    g.rectfill(x + 1, std::min(mid, mid - h), x + kStepW - 2, std::max(mid, mid - h), 4);
    g.rectfill(x + 1, mid - h, x + kStepW - 2, mid - h, 31);
  }
  ui.text(tr("wave {0} (pick waves 8-f below)", std::to_string(table)), r.x + 2, r.y + 2, th.dim);
  if (ui.clicked_in(r)) {
    ui.capture(kDrawId + 2);
    history_.begin(Target::kWave);
    last_step_ = -1;
  }
  if (ui.captured(kDrawId + 2) && ui.mouse_down(0)) {
    int i = std::clamp((ui.mx() - r.x) / kStepW, 0, kWaveSamples - 1);
    int v = std::clamp((mid - ui.my()) * 128 / (r.h / 2 - 1), -128, 127);
    for_each_step(i, v, [&](int k, int val) { w[size_t(table * kWaveSamples + k)] = uint8_t(int8_t(val)); });
  }
}

void SfxEditor::update(ui::Ui& ui, const ui::Rect& body) {
  auto& in = ui.in();
  if (!in.ctrl() && !in.alt()) {
    if (in.keyp(key_from_name("space"))) play();
    if (in.keyr(key_from_name("["))) set_sfx(sfx_ - 1);
    if (in.keyr(key_from_name("]"))) set_sfx(sfx_ + 1);
    if (in.keyp(key_from_name("w"))) wavetable_ = !wavetable_;
  }
  if (ui.released(0) && history_.open()) {
    if (history_.commit()) studio_.mark_dirty();
    drawing_ = false;
  }

  int y = body.y;
  draw_header(ui, y);
  ui::Rect pitch{0, y + 11, kScreenW, 66};
  hover_step_ = ui.hovered(pitch) ? std::clamp(ui.mx() / kStepW, 0, kSfxSteps - 1) : -1;
  if (wavetable_) draw_wavetable(ui, pitch);
  else draw_pitch(ui, pitch);
  ui::Rect vol{0, pitch.bottom() + 1, kScreenW, 22};
  if (ui.hovered(vol)) hover_step_ = std::clamp(ui.mx() / kStepW, 0, kSfxSteps - 1);
  draw_volume(ui, vol);
  draw_rows(ui, vol.bottom() + 1);
  if (ui.my() > vol.bottom() && ui.my() < vol.bottom() + 16) hover_step_ = std::clamp(ui.mx() / kStepW, 0, kSfxSteps - 1);
  draw_pens(ui, body.bottom() - 9);
}

std::string SfxEditor::status() const {
  if (hover_step_ < 0) return tr("draw pitch and volume  right-drag erases  space plays  [ ] other sfx");
  audio::Sfx s = read();
  const auto& st = s.steps[size_t(hover_step_)];
  char buf[96];
  if (st.volume == 0) {
    return tr("step {0}: rest", std::to_string(hover_step_));
  } else {
    return tr("step {0}: {1}  {2}  vol {3}  {4}", std::to_string(hover_step_), note_name(st.pitch), tr(wave_name(st.wave)),
              std::to_string(st.volume), tr(effect_name(st.effect)));
  }
  return buf;
}

}  // namespace glim::editors
