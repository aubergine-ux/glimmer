#pragma once

#include <vector>

#include "editors/clipboard.hpp"
#include "editors/editor.hpp"

namespace glim::editors {

class SpriteEditor : public Editor {
 public:
  enum class Tool { kPencil, kFill, kLine, kRect, kCircle, kSelect, kPicker };

  explicit SpriteEditor(Studio& studio);

  ui::Icon icon() const override { return ui::Icon::kSprite; }
  const char* name() const override { return "sprites"; }
  void update(ui::Ui& ui, const ui::Rect& body) override;
  void reset() override;
  std::string status() const override;
  bool wants_escape() const override { return floating_ || has_sel_; }

  void undo() override;
  void redo() override;
  void copy() override;
  void cut() override;
  void paste() override;

  // Exposed for tests and scripts.
  int sprite() const { return sprite_; }
  void set_sprite(int n);
  void set_tool(Tool t) { tool_ = t; }
  void set_color(int c) { color_ = c; }
  int color() const { return color_; }
  void flip(bool horizontal);
  void rotate();

 private:
  struct Area {
    int x, y, w, h;  // in sheet pixels
  };

  int block() const { return size_ * 8; }
  int origin_x() const { return (sprite_ % 16) * 8; }
  int origin_y() const { return (sprite_ / 16) * 8; }
  Area target() const;
  void commit_floating();
  void stroke_to(int x, int y);
  void flood(int x, int y);
  void shape(int x0, int y0, int x1, int y1, bool preview);

  void draw_canvas(ui::Ui& ui, const ui::Rect& r);
  void draw_tools(ui::Ui& ui, int x, int y);
  void draw_flags(ui::Ui& ui, int x, int y);
  void draw_preview(ui::Ui& ui, const ui::Rect& r);
  void draw_sheet(ui::Ui& ui, const ui::Rect& r);
  void keys(ui::Ui& ui);

  int sprite_ = 1;
  int size_ = 1;  // 1, 2 or 4 sprites per side
  int sheet_ = 0;
  int color_ = 31;
  Tool tool_ = Tool::kPencil;
  bool onion_ = false;
  bool grid_ = true;

  int scale_ = 12;
  int hover_x_ = -1, hover_y_ = -1;  // sheet pixel under the mouse, -1 outside
  bool drawing_ = false;
  int start_x_ = 0, start_y_ = 0, last_x_ = 0, last_y_ = 0;
  std::vector<uint8_t> before_;  // block pixels when a shape drag began

  bool has_sel_ = false;
  Area sel_{0, 0, 0, 0};
  bool floating_ = false;
  PixelClip float_;
  int float_x_ = 0, float_y_ = 0, grab_x_ = 0, grab_y_ = 0;
  bool moving_ = false;

  bool anim_ = false;
  int anim_frames_ = 4;
  int anim_fps_ = 8;
  uint64_t anim_tick_ = 0;
};

}  // namespace glim::editors
