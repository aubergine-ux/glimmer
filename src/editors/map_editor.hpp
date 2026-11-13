#pragma once

#include <array>

#include "editors/clipboard.hpp"
#include "editors/editor.hpp"

namespace glim::editors {

// Auto-tiling: a terrain is a 4x4 block of 16 sprites. Each painted cell picks
// the sprite whose index matches which neighbours belong to the same terrain:
// north = 1, east = 2, south = 4, west = 8, laid out row-major in the block.
int autotile_base(int tile);
int autotile_pick(int base, int mask);

class MapEditor : public Editor {
 public:
  enum class Tool { kPencil, kRect, kFill, kSelect, kPicker, kAuto };

  explicit MapEditor(Studio& studio);

  ui::Icon icon() const override { return ui::Icon::kMap; }
  const char* name() const override { return "map"; }
  void update(ui::Ui& ui, const ui::Rect& body) override;
  void reset() override;
  std::string status() const override;

  void copy() override;
  void cut() override;
  void paste() override;

  // Exposed for tests.
  void set_tool(Tool t) { tool_ = t; }
  void set_layer(int l) { layer_ = l; }
  void set_tile(int n);
  void paint(int cx, int cy);
  void fill_rect(int x0, int y0, int x1, int y1);
  void flood(int cx, int cy);
  void autotile(int cx, int cy);
  uint16_t cell(int layer, int cx, int cy) const;

 private:
  void set_cell(int layer, int cx, int cy, uint16_t v);
  // Applies the current brush to every cell on the line from the last cell, so fast drags leave no gaps.
  void stroke(int cx, int cy);
  void retile(int cx, int cy, int base);
  int tile_px() const { return 8 >> zoom_; }
  void draw_map(ui::Ui& ui, const ui::Rect& view);
  void draw_toolbar(ui::Ui& ui, const ui::Rect& bar);
  void draw_picker(ui::Ui& ui, const ui::Rect& body);
  void keys(ui::Ui& ui, const ui::Rect& view);

  CellClip stamp_{1, 1, {1}};
  int layer_ = 0;
  std::array<bool, kMapLayers> visible_{true, true, true, true};
  Tool tool_ = Tool::kPencil;
  int zoom_ = 0;  // 0: 8px tiles, 1: 4px, 2: 2px (whole map)
  int cam_x_ = 0, cam_y_ = 0;  // view origin in pixels at the current zoom
  bool screen_guides_ = true;
  bool picker_ = false;
  int picker_sheet_ = 0;
  int pick_x0_ = -1, pick_y0_ = -1;

  int hover_cx_ = -1, hover_cy_ = -1;
  bool dragging_ = false, panning_ = false;
  int drag_x0_ = 0, drag_y0_ = 0;
  int last_cx_ = 0, last_cy_ = 0;
  int pan_mx_ = 0, pan_my_ = 0, pan_cx_ = 0, pan_cy_ = 0;
  bool has_sel_ = false;
  int sel_x0_ = 0, sel_y0_ = 0, sel_x1_ = 0, sel_y1_ = 0;
};

}  // namespace glim::editors
