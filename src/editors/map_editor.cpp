#include "editors/map_editor.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "editors/sheet.hpp"
#include "editors/studio.hpp"
#include "ui/i18n.hpp"

namespace glim::editors {

using ui::tr;

namespace {

using input::key_from_name;
constexpr int kToolbarH = 9;
constexpr uint16_t kIdMask = 0x01FF;
constexpr int kMapCaptureId = 0x3a9;

}  // namespace

int autotile_base(int tile) {
  int sheet = tile / 256, col = tile % 16, row = (tile / 16) % 16;
  return sheet * 256 + (row / 4) * 4 * 16 + (col / 4) * 4;
}

int autotile_pick(int base, int mask) { return base + (mask % 4) + (mask / 4) * 16; }

MapEditor::MapEditor(Studio& studio) : Editor(studio) {}

void MapEditor::reset() {
  Editor::reset();
  cam_x_ = cam_y_ = 0;
  has_sel_ = false;
  picker_ = false;
  stamp_ = {1, 1, {1}};
}

void MapEditor::set_tile(int n) { stamp_ = {1, 1, {uint16_t(std::clamp(n, 0, kSprites - 1))}}; }

uint16_t MapEditor::cell(int layer, int cx, int cy) const {
  if (cx < 0 || cy < 0 || cx >= kMapW || cy >= kMapH) return 0;
  const auto& m = studio_.cart().map;
  size_t i = size_t(layer) * addr::kMapLayerSize + (size_t(cy) * kMapW + size_t(cx)) * 2;
  return uint16_t(m[i] | (m[i + 1] << 8));
}

void MapEditor::set_cell(int layer, int cx, int cy, uint16_t v) {
  if (cx < 0 || cy < 0 || cx >= kMapW || cy >= kMapH) return;
  auto& m = studio_.cart().map;
  size_t i = size_t(layer) * addr::kMapLayerSize + (size_t(cy) * kMapW + size_t(cx)) * 2;
  m[i] = uint8_t(v);
  m[i + 1] = uint8_t(v >> 8);
}

void MapEditor::paint(int cx, int cy) {
  for (int y = 0; y < stamp_.h; ++y)
    for (int x = 0; x < stamp_.w; ++x) set_cell(layer_, cx + x, cy + y, stamp_.cells[size_t(y) * stamp_.w + x]);
}

void MapEditor::fill_rect(int x0, int y0, int x1, int y1) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  // The stamp tiles across the rectangle, so a 2x2 stamp fills in a pattern.
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      set_cell(layer_, x, y, stamp_.cells[size_t((y - y0) % stamp_.h) * stamp_.w + size_t((x - x0) % stamp_.w)]);
}

void MapEditor::flood(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= kMapW || cy >= kMapH) return;
  uint16_t from = cell(layer_, cx, cy), to = stamp_.cells[0];
  if (from == to) return;
  std::vector<std::pair<int, int>> stack{{cx, cy}};
  while (!stack.empty()) {
    auto [x, y] = stack.back();
    stack.pop_back();
    if (x < 0 || y < 0 || x >= kMapW || y >= kMapH || cell(layer_, x, y) != from) continue;
    set_cell(layer_, x, y, to);
    stack.push_back({x + 1, y});
    stack.push_back({x - 1, y});
    stack.push_back({x, y + 1});
    stack.push_back({x, y - 1});
  }
}

void MapEditor::retile(int cx, int cy, int base) {
  auto member = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= kMapW || y >= kMapH) return false;
    int id = cell(layer_, x, y) & kIdMask;
    return id != 0 && autotile_base(id) == base;
  };
  if (!member(cx, cy)) return;
  int mask = (member(cx, cy - 1) ? 1 : 0) | (member(cx + 1, cy) ? 2 : 0) | (member(cx, cy + 1) ? 4 : 0) |
             (member(cx - 1, cy) ? 8 : 0);
  set_cell(layer_, cx, cy, uint16_t(autotile_pick(base, mask)));
}

void MapEditor::autotile(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= kMapW || cy >= kMapH) return;
  int base = autotile_base(stamp_.cells[0] & kIdMask);
  set_cell(layer_, cx, cy, uint16_t(base));  // any member works; retile picks the right one
  retile(cx, cy, base);
  retile(cx, cy - 1, base);
  retile(cx + 1, cy, base);
  retile(cx, cy + 1, base);
  retile(cx - 1, cy, base);
}

void MapEditor::stroke(int cx, int cy) {
  int x = last_cx_, y = last_cy_;
  int dx = std::abs(cx - x), sx = x < cx ? 1 : -1, dy = -std::abs(cy - y), sy = y < cy ? 1 : -1, err = dx + dy;
  for (;;) {
    if (tool_ == Tool::kAuto) autotile(x, y);
    else paint(x, y);
    if (x == cx && y == cy) break;
    int e2 = 2 * err;
    bool step_x = e2 >= dy, step_y = e2 <= dx;
    if (step_x) {
      err += dy;
      x += sx;
    }
    // Auto-tiled paths only join through edges, so diagonal steps get a corner cell.
    if (step_x && step_y && tool_ == Tool::kAuto) autotile(x, y);
    if (step_y) {
      err += dx;
      y += sy;
    }
  }
  last_cx_ = cx;
  last_cy_ = cy;
}

void MapEditor::copy() {
  if (!has_sel_) {
    studio_.message(tr("select cells first (s)"), true);
    return;
  }
  CellClip clip;
  clip.w = std::abs(sel_x1_ - sel_x0_) + 1;
  clip.h = std::abs(sel_y1_ - sel_y0_) + 1;
  int x0 = std::min(sel_x0_, sel_x1_), y0 = std::min(sel_y0_, sel_y1_);
  for (int y = 0; y < clip.h; ++y)
    for (int x = 0; x < clip.w; ++x) clip.cells.push_back(cell(layer_, x0 + x, y0 + y));
  studio_.set_clipboard(encode_cells(clip));
  studio_.message(tr("copied {0}x{1} cells", std::to_string(clip.w), std::to_string(clip.h)));
}

void MapEditor::cut() {
  copy();
  if (!has_sel_) return;
  history_.begin(Target::kMap);
  for (int y = std::min(sel_y0_, sel_y1_); y <= std::max(sel_y0_, sel_y1_); ++y)
    for (int x = std::min(sel_x0_, sel_x1_); x <= std::max(sel_x0_, sel_x1_); ++x) set_cell(layer_, x, y, 0);
  history_.commit();
  studio_.mark_dirty();
}

void MapEditor::paste() {
  CellClip clip;
  if (!decode_cells(studio_.clipboard(), clip)) {
    studio_.message(tr("the clipboard has no map cells"), true);
    return;
  }
  stamp_ = clip;
  tool_ = Tool::kPencil;
  has_sel_ = false;
  studio_.message(tr("pasted as a stamp - click to place it"));
}

void MapEditor::keys(ui::Ui& ui, const ui::Rect& view) {
  auto& in = ui.in();
  if (in.ctrl() || in.alt()) return;
  auto kp = [&](const char* k) { return in.keyp(key_from_name(k)); };
  auto kr = [&](const char* k) { return in.keyr(key_from_name(k)); };
  if (kp("tab")) picker_ = !picker_;
  if (picker_) {
    if (kp("escape")) picker_ = false;
    return;
  }
  if (kp("p")) tool_ = Tool::kPencil;
  if (kp("r")) tool_ = Tool::kRect;
  if (kp("f")) tool_ = Tool::kFill;
  if (kp("s")) tool_ = Tool::kSelect;
  if (kp("i")) tool_ = Tool::kPicker;
  if (kp("a")) tool_ = Tool::kAuto;
  if (kp("g")) screen_guides_ = !screen_guides_;
  if (kp("escape")) has_sel_ = false;
  for (int l = 0; l < kMapLayers; ++l)
    if (kp(std::to_string(l + 1).c_str())) layer_ = l;
  int step = 8 * (in.shift() ? 4 : 1);
  if (kr("left")) cam_x_ -= step;
  if (kr("right")) cam_x_ += step;
  if (kr("up")) cam_y_ -= step;
  if (kr("down")) cam_y_ += step;
  if ((kp("delete") || kp("backspace")) && has_sel_) {
    history_.begin(Target::kMap);
    for (int y = std::min(sel_y0_, sel_y1_); y <= std::max(sel_y0_, sel_y1_); ++y)
      for (int x = std::min(sel_x0_, sel_x1_); x <= std::max(sel_x0_, sel_x1_); ++x) set_cell(layer_, x, y, 0);
    history_.commit();
    studio_.mark_dirty();
  }
  (void)view;
}

void MapEditor::draw_map(ui::Ui& ui, const ui::Rect& view) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  auto& c = studio_.cart();
  int tp = tile_px();
  int map_w = kMapW * tp, map_h = kMapH * tp;
  cam_x_ = std::clamp(cam_x_, std::min(0, map_w - view.w), std::max(0, map_w - view.w));
  cam_y_ = std::clamp(cam_y_, std::min(0, map_h - view.h), std::max(0, map_h - view.h));
  if (map_w <= view.w) cam_x_ = -(view.w - map_w) / 2;
  if (map_h <= view.h) cam_y_ = -(view.h - map_h) / 2;

  ui.fill(view, th.bg);
  g.clip(view.x, view.y, view.w, view.h);
  // Outside the map: a quiet dither so the edges are obvious.
  g.fillp(0xa5a5, th.bg);
  g.rectfill(view.x, view.y, view.right(), view.bottom(), th.panel);
  g.fillp(0);
  g.rectfill(view.x - cam_x_, view.y - cam_y_, view.x - cam_x_ + map_w - 1, view.y - cam_y_ + map_h - 1, th.bg);

  int c0 = std::max(0, cam_x_ / tp), r0 = std::max(0, cam_y_ / tp);
  int c1 = std::min(kMapW - 1, (cam_x_ + view.w) / tp), r1 = std::min(kMapH - 1, (cam_y_ + view.h) / tp);
  int step = 8 / tp;  // sample every step-th pixel when zoomed out
  for (int l = 0; l < kMapLayers; ++l) {
    if (!visible_[size_t(l)]) continue;
    for (int cy = r0; cy <= r1; ++cy)
      for (int cx = c0; cx <= c1; ++cx) {
        uint16_t v = cell(l, cx, cy);
        int id = v & kIdMask;
        if (!id) continue;
        bool fx = v & 0x200, fy = v & 0x400;
        int sx = (id % 16) * 8, sy = (id / 16) * 8;
        int dx = view.x + cx * tp - cam_x_, dy = view.y + cy * tp - cam_y_;
        for (int py = 0; py < tp; ++py)
          for (int px = 0; px < tp; ++px) {
            int ux = px * step + step / 2, uy = py * step + step / 2;
            if (step == 1) ux = px, uy = py;
            int col = sheet_px(c, sx + (fx ? 7 - ux : ux), sy + (fy ? 7 - uy : uy));
            if (col) g.pset(dx + px, dy + py, col);
          }
      }
  }

  // Screen-sized guides: one game screen is 32x18 cells.
  if (screen_guides_) {
    g.fillp(0xcccc);
    for (int x = 32; x < kMapW; x += 32) g.line(view.x + x * tp - cam_x_, view.y, view.x + x * tp - cam_x_, view.bottom(), th.raised);
    for (int y = 18; y < kMapH; y += 18) g.line(view.x, view.y + y * tp - cam_y_, view.right(), view.y + y * tp - cam_y_, th.raised);
    g.fillp(0);
  }

  // Mouse.
  hover_cx_ = hover_cy_ = -1;
  bool over = ui.hovered(view) && !picker_;
  if (over || ui.captured(kMapCaptureId)) {
    hover_cx_ = (ui.mx() - view.x + cam_x_) / tp;
    hover_cy_ = (ui.my() - view.y + cam_y_) / tp;
    if (ui.mx() - view.x + cam_x_ < 0) hover_cx_ = -1;
    if (ui.my() - view.y + cam_y_ < 0) hover_cy_ = -1;
  }
  bool in_map = hover_cx_ >= 0 && hover_cy_ >= 0 && hover_cx_ < kMapW && hover_cy_ < kMapH;

  if (over && ui.wheel()) {
    // Zoom around the mouse so the tile under it stays put.
    int old_tp = tp, nz = std::clamp(zoom_ - ui.wheel(), 0, 2);
    if (nz != zoom_) {
      int wx = ui.mx() - view.x + cam_x_, wy = ui.my() - view.y + cam_y_;
      zoom_ = nz;
      int ntp = tile_px();
      cam_x_ = wx * ntp / old_tp - (ui.mx() - view.x);
      cam_y_ = wy * ntp / old_tp - (ui.my() - view.y);
    }
  }
  bool space = ui.in().key(key_from_name("space"));
  if (over && (ui.clicked(2) || (space && ui.clicked(0)))) {
    panning_ = true;
    pan_mx_ = ui.mx();
    pan_my_ = ui.my();
    pan_cx_ = cam_x_;
    pan_cy_ = cam_y_;
    ui.capture(kMapCaptureId);
  }
  if (panning_) {
    cam_x_ = pan_cx_ - (ui.mx() - pan_mx_);
    cam_y_ = pan_cy_ - (ui.my() - pan_my_);
    if (!ui.mouse_down(2) && !ui.mouse_down(0)) panning_ = false;
  }
  if (over && ui.clicked(1) && in_map) set_tile(cell(layer_, hover_cx_, hover_cy_) & kIdMask);

  if (over && ui.clicked(0) && !panning_ && !space && in_map) {
    ui.capture(kMapCaptureId);
    drag_x0_ = last_cx_ = hover_cx_;
    drag_y0_ = last_cy_ = hover_cy_;
    dragging_ = true;
    if (tool_ == Tool::kPicker) {
      set_tile(cell(layer_, hover_cx_, hover_cy_) & kIdMask);
      tool_ = Tool::kPencil;
      dragging_ = false;
    } else if (tool_ == Tool::kSelect) {
      has_sel_ = true;
      sel_x0_ = sel_x1_ = hover_cx_;
      sel_y0_ = sel_y1_ = hover_cy_;
    } else {
      history_.begin(Target::kMap);
      if (tool_ == Tool::kPencil) paint(hover_cx_, hover_cy_);
      if (tool_ == Tool::kFill) flood(hover_cx_, hover_cy_);
      if (tool_ == Tool::kAuto) autotile(hover_cx_, hover_cy_);
    }
  }
  if (dragging_ && ui.mouse_down(0) && hover_cx_ >= 0) {
    int cx = std::clamp(hover_cx_, 0, kMapW - 1), cy = std::clamp(hover_cy_, 0, kMapH - 1);
    if (tool_ == Tool::kPencil || tool_ == Tool::kAuto) stroke(cx, cy);
    if (tool_ == Tool::kSelect) {
      sel_x1_ = cx;
      sel_y1_ = cy;
    }
  }
  if (dragging_ && ui.released(0)) {
    if (tool_ == Tool::kRect && hover_cx_ >= 0)
      fill_rect(drag_x0_, drag_y0_, std::clamp(hover_cx_, 0, kMapW - 1), std::clamp(hover_cy_, 0, kMapH - 1));
    if (tool_ != Tool::kSelect && history_.commit()) studio_.mark_dirty();
    dragging_ = false;
  }

  // Overlays: selection, rect preview, stamp ghost.
  auto box = [&](int x0, int y0, int x1, int y1, int color, bool animated) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (animated) g.fillp((ui.frame() / 4) % 2 ? 0x3333 : 0xcccc);
    g.rect(view.x + x0 * tp - cam_x_ - 1, view.y + y0 * tp - cam_y_ - 1, view.x + (x1 + 1) * tp - cam_x_,
           view.y + (y1 + 1) * tp - cam_y_, color);
    g.fillp(0);
  };
  if (has_sel_) box(sel_x0_, sel_y0_, sel_x1_, sel_y1_, 31, true);
  if (dragging_ && tool_ == Tool::kRect && hover_cx_ >= 0) box(drag_x0_, drag_y0_, hover_cx_, hover_cy_, th.accent, false);
  if (over && in_map && !dragging_ && tool_ != Tool::kSelect) {
    int w = (tool_ == Tool::kPencil) ? stamp_.w : 1, h = (tool_ == Tool::kPencil) ? stamp_.h : 1;
    box(hover_cx_, hover_cy_, hover_cx_ + w - 1, hover_cy_ + h - 1, (ui.frame() / 8) % 2 ? 31 : th.accent, false);
  }
  g.clip();
}

void MapEditor::draw_toolbar(ui::Ui& ui, const ui::Rect& bar) {
  const auto& th = ui.theme();
  ui.fill(bar, th.raised);
  struct Item {
    ui::Icon icon;
    Tool tool;
    const char* tip;
  };
  static constexpr Item kTools[] = {
      {ui::Icon::kPencil, Tool::kPencil, "paint (p) - right-click picks a tile"},
      {ui::Icon::kRect, Tool::kRect, "rectangle fill (r)"},
      {ui::Icon::kFill, Tool::kFill, "flood fill (f)"},
      {ui::Icon::kSelect, Tool::kSelect, "select (s) - ctrl+c copies, ctrl+v pastes as a stamp"},
      {ui::Icon::kPicker, Tool::kPicker, "pick a tile from the map (i)"},
      {ui::Icon::kAutoTile, Tool::kAuto, "auto-tile (a): paints with the 4x4 block holding the current tile"},
  };
  int x = bar.x + 1;
  for (const auto& t : kTools) {
    if (ui.icon_button(x, bar.y, t.icon, tool_ == t.tool, tr(t.tip), bar.h)) tool_ = t.tool;
    x += 10;
  }
  x += 3;
  // Current tile; click to open the picker.
  ui::Rect cur{x, bar.y, 9, bar.h};
  ui.fill(cur, th.bg);
  draw_sprite(ui, studio_.cart(), stamp_.cells[0] & kIdMask, x + 1, bar.y + 1, 1, -1);
  if (ui.hovered(cur)) ui.tooltip(tr("tiles (tab)"));
  if (ui.clicked_in(cur)) picker_ = !picker_;
  x += 12;

  for (int l = 0; l < kMapLayers; ++l) {
    ui::Rect lr{x, bar.y + 1, 9, bar.h - 2};
    bool active = l == layer_;
    if (ui.button(lr, std::to_string(l + 1), active, tr("layer {0} ({0}), right-click hides it", std::to_string(l + 1))))
      layer_ = l;
    if (ui.hovered(lr) && ui.clicked(1)) visible_[size_t(l)] = !visible_[size_t(l)];
    if (!visible_[size_t(l)]) ui.g().line(lr.x, lr.bottom(), lr.right(), lr.y, th.error);
    x += 10;
  }
  x += 3;
  if (ui.icon_button(x, bar.y, ui::Icon::kGrid, screen_guides_, tr("screen guides (g)"), bar.h)) screen_guides_ = !screen_guides_;
  x += 12;
  static constexpr const char* kZoom[] = {"1x", "1/2", "1/4"};
  for (int z = 0; z < 3; ++z) {
    if (ui.button({x, bar.y + 1, 14, bar.h - 2}, kZoom[z], zoom_ == z, z == 2 ? "whole map" : tr("zoom (mouse wheel)"))) zoom_ = z;
    x += 15;
  }
}

void MapEditor::draw_picker(ui::Ui& ui, const ui::Rect& body) {
  if (!picker_) return;
  const auto& th = ui.theme();
  auto& g = ui.g();
  ui::Rect box{body.x + 60, body.y, kSheetW + 4, body.h - kToolbarH};
  ui.begin_popup(box);
  ui.frame(box, th.raised);
  ui::Rect sheet{box.x + 2, box.y + 2, kSheetW, std::min(kSheetH, box.h - 12)};
  int rows = sheet.h / 8;
  draw_sheet_area(ui, studio_.cart(), 0, picker_sheet_ * kSheetH, kSheetW, rows * 8, sheet.x, sheet.y, 1, th.bg);
  for (int i = 0; i < 2; ++i)
    if (ui.button({box.x + 2 + i * 14, box.bottom() - 9, 13, 8}, "s" + std::to_string(i), picker_sheet_ == i)) picker_sheet_ = i;
  ui.text(tr("drag for a multi-tile stamp"), box.x + 32, box.bottom() - 7, th.dim);

  if (ui.hovered(sheet)) {
    int col = (ui.mx() - sheet.x) / 8, row = (ui.my() - sheet.y) / 8;
    g.rect(sheet.x + col * 8, sheet.y + row * 8, sheet.x + col * 8 + 7, sheet.y + row * 8 + 7, 31);
    if (ui.clicked(0)) {
      pick_x0_ = col;
      pick_y0_ = row;
      ui.capture(0x71c);
    }
  }
  if (pick_x0_ >= 0) {
    int col = std::clamp((ui.mx() - sheet.x) / 8, 0, 15), row = std::clamp((ui.my() - sheet.y) / 8, 0, rows - 1);
    int x0 = std::min(pick_x0_, col), x1 = std::max(pick_x0_, col), y0 = std::min(pick_y0_, row), y1 = std::max(pick_y0_, row);
    g.rect(sheet.x + x0 * 8 - 1, sheet.y + y0 * 8 - 1, sheet.x + x1 * 8 + 8, sheet.y + y1 * 8 + 8, th.accent);
    if (ui.released(0)) {
      stamp_ = {x1 - x0 + 1, y1 - y0 + 1, {}};
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) stamp_.cells.push_back(uint16_t(picker_sheet_ * 256 + y * 16 + x));
      pick_x0_ = -1;
      picker_ = false;
      if (tool_ == Tool::kSelect || tool_ == Tool::kPicker) tool_ = Tool::kPencil;
    }
  }
  ui.end_popup();
  if (ui.clicked(0) && !ui.hovered(box) && pick_x0_ < 0) picker_ = false;
}

void MapEditor::update(ui::Ui& ui, const ui::Rect& body) {
  ui::Rect view{body.x, body.y, body.w, body.h - kToolbarH};
  keys(ui, view);
  draw_map(ui, view);
  draw_toolbar(ui, {body.x, view.bottom() + 1, body.w, kToolbarH});
  draw_picker(ui, body);
}

std::string MapEditor::status() const {
  static constexpr const char* kToolNames[] = {"paint", "rect", "fill", "select", "pick", "auto-tile"};
  std::string tool = tr(kToolNames[int(tool_)]), layer = std::to_string(layer_ + 1);
  if (hover_cx_ >= 0 && hover_cy_ >= 0 && hover_cx_ < kMapW && hover_cy_ < kMapH)
    return tr("x{0} y{1}  tile #{2}  layer {3}  {4}", std::to_string(hover_cx_), std::to_string(hover_cy_),
              std::to_string(cell(layer_, hover_cx_, hover_cy_) & kIdMask), layer, tool);
  return tr("layer {0}  {1}  tab: tiles  wheel: zoom  space+drag: pan", layer, tool);
}

}  // namespace glim::editors
