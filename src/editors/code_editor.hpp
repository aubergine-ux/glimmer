#pragma once

#include <string>
#include <utility>
#include <vector>

#include "editors/editor.hpp"
#include "editors/lua_lexer.hpp"

namespace glim::editors {

// Code tabs are stored in the cart as marker comments, so a cart's code stays
// a single valid Lua chunk and error line numbers need no translation.
inline constexpr std::string_view kTabMarker = "--8<-- ";

class CodeEditor : public Editor {
 public:
  explicit CodeEditor(Studio& studio);

  ui::Icon icon() const override { return ui::Icon::kCode; }
  const char* name() const override { return "code"; }
  void update(ui::Ui& ui, const ui::Rect& body) override;
  void reset() override;
  std::string status() const override;
  bool wants_escape() const override { return ac_open_ || find_open_ || ref_open_ || renaming_ >= 0; }

  void undo() override;
  void redo() override;
  void copy() override;
  void cut() override;
  void paste() override;

  // Jump to a runtime or syntax error and keep the line highlighted until the next edit.
  void show_error(int line, const std::string& message);

  // Exposed for tests: cursor as (line, column), both 0-based.
  std::pair<int, int> cursor_pos() const;
  void set_cursor(int line, int col);
  int tab() const { return tab_; }
  int tab_count() const { return int(tabs_.size()); }

 private:
  enum class EditKind { kTyping, kDeleting, kOther };
  struct Tab {
    int first = 0, last = 0;  // line range shown, excluding the marker line
    std::string name;
  };

  std::string& text();
  const std::string& text() const;
  void reindex();
  int line_count() const { return int(starts_.size()); }
  size_t line_start(int line) const;
  size_t line_end(int line) const;
  int line_of(size_t pos) const;
  int col_of(size_t pos) const { return int(pos - line_start(line_of(pos))); }
  size_t pos_at(int line, int col) const;
  std::string_view line_text(int line) const;
  int indent_of(int line) const;

  bool has_selection() const { return cursor_ != anchor_; }
  std::pair<size_t, size_t> selection() const;
  void move_to(size_t pos, bool extend);
  void select_tab(int index);
  int tab_of_line(int line) const;
  void clamp_to_tab();

  void begin_edit(EditKind kind);
  void end_edit();
  void replace(size_t from, size_t to, std::string_view with, EditKind kind);
  void insert_text(std::string_view s, EditKind kind);
  void newline();
  void indent_selection(bool outdent);
  void toggle_comment();
  void duplicate_line();
  void maybe_dedent();

  void handle_keys(ui::Ui& ui);
  void handle_mouse(ui::Ui& ui, const ui::Rect& area, int gutter);
  void ensure_visible(int rows, int cols);
  void update_autocomplete();
  void accept_autocomplete();
  void find_next(bool backwards);
  void replace_one();
  void replace_all();
  bool call_context(std::string& fn, int& arg) const;

  void draw_tabs(ui::Ui& ui, const ui::Rect& r);
  void draw_text(ui::Ui& ui, const ui::Rect& area, int gutter, int rows, int cols);
  void draw_find_bar(ui::Ui& ui, const ui::Rect& r);
  void draw_autocomplete(ui::Ui& ui, const ui::Rect& area, int gutter);
  void draw_signature(ui::Ui& ui, const ui::Rect& area, int gutter);
  void draw_reference(ui::Ui& ui, const ui::Rect& body);

  std::vector<size_t> starts_;
  std::vector<LexState> lex_;
  std::vector<Tab> tabs_;
  size_t indexed_size_ = std::string::npos;
  size_t indexed_hash_ = 0;
  int tab_ = 0;

  size_t cursor_ = 0, anchor_ = 0;
  int want_col_ = 0;
  int scroll_ = 0, hscroll_ = 0;
  int rows_ = 1, cols_ = 1;
  int blink_ = 0;
  bool dragging_ = false;
  EditKind last_kind_ = EditKind::kOther;
  int idle_ = 0;

  bool find_open_ = false, replace_open_ = false;
  int find_focus_ = 0;  // 0 = code, 1 = find field, 2 = replace field
  std::string find_, replace_with_;
  bool find_case_ = false;

  bool ac_open_ = false;
  std::vector<std::string> ac_items_;
  int ac_sel_ = 0;
  bool ac_browsed_ = false;
  size_t ac_start_ = 0;
  std::vector<std::string> idents_;
  size_t idents_hash_ = 0;

  bool ref_open_ = false;
  std::string ref_filter_;
  int ref_sel_ = 0, ref_scroll_ = 0;

  int renaming_ = -1;
  std::string rename_buf_;

  int error_line_ = -1;
  std::string error_msg_;
  std::string hint_;
};

}  // namespace glim::editors
