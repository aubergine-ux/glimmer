#include "ui/ui.hpp"

#include <algorithm>
#include <cstdlib>

#include "gfx/font.hpp"

namespace glim::ui {

namespace {
constexpr int kDoubleClickFrames = 18;
constexpr int kTooltipDelay = 24;
constexpr int kCursorBlink = 32;
}  // namespace

Theme default_theme() {
  Theme t{};
  t.bg = 0;
  t.panel = 1;
  t.raised = 2;
  t.hot = 3;
  t.shadow = 0;
  t.text = 5;
  t.dim = 6;
  t.accent = 29;
  t.accent_text = 5;
  t.select = 4;
  t.error = 11;
  t.ok = 21;
  t.warn = 17;
  return t;
}

Theme high_contrast_theme() {
  Theme t{};
  t.bg = 0;
  t.panel = 0;
  t.raised = 8;
  t.hot = 7;
  t.shadow = 31;
  t.text = 31;
  t.dim = 18;
  t.accent = 17;
  t.accent_text = 0;
  t.select = 25;
  t.error = 11;
  t.ok = 21;
  t.warn = 16;
  return t;
}

Ui::Ui(input::Input& in) : in_(in), theme_(default_theme()), rgb_(size_t(kScreenW) * kScreenH) {
  g_.reset_palette();
  g_.reset_state();
}

void Ui::begin_frame() {
  ++frame_;
  const auto& m = in_.mouse();
  if (m.x == mx_ && m.y == my_) ++still_frames_;
  else still_frames_ = 0;
  mx_ = m.x;
  my_ = m.y;
  prev_buttons_ = buttons_;
  buttons_ = m.buttons;
  pressed_ = buttons_ & ~prev_buttons_;
  released_ = prev_buttons_ & ~buttons_;
  wheel_ = m.wheel;

  double_click_ = false;
  if (pressed_ & 1) {
    double_click_ = frame_ - last_click_frame_ <= kDoubleClickFrames && std::abs(mx_ - last_click_x_) <= 2 &&
                    std::abs(my_ - last_click_y_) <= 2;
    last_click_frame_ = double_click_ ? 0 : frame_;
    last_click_x_ = mx_;
    last_click_y_ = my_;
  }

  blocked_ = false;
  for (const auto& r : popups_)
    if (r.contains(mx_, my_)) blocked_ = true;
  popups_.clear();
  tip_.clear();
  g_.reset_state();
}

void Ui::end_frame() {
  if (!tip_.empty() && still_frames_ >= kTooltipDelay && !any_capture()) {
    int w = gfx::Gfx::text_width(tip_) + 4;
    int x = std::clamp(mx_ + 6, 0, kScreenW - w), y = my_ + 10;
    if (y + 9 > kScreenH) y = my_ - 10;
    fill({x, y, w, 9}, theme_.text);
    text(tip_, x + 2, y + 2, theme_.panel);
  }
  // Software cursor: drawn into the frame so it scales and records with it.
  const uint8_t* rows = icon_rows(Icon::kCursor);
  for (int pass = 0; pass < 2; ++pass) {
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        if (!((rows[y] >> (7 - x)) & 1)) continue;
        if (pass == 0) {
          for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) g_.pset(mx_ + x + ox, my_ + y + oy, theme_.shadow);
        } else {
          g_.pset(mx_ + x, my_ + y, 31);
        }
      }
    }
  }
  if (!mouse_down(0) && !mouse_down(1) && !mouse_down(2)) capture_ = 0;
}

const uint32_t* Ui::display() {
  g_.to_rgb(rgb_.data());
  return rgb_.data();
}

void Ui::fill(const Rect& r, int c) {
  if (r.w > 0 && r.h > 0) g_.rectfill(r.x, r.y, r.right(), r.bottom(), c);
}

void Ui::frame(const Rect& r, int fill_color, bool sunken) {
  fill(r, fill_color);
  int light = sunken ? theme_.shadow : theme_.hot, dark = sunken ? theme_.hot : theme_.shadow;
  if (fill_color == theme_.hot) light = theme_.text;
  g_.line(r.x, r.y, r.right() - 1, r.y, light);
  g_.line(r.x, r.y, r.x, r.bottom() - 1, light);
  g_.line(r.x, r.bottom(), r.right(), r.bottom(), dark);
  g_.line(r.right(), r.y, r.right(), r.bottom(), dark);
}

void Ui::text(std::string_view s, int x, int y, int c) { g_.print(s, x, y, c); }

void Ui::text_centered(std::string_view s, const Rect& r, int c) {
  int w = gfx::Gfx::text_width(s);
  g_.print(s, r.x + (r.w - w + 1) / 2, r.y + (r.h - gfx::kGlyphH) / 2, c);
}

void Ui::icon(Icon ic, int x, int y, int c) {
  const uint8_t* rows = icon_rows(ic);
  for (int yy = 0; yy < 8; ++yy)
    for (int xx = 0; xx < 8; ++xx)
      if ((rows[yy] >> (7 - xx)) & 1) g_.pset(x + xx, y + yy, c);
}

void Ui::tooltip(std::string_view tip) { tip_ = tip; }

bool Ui::button(const Rect& r, std::string_view label, bool selected, std::string_view tip) {
  bool hot = hovered(r);
  bool held = hot && mouse_down(0);
  int bg = selected ? theme_.accent : (hot ? theme_.hot : theme_.raised);
  frame(r, bg, held);
  text_centered(label, {r.x, r.y + (held ? 1 : 0), r.w, r.h}, selected ? theme_.accent_text : theme_.text);
  if (hot && !tip.empty()) tooltip(tip);
  return hot && clicked(0);
}

bool Ui::icon_button(int x, int y, Icon ic, bool selected, std::string_view tip) {
  Rect r{x, y, 10, 10};
  bool hot = hovered(r);
  if (selected) fill(r, theme_.accent);
  else if (hot) fill(r, theme_.hot);
  int c = selected ? theme_.accent_text : (hot ? theme_.text : theme_.dim);
  icon(ic, x + 1, y + 1 + (hot && mouse_down(0) ? 1 : 0), c);
  if (hot && !tip.empty()) tooltip(tip);
  return hot && clicked(0);
}

bool Ui::toggle(int x, int y, bool& value, int on_color, std::string_view tip) {
  Rect r{x, y, 7, 7};
  bool hot = hovered(r);
  g_.rect(x, y, x + 6, y + 6, hot ? theme_.text : theme_.shadow);
  fill({x + 1, y + 1, 5, 5}, value ? on_color : theme_.bg);
  if (hot && !tip.empty()) tooltip(tip);
  if (hot && clicked(0)) {
    value = !value;
    return true;
  }
  return false;
}

bool Ui::slider(const Rect& r, int& value, int lo, int hi, std::string_view tip) {
  int id = widget_id(r.x, r.y);
  bool hot = hovered(r);
  if (hot && clicked(0)) capture(id);
  int old = value;
  if (captured(id) && hi > lo) value = lo + std::clamp((mx_ - r.x) * (hi - lo + 1) / std::max(1, r.w), 0, hi - lo);
  if (hot && wheel_) value = std::clamp(value + wheel_, lo, hi);
  fill(r, theme_.bg);
  int knob = hi > lo ? r.x + (value - lo) * (r.w - 3) / (hi - lo) : r.x;
  fill({r.x, r.y + r.h / 2, knob - r.x, 1}, theme_.accent);
  fill({knob, r.y, 3, r.h}, hot || captured(id) ? theme_.text : theme_.dim);
  if (hot && !tip.empty()) tooltip(tip);
  return value != old;
}

bool Ui::spinner(int x, int y, std::string_view label, int& value, int lo, int hi, std::string_view tip) {
  int old = value;
  int lw = label.empty() ? 0 : gfx::Gfx::text_width(label) + 2;
  if (lw) text(label, x, y + 2, theme_.dim);
  x += lw;
  if (icon_button(x - 2, y - 1, Icon::kMinus)) value = std::max(lo, value - (in_.shift() ? 8 : 1));
  Rect field{x + 8, y, 15, 7};
  fill(field, theme_.bg);
  text_centered(std::to_string(value), field, theme_.text);
  if (hovered(field) && wheel_) value = std::clamp(value + wheel_, lo, hi);
  if (hovered(field) && !tip.empty()) tooltip(tip);
  if (icon_button(x + 22, y - 1, Icon::kPlus)) value = std::min(hi, value + (in_.shift() ? 8 : 1));
  return value != old;
}

bool Ui::text_field(const Rect& r, std::string& value, bool focused) {
  fill(r, theme_.bg);
  if (focused) {
    using input::key_from_name;
    if (text_cursor_ > value.size()) text_cursor_ = value.size();
    if (in_.ctrl() && in_.keyp(key_from_name("a"))) text_all_ = true;
    bool edits = !in_.text().empty() || in_.keyr(key_from_name("backspace")) || in_.keyr(key_from_name("delete"));
    if (text_all_ && edits) {
      value.clear();
      text_cursor_ = 0;
      text_all_ = false;
      if (in_.text().empty()) return false;
    }
    if (text_all_ && (in_.keyp(key_from_name("left")) || in_.keyp(key_from_name("right")))) text_all_ = false;
    for (char ch : in_.text()) {
      if (uint8_t(ch) >= 32 && uint8_t(ch) < 127) value.insert(text_cursor_++, 1, ch);
    }
    if (in_.keyr(key_from_name("backspace")) && text_cursor_ > 0) value.erase(--text_cursor_, 1);
    if (in_.keyr(key_from_name("delete")) && text_cursor_ < value.size()) value.erase(text_cursor_, 1);
    if (in_.keyr(key_from_name("left")) && text_cursor_ > 0) --text_cursor_;
    if (in_.keyr(key_from_name("right")) && text_cursor_ < value.size()) ++text_cursor_;
    if (in_.keyp(key_from_name("home"))) text_cursor_ = 0;
    if (in_.keyp(key_from_name("end"))) text_cursor_ = value.size();
  }
  int max_chars = (r.w - 4) / gfx::kCharW;
  size_t first = 0;
  if (focused && text_cursor_ > size_t(max_chars)) first = text_cursor_ - max_chars;
  g_.clip(r.x, r.y, r.w, r.h);
  if (focused && text_all_ && !value.empty())
    fill({r.x + 1, r.y + 1, std::min(r.w - 2, int(value.size()) * gfx::kCharW + 2), r.h - 2}, theme_.select);
  text(std::string_view(value).substr(first), r.x + 2, r.y + (r.h - gfx::kGlyphH) / 2, theme_.text);
  if (focused && (frame_ / (kCursorBlink / 2)) % 2 == 0) {
    int cx = r.x + 1 + int(text_cursor_ - first) * gfx::kCharW;
    g_.line(cx, r.y + 1, cx, r.bottom() - 1, theme_.accent);
  }
  g_.clip();
  return focused && in_.keyp(input::key_from_name("return"));
}

bool Ui::palette(int x, int y, int cell, int& color, int columns) {
  int old = color;
  for (int c = 0; c < kColors; ++c) {
    Rect r{x + (c % columns) * cell, y + (c / columns) * cell, cell, cell};
    fill(r, c);
    if (hovered(r) && clicked(0)) color = c;
  }
  Rect sel{x + (color % columns) * cell, y + (color / columns) * cell, cell, cell};
  g_.rect(sel.x - 1, sel.y - 1, sel.right() + 1, sel.bottom() + 1, theme_.shadow);
  g_.rect(sel.x, sel.y, sel.right(), sel.bottom(), 31);
  return color != old;
}

}  // namespace glim::ui
