#include "editors/sprite_editor.hpp"

#include <algorithm>
#include <cmath>

#include "editors/sheet.hpp"
#include "editors/studio.hpp"

namespace glim::editors {

namespace {

using input::key_from_name;
constexpr int kCanvas = 96;
constexpr int kCanvasX = 2;
constexpr int kColumnX = 100;
constexpr int kSheetX = 128;
constexpr int kPreview = 26;
constexpr int kFlagColors[8] = {11, 16, 17, 21, 25, 4, 30, 6};
constexpr int kSelectionId = 0x5e1;

}  // namespace

void draw_sheet_area(ui::Ui& ui, const cart::Cart& c, int sx, int sy, int w, int h, int dx, int dy, int scale,
                     int zero) {
  auto& g = ui.g();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int col = sheet_px(c, sx + x, sy + y);
      if (col == 0) {
        if (zero < 0) continue;
        col = zero;
      }
      if (scale == 1) g.pset(dx + x, dy + y, col);
      else g.rectfill(dx + x * scale, dy + y * scale, dx + (x + 1) * scale - 1, dy + (y + 1) * scale - 1, col);
    }
  }
}

SpriteEditor::SpriteEditor(Studio& studio) : Editor(studio) {}

void SpriteEditor::reset() {
  Editor::reset();
  has_sel_ = floating_ = drawing_ = moving_ = false;
  set_sprite(1);
}

void SpriteEditor::set_sprite(int n) {
  commit_floating();
  n = std::clamp(n, 0, kSprites - 1);
  int sheet = n / 256, col = n % 16, row = (n / 16) % 16;
  col = std::min(col, 16 - size_);
  row = std::min(row, 16 - size_);
  sprite_ = sheet * 256 + row * 16 + col;
  sheet_ = sheet;
  has_sel_ = false;
}

SpriteEditor::Area SpriteEditor::target() const {
  if (has_sel_) return sel_;
  return {origin_x(), origin_y(), block(), block()};
}

void SpriteEditor::undo() {
  commit_floating();
  if (history_.undo() < 0) studio_.message("nothing to undo");
}

void SpriteEditor::redo() {
  if (history_.redo() < 0) studio_.message("nothing to redo");
}

void SpriteEditor::commit_floating() {
  if (!floating_) return;
  auto& c = studio_.cart();
  history_.begin(Target::kGfx);
  for (int y = 0; y < float_.h; ++y)
    for (int x = 0; x < float_.w; ++x) {
      int col = float_.px[size_t(y) * float_.w + x];
      int tx = float_x_ + x, ty = float_y_ + y;
      // Pasted pixels stay inside the block being edited.
      if (col && tx >= origin_x() && ty >= origin_y() && tx < origin_x() + block() && ty < origin_y() + block())
        set_sheet_px(c, tx, ty, col);
    }
  history_.commit();
  studio_.mark_dirty();
  floating_ = false;
}

void SpriteEditor::copy() {
  PixelClip clip;
  if (floating_) {
    clip = float_;
  } else {
    Area a = target();
    clip.w = a.w;
    clip.h = a.h;
    for (int y = 0; y < a.h; ++y)
      for (int x = 0; x < a.w; ++x) clip.px.push_back(uint8_t(sheet_px(studio_.cart(), a.x + x, a.y + y)));
  }
  studio_.set_clipboard(encode_pixels(clip));
  studio_.message("copied " + std::to_string(clip.w) + "x" + std::to_string(clip.h) + " pixels");
}

void SpriteEditor::cut() {
  copy();
  if (floating_) {
    floating_ = false;
    return;
  }
  Area a = target();
  history_.begin(Target::kGfx);
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) set_sheet_px(studio_.cart(), a.x + x, a.y + y, 0);
  history_.commit();
  studio_.mark_dirty();
}

void SpriteEditor::paste() {
  PixelClip clip;
  if (!decode_pixels(studio_.clipboard(), clip)) {
    studio_.message("the clipboard has no pixels", true);
    return;
  }
  commit_floating();
  float_ = clip;
  floating_ = true;
  Area a = target();
  float_x_ = a.x;
  float_y_ = a.y;
  tool_ = Tool::kSelect;
  has_sel_ = false;
  studio_.message("drag to place, click outside to drop");
}

void SpriteEditor::flip(bool horizontal) {
  commit_floating();
  Area a = target();
  auto& c = studio_.cart();
  history_.begin(Target::kGfx);
  std::vector<int> px(size_t(a.w) * a.h);
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) px[size_t(y) * a.w + x] = sheet_px(c, a.x + x, a.y + y);
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) {
      int sx = horizontal ? a.w - 1 - x : x, sy = horizontal ? y : a.h - 1 - y;
      set_sheet_px(c, a.x + x, a.y + y, px[size_t(sy) * a.w + sx]);
    }
  history_.commit();
  studio_.mark_dirty();
}

void SpriteEditor::rotate() {
  commit_floating();
  Area a = target();
  if (a.w != a.h) {
    studio_.message("rotate needs a square selection", true);
    return;
  }
  auto& c = studio_.cart();
  history_.begin(Target::kGfx);
  std::vector<int> px(size_t(a.w) * a.h);
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) px[size_t(y) * a.w + x] = sheet_px(c, a.x + x, a.y + y);
  // Clockwise: the left column becomes the top row.
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) set_sheet_px(c, a.x + x, a.y + y, px[size_t(a.w - 1 - x) * a.w + y]);
  history_.commit();
  studio_.mark_dirty();
}

void SpriteEditor::stroke_to(int x, int y) {
  Area a = target();
  auto& c = studio_.cart();
  int x0 = last_x_, y0 = last_y_;
  int dx = std::abs(x - x0), sx = x0 < x ? 1 : -1, dy = -std::abs(y - y0), sy = y0 < y ? 1 : -1, err = dx + dy;
  for (;;) {
    if (x0 >= a.x && y0 >= a.y && x0 < a.x + a.w && y0 < a.y + a.h) set_sheet_px(c, x0, y0, color_);
    if (x0 == x && y0 == y) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  last_x_ = x;
  last_y_ = y;
}

void SpriteEditor::flood(int x, int y) {
  Area a = target();
  auto& c = studio_.cart();
  int from = sheet_px(c, x, y);
  if (from == color_) return;
  std::vector<std::pair<int, int>> stack{{x, y}};
  while (!stack.empty()) {
    auto [px, py] = stack.back();
    stack.pop_back();
    if (px < a.x || py < a.y || px >= a.x + a.w || py >= a.y + a.h) continue;
    if (sheet_px(c, px, py) != from) continue;
    set_sheet_px(c, px, py, color_);
    stack.push_back({px + 1, py});
    stack.push_back({px - 1, py});
    stack.push_back({px, py + 1});
    stack.push_back({px, py - 1});
  }
}

void SpriteEditor::shape(int x0, int y0, int x1, int y1, bool filled) {
  Area a = target();
  auto& c = studio_.cart();
  // Restore the block, then draw the shape fresh, so dragging shows a live preview.
  for (int y = 0; y < block(); ++y)
    for (int x = 0; x < block(); ++x)
      set_sheet_px(c, origin_x() + x, origin_y() + y, before_[size_t(y) * block() + x]);
  auto put = [&](int x, int y) {
    if (x >= a.x && y >= a.y && x < a.x + a.w && y < a.y + a.h) set_sheet_px(c, x, y, color_);
  };
  if (tool_ == Tool::kLine) {
    last_x_ = x0;
    last_y_ = y0;
    stroke_to(x1, y1);
    return;
  }
  int lx = std::min(x0, x1), hx = std::max(x0, x1), ly = std::min(y0, y1), hy = std::max(y0, y1);
  if (tool_ == Tool::kRect) {
    for (int y = ly; y <= hy; ++y)
      for (int x = lx; x <= hx; ++x)
        if (filled || x == lx || x == hx || y == ly || y == hy) put(x, y);
    return;
  }
  // Ellipse fitted to the box, sampled per pixel center so small circles stay round.
  double cx = (lx + hx) / 2.0, cy = (ly + hy) / 2.0, rx = (hx - lx) / 2.0 + 0.5, ry = (hy - ly) / 2.0 + 0.5;
  auto inside = [&](int x, int y) {
    double nx = (x - cx) / rx, ny = (y - cy) / ry;
    return nx * nx + ny * ny <= 1.0;
  };
  for (int y = ly; y <= hy; ++y)
    for (int x = lx; x <= hx; ++x) {
      if (!inside(x, y)) continue;
      bool edge = !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
      if (filled || edge) put(x, y);
    }
}

void SpriteEditor::keys(ui::Ui& ui) {
  auto& in = ui.in();
  if (in.ctrl() || in.alt()) return;
  auto kp = [&](const char* k) { return in.keyp(key_from_name(k)); };
  auto kr = [&](const char* k) { return in.keyr(key_from_name(k)); };
  if (kp("p")) tool_ = Tool::kPencil;
  if (kp("f")) tool_ = Tool::kFill;
  if (kp("l")) tool_ = Tool::kLine;
  if (kp("r")) tool_ = Tool::kRect;
  if (kp("c")) tool_ = Tool::kCircle;
  if (kp("s")) tool_ = Tool::kSelect;
  if (kp("i")) tool_ = Tool::kPicker;
  if (kp("h")) flip(true);
  if (kp("v")) flip(false);
  if (kp("t")) rotate();
  if (kp("o")) onion_ = !onion_;
  if (kp("g")) grid_ = !grid_;
  if (kp("space")) anim_ = !anim_;
  if (kr("[")) set_sprite(sprite_ - size_);
  if (kr("]")) set_sprite(sprite_ + size_);
  if (kr("left")) set_sprite(sprite_ - size_);
  if (kr("right")) set_sprite(sprite_ + size_);
  if (kr("up")) set_sprite(sprite_ - 16 * size_);
  if (kr("down")) set_sprite(sprite_ + 16 * size_);
  if (kr("-")) color_ = (color_ + kColors - 1) % kColors;
  if (kr("=")) color_ = (color_ + 1) % kColors;
  if (kp("1") || kp("2") || kp("4")) {
    size_ = kp("1") ? 1 : (kp("2") ? 2 : 4);
    set_sprite(sprite_);
  }
  if (kp("return") || kp("escape")) {
    commit_floating();
    has_sel_ = false;
  }
}

void SpriteEditor::draw_canvas(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  auto& c = studio_.cart();
  scale_ = kCanvas / block();
  int B = block();

  // Map the mouse to a sheet pixel.
  hover_x_ = hover_y_ = -1;
  int mx = (ui.mx() - r.x) / scale_, my = (ui.my() - r.y) / scale_;
  bool over = ui.hovered(r);
  if (over || drawing_ || moving_) {
    hover_x_ = origin_x() + std::clamp(mx, 0, B - 1);
    hover_y_ = origin_y() + std::clamp(my, 0, B - 1);
  }

  // Input.
  if (over && ui.clicked(2)) color_ = sheet_px(c, hover_x_, hover_y_);
  if (over && ui.wheel()) {
    size_ = std::clamp(ui.wheel() > 0 ? size_ / 2 : size_ * 2, 1, 4);
    set_sprite(sprite_);
    return;
  }
  if (over && ui.clicked(0)) {
    if (floating_ && !(hover_x_ >= float_x_ && hover_y_ >= float_y_ && hover_x_ < float_x_ + float_.w &&
                       hover_y_ < float_y_ + float_.h)) {
      commit_floating();
    } else if (floating_) {
      moving_ = true;
      grab_x_ = hover_x_ - float_x_;
      grab_y_ = hover_y_ - float_y_;
    } else if (tool_ == Tool::kPicker) {
      color_ = sheet_px(c, hover_x_, hover_y_);
      tool_ = Tool::kPencil;
    } else if (tool_ == Tool::kSelect) {
      if (has_sel_ && hover_x_ >= sel_.x && hover_y_ >= sel_.y && hover_x_ < sel_.x + sel_.w &&
          hover_y_ < sel_.y + sel_.h) {
        // Lift the selection so it can be dragged somewhere else.
        float_ = {sel_.w, sel_.h, {}};
        history_.begin(Target::kGfx);
        for (int y = 0; y < sel_.h; ++y)
          for (int x = 0; x < sel_.w; ++x) {
            float_.px.push_back(uint8_t(sheet_px(c, sel_.x + x, sel_.y + y)));
            set_sheet_px(c, sel_.x + x, sel_.y + y, 0);
          }
        history_.commit();
        floating_ = moving_ = true;
        float_x_ = sel_.x;
        float_y_ = sel_.y;
        grab_x_ = hover_x_ - sel_.x;
        grab_y_ = hover_y_ - sel_.y;
        has_sel_ = false;
      } else {
        drawing_ = true;
        start_x_ = hover_x_;
        start_y_ = hover_y_;
        has_sel_ = false;
      }
    } else {
      drawing_ = true;
      history_.begin(Target::kGfx);
      start_x_ = last_x_ = hover_x_;
      start_y_ = last_y_ = hover_y_;
      before_.assign(size_t(B) * B, 0);
      for (int y = 0; y < B; ++y)
        for (int x = 0; x < B; ++x) before_[size_t(y) * B + x] = uint8_t(sheet_px(c, origin_x() + x, origin_y() + y));
      if (tool_ == Tool::kPencil) stroke_to(hover_x_, hover_y_);
      if (tool_ == Tool::kFill) flood(hover_x_, hover_y_);
    }
    ui.capture(kSelectionId);
  }
  if (drawing_ && ui.mouse_down(0)) {
    if (tool_ == Tool::kPencil) stroke_to(hover_x_, hover_y_);
    else if (tool_ == Tool::kLine || tool_ == Tool::kRect || tool_ == Tool::kCircle)
      shape(start_x_, start_y_, hover_x_, hover_y_, ui.in().shift() || ui.in().ctrl());
    else if (tool_ == Tool::kSelect) {
      int x0 = std::min(start_x_, hover_x_), y0 = std::min(start_y_, hover_y_);
      sel_ = {x0, y0, std::abs(hover_x_ - start_x_) + 1, std::abs(hover_y_ - start_y_) + 1};
      has_sel_ = sel_.w * sel_.h > 1;
    }
  }
  if (moving_ && ui.mouse_down(0)) {
    float_x_ = hover_x_ - grab_x_;
    float_y_ = hover_y_ - grab_y_;
  }
  if (ui.released(0)) {
    if (drawing_ && tool_ != Tool::kSelect && history_.commit()) studio_.mark_dirty();
    drawing_ = moving_ = false;
  }

  // Drawing: transparent pixels as a checkerboard, onion skin, grid, selection.
  for (int y = 0; y < B; ++y)
    for (int x = 0; x < B; ++x) {
      int px = sheet_px(c, origin_x() + x, origin_y() + y);
      int x0 = r.x + x * scale_, y0 = r.y + y * scale_;
      if (px) {
        g.rectfill(x0, y0, x0 + scale_ - 1, y0 + scale_ - 1, px);
        continue;
      }
      g.rectfill(x0, y0, x0 + scale_ - 1, y0 + scale_ - 1, ((x + y) & 1) ? th.bg : th.panel);
      if (onion_ && sprite_ % 16 >= size_) {
        int prev = sheet_px(c, origin_x() - B + x, origin_y() + y);
        if (prev) {
          g.fillp(0x5a5a);
          g.rectfill(x0, y0, x0 + scale_ - 1, y0 + scale_ - 1, prev);
          g.fillp(0);
        }
      }
    }
  if (floating_) {
    for (int y = 0; y < float_.h; ++y)
      for (int x = 0; x < float_.w; ++x) {
        int col = float_.px[size_t(y) * float_.w + x];
        int cx = float_x_ + x - origin_x(), cy = float_y_ + y - origin_y();
        if (!col || cx < 0 || cy < 0 || cx >= B || cy >= B) continue;
        g.rectfill(r.x + cx * scale_, r.y + cy * scale_, r.x + (cx + 1) * scale_ - 1, r.y + (cy + 1) * scale_ - 1, col);
      }
  }
  if (grid_ && scale_ >= 6) {
    for (int i = 1; i < B; ++i) {
      int c8 = (i % 8 == 0) ? th.raised + 1 : th.raised;
      if (i % 8 == 0 || scale_ >= 12) {
        g.fillp(i % 8 == 0 ? 0 : 0xaaaa);
        g.line(r.x + i * scale_, r.y, r.x + i * scale_, r.bottom(), c8);
        g.line(r.x, r.y + i * scale_, r.right(), r.y + i * scale_, c8);
        g.fillp(0);
      }
    }
  }
  auto outline = [&](const Area& a, int color) {
    int x0 = r.x + (a.x - origin_x()) * scale_, y0 = r.y + (a.y - origin_y()) * scale_;
    g.fillp((ui.frame() / 4) % 2 ? 0x3333 : 0xcccc);
    g.rect(x0, y0, x0 + a.w * scale_ - 1, y0 + a.h * scale_ - 1, color);
    g.fillp(0);
  };
  if (has_sel_) outline(sel_, 31);
  if (floating_) outline({float_x_, float_y_, float_.w, float_.h}, th.accent);
  if (over && !floating_ && (tool_ == Tool::kPencil || tool_ == Tool::kFill || tool_ == Tool::kPicker)) {
    int x0 = r.x + (hover_x_ - origin_x()) * scale_, y0 = r.y + (hover_y_ - origin_y()) * scale_;
    g.rect(x0, y0, x0 + scale_ - 1, y0 + scale_ - 1, tool_ == Tool::kPicker ? 31 : color_);
  }
  g.rect(r.x - 1, r.y - 1, r.right() + 1, r.bottom() + 1, th.shadow);
}

void SpriteEditor::draw_tools(ui::Ui& ui, int x, int y) {
  struct Item {
    ui::Icon icon;
    Tool tool;
    const char* tip;
  };
  static constexpr Item kTools[] = {
      {ui::Icon::kPencil, Tool::kPencil, "pencil (p) - right-click picks a color"},
      {ui::Icon::kFill, Tool::kFill, "fill (f)"},
      {ui::Icon::kLine, Tool::kLine, "line (l)"},
      {ui::Icon::kRect, Tool::kRect, "rectangle (r) - hold shift to fill"},
      {ui::Icon::kCircle, Tool::kCircle, "circle (c) - hold shift to fill"},
      {ui::Icon::kSelect, Tool::kSelect, "select (s) - drag a selection to move it"},
      {ui::Icon::kPicker, Tool::kPicker, "color picker (i)"},
  };
  for (int i = 0; i < 7; ++i) {
    const auto& t = kTools[i];
    if (ui.icon_button(x + (i % 2) * 12, y + (i / 2) * 10, t.icon, tool_ == t.tool, t.tip)) {
      commit_floating();
      tool_ = t.tool;
    }
  }
  if (ui.icon_button(x + 12, y + 30, ui::Icon::kGrid, grid_, "grid (g)")) grid_ = !grid_;
  if (ui.icon_button(x, y + 40, ui::Icon::kFlipH, false, "flip horizontally (h)")) flip(true);
  if (ui.icon_button(x + 12, y + 40, ui::Icon::kFlipV, false, "flip vertically (v)")) flip(false);
  if (ui.icon_button(x, y + 50, ui::Icon::kRotate, false, "rotate clockwise (t)")) rotate();
  if (ui.icon_button(x + 12, y + 50, ui::Icon::kOnion, onion_, "onion skin: show the previous sprite (o)"))
    onion_ = !onion_;
}

void SpriteEditor::draw_flags(ui::Ui& ui, int x, int y) {
  auto& c = studio_.cart();
  for (int f = 0; f < 8; ++f) {
    bool on = (c.flags[size_t(sprite_)] >> f) & 1;
    if (ui.toggle(x + f * 8, y, on, kFlagColors[f], "flag " + std::to_string(f) + " - read with fget()")) {
      history_.begin(Target::kFlags);
      c.flags[size_t(sprite_)] = uint8_t(c.flags[size_t(sprite_)] ^ (1 << f));
      history_.commit();
      studio_.mark_dirty();
    }
  }
}

void SpriteEditor::draw_preview(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& c = studio_.cart();
  ui.fill(r, th.bg);
  int frame = 0;
  if (anim_) {
    ++anim_tick_;
    frame = int(anim_tick_ * uint64_t(anim_fps_) / kFps) % std::max(1, anim_frames_);
  }
  int n = sprite_ + frame * size_;
  int ox = (n % 16) * 8, oy = (n / 16) * 8, B = block(), inner = r.w - 2;
  // Nearest-neighbour fit of the block into the preview, any size.
  for (int y = 0; y < inner; ++y)
    for (int x = 0; x < inner; ++x) {
      int col = sheet_px(c, ox + x * B / inner, oy + y * B / inner);
      if (col) ui.g().pset(r.x + 1 + x, r.y + 1 + y, col);
    }
  ui.g().rect(r.x, r.y, r.right(), r.bottom(), th.shadow);
  if (ui.hovered(r)) {
    ui.tooltip("preview: wheel sets frames, shift+wheel speed");
    if (ui.wheel() && ui.in().shift()) anim_fps_ = std::clamp(anim_fps_ + ui.wheel(), 1, 30);
    else if (ui.wheel()) anim_frames_ = std::clamp(anim_frames_ + ui.wheel(), 1, 16);
    if (ui.clicked(0)) anim_ = !anim_;
  }
}

void SpriteEditor::draw_sheet(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  draw_sheet_area(ui, studio_.cart(), 0, sheet_ * kSheetH, kSheetW, kSheetH, r.x, r.y, 1, th.bg);
  // Faint marks at sprite corners so an empty sheet still shows its grid.
  for (int y = 8; y < kSheetH; y += 8)
    for (int x = 8; x < kSheetW; x += 8)
      if (sheet_px(studio_.cart(), x, sheet_ * kSheetH + y) == 0) g.pset(r.x + x, r.y + y, th.panel);
  if (ui.hovered(r)) {
    int col = (ui.mx() - r.x) / 8, row = (ui.my() - r.y) / 8;
    int n = sheet_ * 256 + row * 16 + col;
    g.rect(r.x + col * 8, r.y + row * 8, r.x + col * 8 + 7, r.y + row * 8 + 7, th.dim);
    ui.tooltip("#" + std::to_string(n));
    if (ui.clicked(0)) set_sprite(n);
  }
  int sx = r.x + (sprite_ % 16) * 8, sy = r.y + ((sprite_ / 16) % 16) * 8, B = block();
  g.rect(sx - 1, sy - 1, sx + B, sy + B, th.shadow);
  g.rect(sx - 2, sy - 2, sx + B + 1, sy + B + 1, 31);
}

void SpriteEditor::update(ui::Ui& ui, const ui::Rect& body) {
  const auto& th = ui.theme();
  keys(ui);
  int y0 = body.y;

  draw_canvas(ui, {kCanvasX, y0 + 2, kCanvas, kCanvas});
  draw_tools(ui, kColumnX, y0 + 2);

  if (ui.button({kColumnX, y0 + 64, 12, 8}, "s0", sheet_ == 0, "sheet 0: sprites 0-255")) set_sprite(sprite_ % 256);
  if (ui.button({kColumnX + 13, y0 + 64, 12, 8}, "s1", sheet_ == 1, "sheet 1: sprites 256-511"))
    set_sprite(256 + sprite_ % 256);
  draw_preview(ui, {kColumnX, y0 + 74, kPreview, kPreview});
  if (ui.icon_button(kColumnX - 1, y0 + 101, anim_ ? ui::Icon::kStop : ui::Icon::kPlay, anim_, "play animation (space)"))
    anim_ = !anim_;
  ui.text(std::to_string(anim_frames_) + "f", kColumnX + 11, y0 + 102, th.dim);
  ui.text(std::to_string(anim_fps_) + "/s", kColumnX + 11, y0 + 108, th.dim);

  int py = y0 + kCanvas + 4;
  ui.palette(kCanvasX, py, 6, color_, 16);
  int ry = py + 14;
  char id[8];
  std::snprintf(id, sizeof id, "#%03d", sprite_);
  ui.text(id, kCanvasX, ry + 1, th.text);
  for (int i = 0; i < 3; ++i) {
    int s = 1 << i;
    std::string label = std::to_string(s * 8);
    if (ui.button({kCanvasX + 18 + i * 11, ry, 10, 7}, label, size_ == s, "edit " + label + "x" + label + " pixels (" + std::to_string(s) + ")")) {
      size_ = s;
      set_sprite(sprite_);
    }
  }
  draw_flags(ui, kCanvasX + 53, ry);

  draw_sheet(ui, {kSheetX, y0, kSheetW, kSheetH});
}

std::string SpriteEditor::status() const {
  static constexpr const char* kToolNames[] = {"pencil", "fill", "line", "rect", "circle", "select", "picker"};
  char buf[96];
  if (hover_x_ >= 0) {
    std::snprintf(buf, sizeof buf, "#%03d  x%d y%d  color %d  %s", sprite_, hover_x_ - origin_x(), hover_y_ - origin_y(),
                  sheet_px(studio_.cart(), hover_x_, hover_y_), kToolNames[int(tool_)]);
  } else {
    std::snprintf(buf, sizeof buf, "#%03d  %dx%d  %s  [ ] change sprite  1 2 4 zoom", sprite_, block(), block(),
                  kToolNames[int(tool_)]);
  }
  return buf;
}

}  // namespace glim::editors
