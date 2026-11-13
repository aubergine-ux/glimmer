#pragma once

#include "audio/audio.hpp"
#include "editors/editor.hpp"

namespace glim::editors {

class MusicEditor : public Editor {
 public:
  explicit MusicEditor(Studio& studio);

  ui::Icon icon() const override { return ui::Icon::kMusic; }
  const char* name() const override { return "music"; }
  void update(ui::Ui& ui, const ui::Rect& body) override;
  void reset() override;
  std::string status() const override;

  void copy() override;
  void paste() override;

  // Exposed for tests.
  int pattern() const { return pattern_; }
  void set_pattern(int p) { pattern_ = std::clamp(p, 0, kPatterns - 1); }
  void set_cursor(int channel, int row) {
    channel_ = std::clamp(channel, 0, kChannels - 1);
    row_ = std::clamp(row, 0, kSfxSteps - 1);
  }
  int row() const { return row_; }
  void enter_note(int pitch);
  void clear_note();
  uint8_t channel_byte(int ch) const;
  void set_channel_byte(int ch, uint8_t v);
  audio::Step step(int ch, int row) const;

 private:
  int channel_sfx(int ch) const { return channel_byte(ch) & 63; }
  bool channel_on(int ch) const { return !(channel_byte(ch) & 0x40); }
  void set_step(int ch, int row, const audio::Step& s);
  void play(bool from_here);
  void follow_playback();

  void draw_header(ui::Ui& ui, int y);
  void draw_channels(ui::Ui& ui, int y);
  void draw_grid(ui::Ui& ui, const ui::Rect& r);
  void draw_piano(ui::Ui& ui, const ui::Rect& r);
  void draw_pens(ui::Ui& ui, int y);
  void keys(ui::Ui& ui);

  int pattern_ = 0;
  int channel_ = 0;
  int row_ = 0;
  int scroll_ = 0;
  int octave_ = 2;  // octave offset for the piano and keyboard, 0-4
  int pen_wave_ = 0;
  int pen_volume_ = 5;
  int pen_effect_ = 0;
  int advance_ = 1;
  bool follow_ = true;
  int held_note_ = -1;
  int hover_row_ = -1, hover_ch_ = -1;
};

}  // namespace glim::editors
