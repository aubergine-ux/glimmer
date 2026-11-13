#pragma once

#include <string>

#include "audio/audio.hpp"
#include "editors/editor.hpp"

namespace glim::editors {

std::string note_name(int pitch);  // "c-4", "f#2"...
const char* wave_name(int wave);
const char* effect_name(int effect);
int wave_color(int wave);

class SfxEditor : public Editor {
 public:
  explicit SfxEditor(Studio& studio);

  ui::Icon icon() const override { return ui::Icon::kSfx; }
  const char* name() const override { return "sound"; }
  void update(ui::Ui& ui, const ui::Rect& body) override;
  void reset() override;
  std::string status() const override;

  void copy() override;
  void paste() override;

  int sfx() const { return sfx_; }
  void set_sfx(int n) { sfx_ = std::clamp(n, 0, kSfxCount - 1); }
  audio::Sfx read() const;
  void write(const audio::Sfx& s);
  void preview_step(int step);
  void play();

 private:
  void draw_header(ui::Ui& ui, int y);
  void draw_pitch(ui::Ui& ui, const ui::Rect& r);
  void draw_volume(ui::Ui& ui, const ui::Rect& r);
  void draw_rows(ui::Ui& ui, int y);
  void draw_pens(ui::Ui& ui, int y);
  void draw_wavetable(ui::Ui& ui, const ui::Rect& r);
  int playing_step() const;
  template <class Fn>
  void for_each_step(int step, int value, Fn&& fn);

  int sfx_ = 0;
  int pen_wave_ = 0;
  int pen_effect_ = 0;
  int pen_volume_ = 5;
  bool wavetable_ = false;
  int hover_step_ = -1;
  bool drawing_ = false;
  int last_step_ = -1;
  int last_value_ = 0;
};

}  // namespace glim::editors
