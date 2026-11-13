#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/memory.hpp"
#include "gfx/gfx.hpp"
#include "input/input.hpp"
#include "ui/icons.hpp"

namespace glim::ui {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  int right() const { return x + w - 1; }
  int bottom() const { return y + h - 1; }
};

// Palette indices for every UI element, so themes are pure data.
struct Theme {
  uint8_t bg;          // behind canvases
  uint8_t panel;       // editor background
  uint8_t raised;      // buttons and bars
  uint8_t hot;         // hovered controls
  uint8_t shadow;      // bevels and outlines
  uint8_t text;
  uint8_t dim;         // secondary text
  uint8_t accent;      // selected controls, the top bar
  uint8_t accent_text; // text on accent
  uint8_t select;      // text selection and marquees
  uint8_t error;
  uint8_t ok;
  uint8_t warn;
};

Theme default_theme();
Theme high_contrast_theme();

// Immediate-mode UI drawn at console resolution into its own framebuffer, so
// editors never touch the cart's RAM. Widgets are functions that draw and
// return what the user did this frame.
class Ui {
 public:
  explicit Ui(input::Input& in);

  void begin_frame();
  void end_frame();
  const uint32_t* display();

  input::Input& in() { return in_; }
  gfx::Gfx& g() { return g_; }
  Memory& mem() { return mem_; }
  const Theme& theme() const { return theme_; }
  void set_theme(const Theme& t) { theme_ = t; }
  uint64_t frame() const { return frame_; }

  // Mouse state in console pixels.
  int mx() const { return mx_; }
  int my() const { return my_; }
  bool mouse_down(int b = 0) const { return (buttons_ >> b) & 1; }
  bool clicked(int b = 0) const { return (pressed_ >> b) & 1; }
  bool released(int b = 0) const { return (released_ >> b) & 1; }
  bool double_clicked() const { return double_click_; }
  int wheel() const { return wheel_; }
  bool hovered(const Rect& r) const { return (!blocked_ || in_popup_) && r.contains(mx_, my_); }
  bool clicked_in(const Rect& r, int b = 0) const { return clicked(b) && hovered(r); }

  // Drag capture: the widget that took the mouse keeps it until release, even
  // when the pointer leaves its rectangle.
  void capture(int id) { capture_ = id; }
  bool captured(int id) const { return capture_ == id; }
  bool any_capture() const { return capture_ != 0; }

  // Widgets drawn between begin_popup and end_popup sit above everything else:
  // from the next frame on, widgets underneath the popup ignore the mouse.
  void begin_popup(const Rect& r) {
    popups_.push_back(r);
    in_popup_ = true;
  }
  void end_popup() { in_popup_ = false; }

  // Drawing helpers.
  void fill(const Rect& r, int c);
  void frame(const Rect& r, int fill, bool sunken = false);
  void text(std::string_view s, int x, int y, int c);
  void text_centered(std::string_view s, const Rect& r, int c);
  void icon(Icon icon, int x, int y, int c);

  // Widgets. All return true on the frame they're activated.
  bool button(const Rect& r, std::string_view label, bool selected = false, std::string_view tip = {});
  bool icon_button(int x, int y, Icon icon, bool selected = false, std::string_view tip = {}, int h = 10);
  bool toggle(int x, int y, bool& value, int on_color, std::string_view tip = {});
  bool slider(const Rect& r, int& value, int lo, int hi, std::string_view tip = {});
  bool spinner(int x, int y, std::string_view label, int& value, int lo, int hi, std::string_view tip = {});
  // Single-line text field; returns true when Enter is pressed while focused.
  bool text_field(const Rect& r, std::string& value, bool focused);
  // The next edit in a focused text field replaces its whole contents.
  void select_all_text() { text_all_ = true; }
  bool palette(int x, int y, int cell, int& color, int columns = 8);

  void tooltip(std::string_view tip);

 private:
  int widget_id(int x, int y) const { return (y << 9) | (x + 1); }

  input::Input& in_;
  Memory mem_;
  gfx::Gfx g_{mem_};
  Theme theme_;
  std::vector<uint32_t> rgb_;
  std::vector<Rect> popups_;

  int mx_ = 0, my_ = 0;
  uint8_t buttons_ = 0, prev_buttons_ = 0, pressed_ = 0, released_ = 0;
  int wheel_ = 0;
  bool blocked_ = false;
  bool in_popup_ = false;
  bool double_click_ = false;
  uint64_t last_click_frame_ = 0;
  int last_click_x_ = -100, last_click_y_ = -100;
  int capture_ = 0;
  uint64_t frame_ = 0;
  int still_frames_ = 0;
  std::string tip_;
  size_t text_cursor_ = std::string::npos;
  bool text_all_ = false;
};

}  // namespace glim::ui
