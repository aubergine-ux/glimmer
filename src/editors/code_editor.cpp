#include "editors/code_editor.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

#include "editors/api_docs.hpp"
#include "editors/studio.hpp"
#include "gfx/font.hpp"

namespace glim::editors {

namespace {

using input::key_from_name;

constexpr int kTabStripH = 8;
constexpr int kFindBarH = 9;
constexpr int kIndent = 2;
constexpr int kIdleCommitFrames = 45;
constexpr int kAutocompleteRows = 5;
constexpr int kMinPrefix = 2;

constexpr std::string_view kLuaWords[] = {"function", "local", "return", "then", "else", "elseif",
                                          "while",    "repeat", "until", "break", "false", "true"};

// Palette index for each token class.
int tok_color(Tok t) {
  switch (t) {
    case Tok::kKeyword: return 30;
    case Tok::kApi: return 25;
    case Tok::kCallback: return 28;
    case Tok::kConst: return 12;
    case Tok::kString: return 17;
    case Tok::kNumber: return 16;
    case Tok::kComment: return 6;
    case Tok::kOp: return 4;
    default: return 5;
  }
}

bool is_marker(std::string_view line) { return line.starts_with(kTabMarker.substr(0, kTabMarker.size() - 1)); }

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

bool opens_block(std::string_view line) {
  line = trim(line);
  // Strip a trailing comment so "if x then -- note" still indents.
  if (auto c = line.find("--"); c != std::string_view::npos) line = trim(line.substr(0, c));
  auto ends = [&](std::string_view w) {
    return line.ends_with(w) && (line.size() == w.size() || !is_ident_char(line[line.size() - w.size() - 1]));
  };
  if (ends("then") || ends("do") || ends("else") || ends("repeat") || line.ends_with("{")) return true;
  if (line.ends_with(")") && line.find("function") != std::string_view::npos && line.find("end") == std::string_view::npos)
    return true;
  return false;
}

char lower(char c) { return char(std::tolower(uint8_t(c))); }

size_t find_in(std::string_view hay, std::string_view needle, size_t from, bool match_case) {
  if (needle.empty()) return std::string_view::npos;
  if (match_case) return hay.find(needle, from);
  for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
    size_t k = 0;
    while (k < needle.size() && lower(hay[i + k]) == lower(needle[k])) ++k;
    if (k == needle.size()) return i;
  }
  return std::string_view::npos;
}

}  // namespace

CodeEditor::CodeEditor(Studio& studio) : Editor(studio) {}

std::string& CodeEditor::text() { return studio_.cart().code; }
const std::string& CodeEditor::text() const { return studio_.cart().code; }

void CodeEditor::reset() {
  Editor::reset();
  cursor_ = anchor_ = 0;
  scroll_ = hscroll_ = 0;
  tab_ = 0;
  error_line_ = -1;
  error_msg_.clear();
  ac_open_ = false;
  indexed_size_ = std::string::npos;
  reindex();
}

void CodeEditor::reindex() {
  const std::string& s = text();
  size_t h = std::hash<std::string>{}(s);
  if (s.size() == indexed_size_ && h == indexed_hash_) return;
  indexed_size_ = s.size();
  indexed_hash_ = h;

  starts_.assign(1, 0);
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i] == '\n') starts_.push_back(i + 1);

  lex_.resize(starts_.size());
  LexState st;
  std::vector<Tok> scratch;
  for (int l = 0; l < line_count(); ++l) {
    lex_[l] = st;
    st = lex_line(line_text(l), st, scratch);
  }

  tabs_.clear();
  Tab cur{0, 0, "main"};
  for (int l = 0; l < line_count(); ++l) {
    if (!is_marker(line_text(l))) continue;
    cur.last = l - 1;
    tabs_.push_back(cur);
    cur = {l + 1, 0, std::string(trim(line_text(l).substr(kTabMarker.size() - 1)))};
    if (cur.name.empty()) cur.name = "tab " + std::to_string(tabs_.size());
  }
  cur.last = line_count() - 1;
  tabs_.push_back(cur);
  tab_ = std::clamp(tab_, 0, int(tabs_.size()) - 1);
  cursor_ = std::min(cursor_, s.size());
  anchor_ = std::min(anchor_, s.size());
}

size_t CodeEditor::line_start(int line) const { return starts_[std::clamp(line, 0, line_count() - 1)]; }

size_t CodeEditor::line_end(int line) const {
  line = std::clamp(line, 0, line_count() - 1);
  return line + 1 < line_count() ? starts_[line + 1] - 1 : text().size();
}

int CodeEditor::line_of(size_t pos) const {
  auto it = std::upper_bound(starts_.begin(), starts_.end(), pos);
  return int(it - starts_.begin()) - 1;
}

size_t CodeEditor::pos_at(int line, int col) const {
  line = std::clamp(line, 0, line_count() - 1);
  size_t start = line_start(line), end = line_end(line);
  return std::min(start + size_t(std::max(col, 0)), end);
}

std::string_view CodeEditor::line_text(int line) const {
  return std::string_view(text()).substr(line_start(line), line_end(line) - line_start(line));
}

int CodeEditor::indent_of(int line) const {
  auto t = line_text(line);
  int n = 0;
  while (n < int(t.size()) && t[n] == ' ') ++n;
  return n;
}

std::pair<size_t, size_t> CodeEditor::selection() const { return {std::min(cursor_, anchor_), std::max(cursor_, anchor_)}; }

std::pair<int, int> CodeEditor::cursor_pos() const { return {line_of(cursor_), col_of(cursor_)}; }

void CodeEditor::set_cursor(int line, int col) {
  reindex();
  move_to(pos_at(line, col), false);
}

int CodeEditor::tab_of_line(int line) const {
  for (int i = 0; i < int(tabs_.size()); ++i)
    if (line <= tabs_[i].last || i + 1 == int(tabs_.size())) return i;
  return 0;
}

void CodeEditor::select_tab(int index) {
  end_edit();
  tab_ = std::clamp(index, 0, int(tabs_.size()) - 1);
  move_to(line_start(tabs_[tab_].first), false);
  scroll_ = tabs_[tab_].first;
}

void CodeEditor::clamp_to_tab() {
  const Tab& t = tabs_[tab_];
  int first = t.first, last = std::max(t.first, t.last);
  if (first >= line_count()) return;
  size_t lo = line_start(first), hi = line_end(last);
  cursor_ = std::clamp(cursor_, lo, hi);
  anchor_ = std::clamp(anchor_, lo, hi);
}

void CodeEditor::move_to(size_t pos, bool extend) {
  cursor_ = std::min(pos, text().size());
  if (!extend) anchor_ = cursor_;
  clamp_to_tab();
  want_col_ = col_of(cursor_);
  blink_ = 0;
}

void CodeEditor::begin_edit(EditKind kind) {
  if (history_.open() && (kind != last_kind_ || kind == EditKind::kOther)) history_.commit(int(cursor_));
  history_.begin(Target::kCode, int(cursor_));
  last_kind_ = kind;
  idle_ = 0;
}

void CodeEditor::end_edit() {
  if (history_.open()) history_.commit(int(cursor_));
}

void CodeEditor::replace(size_t from, size_t to, std::string_view with, EditKind kind) {
  begin_edit(kind);
  text().replace(from, to - from, with);
  cursor_ = anchor_ = from + with.size();
  error_line_ = -1;
  studio_.mark_dirty();
  reindex();
  want_col_ = col_of(cursor_);
  if (kind == EditKind::kOther) end_edit();
}

void CodeEditor::insert_text(std::string_view s, EditKind kind) {
  auto [a, b] = selection();
  replace(a, b, s, has_selection() ? EditKind::kOther : kind);
}

void CodeEditor::newline() {
  int line = line_of(cursor_);
  auto before = std::string_view(text()).substr(line_start(line), cursor_ - line_start(line));
  int indent = indent_of(line);
  indent = std::min<int>(indent, int(before.size()));
  if (opens_block(before)) indent += kIndent;
  insert_text("\n" + std::string(size_t(indent), ' '), EditKind::kTyping);
}

void CodeEditor::maybe_dedent() {
  // Typing the last letter of "end", "else", "elseif" or "until" on a line of
  // its own snaps it back one level, matching the block it closes.
  int line = line_of(cursor_);
  auto t = trim(line_text(line));
  if (t != "end" && t != "else" && t != "until" && t != "elseif") return;
  if (cursor_ != line_end(line)) return;
  int ind = indent_of(line);
  if (ind < kIndent || line == 0) return;
  int prev = line - 1;
  while (prev > 0 && trim(line_text(prev)).empty()) --prev;
  int want = indent_of(prev) - (opens_block(line_text(prev)) ? 0 : kIndent);
  if (want < 0 || want >= ind) return;
  size_t start = line_start(line);
  begin_edit(EditKind::kTyping);
  text().erase(start, size_t(ind - want));
  cursor_ = anchor_ = cursor_ - size_t(ind - want);
  reindex();
}

void CodeEditor::indent_selection(bool outdent) {
  auto [a, b] = selection();
  int l0 = line_of(a), l1 = line_of(b);
  if (has_selection() && b == line_start(l1) && l1 > l0) --l1;
  begin_edit(EditKind::kOther);
  for (int l = l1; l >= l0; --l) {
    size_t start = line_start(l);
    if (outdent) {
      int n = std::min(indent_of(l), kIndent);
      text().erase(start, size_t(n));
    } else if (!line_text(l).empty() || l0 == l1) {
      text().insert(start, std::string(kIndent, ' '));
    }
  }
  reindex();
  anchor_ = line_start(l0);
  cursor_ = line_end(l1);
  if (l0 == l1 && !outdent) anchor_ = cursor_ = std::min(line_end(l0), cursor_);
  studio_.mark_dirty();
  end_edit();
}

void CodeEditor::toggle_comment() {
  auto [a, b] = selection();
  int l0 = line_of(a), l1 = line_of(b);
  if (has_selection() && b == line_start(l1) && l1 > l0) --l1;
  bool all_commented = true;
  for (int l = l0; l <= l1; ++l) {
    auto t = trim(line_text(l));
    if (!t.empty() && !t.starts_with("--")) all_commented = false;
  }
  begin_edit(EditKind::kOther);
  for (int l = l1; l >= l0; --l) {
    if (trim(line_text(l)).empty()) continue;
    size_t at = line_start(l) + size_t(indent_of(l));
    if (all_commented) {
      size_t n = (text().compare(at, 3, "-- ") == 0) ? 3 : 2;
      text().erase(at, n);
    } else {
      text().insert(at, "-- ");
    }
  }
  reindex();
  anchor_ = line_start(l0);
  cursor_ = line_end(l1);
  studio_.mark_dirty();
  end_edit();
}

void CodeEditor::duplicate_line() {
  int line = line_of(cursor_);
  int col = col_of(cursor_);
  std::string copy = "\n" + std::string(line_text(line));
  begin_edit(EditKind::kOther);
  text().insert(line_end(line), copy);
  reindex();
  cursor_ = anchor_ = pos_at(line + 1, col);
  studio_.mark_dirty();
  end_edit();
}

void CodeEditor::undo() {
  int c = history_.undo();
  if (c < 0) {
    studio_.message("nothing to undo");
    return;
  }
  reindex();
  tab_ = tab_of_line(line_of(size_t(c)));
  move_to(size_t(c), false);
}

void CodeEditor::redo() {
  int c = history_.redo();
  if (c < 0) {
    studio_.message("nothing to redo");
    return;
  }
  reindex();
  tab_ = tab_of_line(line_of(size_t(c)));
  move_to(size_t(c), false);
}

void CodeEditor::copy() {
  if (has_selection()) {
    auto [a, b] = selection();
    studio_.set_clipboard(text().substr(a, b - a));
  } else {
    // With nothing selected, copy the whole line, editor style.
    int line = line_of(cursor_);
    studio_.set_clipboard(std::string(line_text(line)) + "\n");
  }
}

void CodeEditor::cut() {
  copy();
  if (has_selection()) {
    auto [a, b] = selection();
    replace(a, b, "", EditKind::kOther);
  } else {
    int line = line_of(cursor_);
    size_t end = line + 1 < line_count() ? line_start(line + 1) : line_end(line);
    replace(line_start(line), end, "", EditKind::kOther);
  }
}

void CodeEditor::paste() {
  std::string clip = studio_.clipboard();
  std::string clean;
  clean.reserve(clip.size());
  for (char c : clip) {
    if (c == '\r') continue;
    if (c == '\t') clean += std::string(kIndent, ' ');
    else clean += c;
  }
  if (!clean.empty()) insert_text(clean, EditKind::kOther);
}

void CodeEditor::show_error(int line, const std::string& message) {
  reindex();
  error_line_ = std::clamp(line - 1, 0, line_count() - 1);
  error_msg_ = message;
  tab_ = tab_of_line(error_line_);
  move_to(pos_at(error_line_, indent_of(error_line_)), false);
  scroll_ = std::max(tabs_[tab_].first, error_line_ - 4);
}

// ---- autocomplete, search, signatures ---------------------------------------

void CodeEditor::update_autocomplete() {
  ac_open_ = false;
  size_t start = cursor_;
  while (start > line_start(line_of(cursor_)) && is_ident_char(text()[start - 1])) --start;
  if (cursor_ - start < size_t(kMinPrefix) || !is_ident_start(text()[start])) return;
  if (start > 0 && text()[start - 1] == '.') return;
  std::string prefix = text().substr(start, cursor_ - start);

  size_t h = indexed_hash_;
  if (h != idents_hash_) {
    idents_hash_ = h;
    idents_.clear();
    const std::string& s = text();
    for (size_t i = 0; i < s.size();) {
      if (is_ident_start(s[i]) && (i == 0 || !is_ident_char(s[i - 1]))) {
        size_t j = i;
        while (j < s.size() && is_ident_char(s[j])) ++j;
        if (j - i >= 3) idents_.emplace_back(s.substr(i, j - i));
        i = j;
      } else {
        ++i;
      }
    }
    std::sort(idents_.begin(), idents_.end());
    idents_.erase(std::unique(idents_.begin(), idents_.end()), idents_.end());
  }

  ac_items_.clear();
  auto consider = [&](std::string_view name) {
    if (name.size() <= prefix.size() || !name.starts_with(prefix)) return;
    if (std::find(ac_items_.begin(), ac_items_.end(), name) == ac_items_.end()) ac_items_.emplace_back(name);
  };
  // API names first: they're what a newcomer is most likely reaching for.
  for (const auto& d : api_docs()) consider(d.name);
  for (auto w : kLuaWords) consider(w);
  for (const auto& w : idents_) consider(w);
  if (ac_items_.empty()) return;
  ac_open_ = true;
  ac_sel_ = 0;
  ac_start_ = start;
  ac_browsed_ = false;
}

void CodeEditor::accept_autocomplete() {
  if (!ac_open_ || ac_items_.empty()) return;
  std::string item = ac_items_[size_t(ac_sel_)];
  ac_open_ = false;
  replace(ac_start_, cursor_, item, EditKind::kOther);
  // Calls get their opening paren, so the signature hint appears right away.
  if (find_api(item) && item[0] != '_' && (cursor_ >= text().size() || text()[cursor_] != '('))
    replace(cursor_, cursor_, "(", EditKind::kTyping);
}

void CodeEditor::find_next(bool backwards) {
  if (find_.empty()) return;
  const std::string& s = text();
  size_t found = std::string::npos;
  if (!backwards) {
    auto [a, b] = selection();
    found = find_in(s, find_, has_selection() ? a + 1 : cursor_, find_case_);
    if (found == std::string::npos) found = find_in(s, find_, 0, find_case_);
  } else {
    size_t limit = selection().first;
    for (size_t p = find_in(s, find_, 0, find_case_); p != std::string::npos && p < limit;
         p = find_in(s, find_, p + 1, find_case_))
      found = p;
    if (found == std::string::npos) {
      for (size_t p = find_in(s, find_, 0, find_case_); p != std::string::npos; p = find_in(s, find_, p + 1, find_case_))
        found = p;
    }
  }
  if (found == std::string::npos) {
    studio_.message("no matches for \"" + find_ + "\"", true);
    return;
  }
  tab_ = tab_of_line(line_of(found));
  anchor_ = found;
  cursor_ = found + find_.size();
  want_col_ = col_of(cursor_);
}

void CodeEditor::replace_one() {
  auto [a, b] = selection();
  std::string_view sel = std::string_view(text()).substr(a, b - a);
  if (has_selection() && find_in(sel, find_, 0, find_case_) == 0 && sel.size() == find_.size())
    replace(a, b, replace_with_, EditKind::kOther);
  find_next(false);
}

void CodeEditor::replace_all() {
  if (find_.empty()) return;
  begin_edit(EditKind::kOther);
  std::string& s = text();
  int n = 0;
  for (size_t p = find_in(s, find_, 0, find_case_); p != std::string::npos;
       p = find_in(s, find_, p + replace_with_.size(), find_case_)) {
    s.replace(p, find_.size(), replace_with_);
    ++n;
  }
  reindex();
  cursor_ = anchor_ = std::min(cursor_, s.size());
  if (n) studio_.mark_dirty();
  end_edit();
  studio_.message("replaced " + std::to_string(n) + (n == 1 ? " match" : " matches"));
}

bool CodeEditor::call_context(std::string& fn, int& arg) const {
  size_t start = line_start(line_of(cursor_));
  int depth = 0, commas = 0;
  for (size_t i = cursor_; i > start; --i) {
    char c = text()[i - 1];
    if (c == ')') {
      ++depth;
    } else if (c == '(') {
      if (depth == 0) {
        size_t e = i - 1, b = e;
        while (b > start && is_ident_char(text()[b - 1])) --b;
        if (b == e) return false;
        fn = text().substr(b, e - b);
        arg = commas;
        return find_api(fn) != nullptr;
      }
      --depth;
    } else if (c == ',' && depth == 0) {
      ++commas;
    }
  }
  return false;
}

// ---- input --------------------------------------------------------------------

void CodeEditor::handle_keys(ui::Ui& ui) {
  auto& in = ui.in();
  auto kr = [&](const char* k) { return in.keyr(key_from_name(k)); };
  auto kp = [&](const char* k) { return in.keyp(key_from_name(k)); };
  bool ctrl = in.ctrl(), shift = in.shift(), alt = in.alt();

  if (ctrl && kp("f")) {
    find_open_ = true;
    replace_open_ = false;
    find_focus_ = 1;
    if (has_selection()) {
      auto [a, b] = selection();
      if (line_of(a) == line_of(b)) find_ = text().substr(a, b - a);
    }
    return;
  }
  if (ctrl && kp("h")) {
    find_open_ = replace_open_ = true;
    find_focus_ = 1;
    return;
  }
  if (kp("f1")) {
    ref_open_ = !ref_open_;
    ref_filter_.clear();
    ref_sel_ = ref_scroll_ = 0;
    std::string fn;
    int arg = 0;
    if (ref_open_ && call_context(fn, arg)) {
      auto docs = api_docs();
      for (size_t i = 0; i < docs.size(); ++i)
        if (docs[i].name == fn) ref_sel_ = int(i);
    }
    return;
  }

  if (ref_open_) {
    auto docs = api_docs();
    std::vector<int> shown;
    for (size_t i = 0; i < docs.size(); ++i)
      if (ref_filter_.empty() || docs[i].name.find(ref_filter_) != std::string_view::npos ||
          docs[i].group.find(ref_filter_) != std::string_view::npos)
        shown.push_back(int(i));
    for (char c : in.text())
      if (uint8_t(c) > 32 && uint8_t(c) < 127) ref_filter_ += c;
    if (kr("backspace") && !ref_filter_.empty()) ref_filter_.pop_back();
    auto it = std::find(shown.begin(), shown.end(), ref_sel_);
    int idx = it == shown.end() ? 0 : int(it - shown.begin());
    if (kr("down")) ++idx;
    if (kr("up")) --idx;
    if (!shown.empty()) ref_sel_ = shown[size_t(std::clamp(idx, 0, int(shown.size()) - 1))];
    if (kp("escape")) ref_open_ = false;
    if (kp("return") && !shown.empty()) {
      ref_open_ = false;
      std::string name(docs[size_t(ref_sel_)].name);
      insert_text(name[0] == '_' ? name : name + "(", EditKind::kOther);
    }
    return;
  }

  if (find_open_ && find_focus_ > 0) {
    if (kp("escape")) {
      find_open_ = replace_open_ = false;
      find_focus_ = 0;
      return;
    }
    if (kp("tab") && replace_open_) find_focus_ = find_focus_ == 1 ? 2 : 1;
    if (kp("return")) {
      if (find_focus_ == 2) replace_one();
      else find_next(shift);
    }
    if (kp("down")) find_focus_ = 0;
    return;
  }

  if (ac_open_) {
    if (kr("down")) {
      ac_sel_ = (ac_sel_ + 1) % int(ac_items_.size());
      ac_browsed_ = true;
      return;
    }
    if (kr("up")) {
      ac_sel_ = (ac_sel_ + int(ac_items_.size()) - 1) % int(ac_items_.size());
      ac_browsed_ = true;
      return;
    }
    // Tab always accepts; Enter only once the list has been browsed, so a
    // newline right after a short word still just starts a new line.
    if (kp("tab") || (kp("return") && ac_browsed_)) {
      accept_autocomplete();
      return;
    }
    if (kp("escape")) {
      ac_open_ = false;
      return;
    }
  }
  if (kp("escape") && find_open_) {
    find_open_ = replace_open_ = false;
    return;
  }

  int line = line_of(cursor_);
  const Tab& t = tabs_[tab_];
  bool moved = false;
  auto go = [&](size_t pos) {
    end_edit();
    move_to(pos, shift);
    moved = true;
  };
  auto go_line = [&](int l) {
    l = std::clamp(l, t.first, std::max(t.first, t.last));
    end_edit();
    int col = want_col_;
    move_to(pos_at(l, col), shift);
    want_col_ = col;
    moved = true;
  };

  if (alt && (kr("up") || kr("down"))) {
    // Move the current line up or down.
    int dir = kr("up") ? -1 : 1;
    int other = line + dir;
    if (other >= t.first && other <= t.last) {
      int col = col_of(cursor_);
      std::string a(line_text(line)), b(line_text(other));
      size_t lo = line_start(std::min(line, other)), hi = line_end(std::max(line, other));
      std::string swapped = dir < 0 ? a + "\n" + b : b + "\n" + a;
      replace(lo, hi, swapped, EditKind::kOther);
      cursor_ = anchor_ = pos_at(other, col);
    }
    return;
  }

  if (kr("left")) {
    size_t p = has_selection() && !shift ? selection().first : (cursor_ > 0 ? cursor_ - 1 : 0);
    if (ctrl) {
      p = cursor_;
      while (p > 0 && !is_ident_char(text()[p - 1])) --p;
      while (p > 0 && is_ident_char(text()[p - 1])) --p;
    }
    go(p);
  }
  if (kr("right")) {
    size_t p = has_selection() && !shift ? selection().second : std::min(cursor_ + 1, text().size());
    if (ctrl) {
      p = cursor_;
      while (p < text().size() && !is_ident_char(text()[p])) ++p;
      while (p < text().size() && is_ident_char(text()[p])) ++p;
    }
    go(p);
  }
  if (kr("up")) go_line(line - 1);
  if (kr("down")) go_line(line + 1);
  if (kr("pageup")) go_line(line - rows_);
  if (kr("pagedown")) go_line(line + rows_);
  if (kp("home")) {
    if (ctrl) {
      go(line_start(t.first));
    } else {
      // Smart home: first press goes to the indentation, second to column 0.
      size_t ind = line_start(line) + size_t(indent_of(line));
      go(cursor_ == ind ? line_start(line) : ind);
    }
  }
  if (kp("end")) go(ctrl ? line_end(std::max(t.first, t.last)) : line_end(line));
  if (ctrl && kp("a")) {
    end_edit();
    anchor_ = line_start(t.first);
    cursor_ = line_end(std::max(t.first, t.last));
    moved = true;
  }
  if (moved) ac_open_ = false;

  if (ctrl && kp("d")) duplicate_line();
  if (ctrl && kp("/")) toggle_comment();
  if (ctrl && kp("pagedown")) select_tab((tab_ + 1) % int(tabs_.size()));
  if (ctrl && kp("pageup")) select_tab((tab_ + int(tabs_.size()) - 1) % int(tabs_.size()));
  if (ctrl) return;

  if (kr("backspace")) {
    if (has_selection()) {
      auto [a, b] = selection();
      replace(a, b, "", EditKind::kOther);
    } else if (cursor_ > line_start(t.first)) {
      // Backspace in leading whitespace removes a whole indent step.
      size_t start = line_start(line);
      int col = int(cursor_ - start);
      bool in_indent = col > 0 && col <= indent_of(line);
      size_t n = in_indent ? size_t((col - 1) % kIndent + 1) : 1;
      replace(cursor_ - n, cursor_, "", EditKind::kDeleting);
    }
    update_autocomplete();
  }
  if (kr("delete")) {
    if (has_selection()) {
      auto [a, b] = selection();
      replace(a, b, "", EditKind::kOther);
    } else if (cursor_ < line_end(std::max(t.first, t.last))) {
      replace(cursor_, cursor_ + 1, "", EditKind::kDeleting);
    }
  }
  if (kr("return")) newline();
  if (kp("tab")) {
    if (has_selection() || shift) indent_selection(shift);
    else insert_text(std::string(size_t(kIndent - col_of(cursor_) % kIndent), ' '), EditKind::kTyping);
  }

  std::string typed;
  for (char c : in.text())
    if (uint8_t(c) >= 32 && uint8_t(c) < 127) typed += c;
  if (!typed.empty()) {
    insert_text(typed, EditKind::kTyping);
    if (is_ident_char(typed.back())) {
      maybe_dedent();
      update_autocomplete();
    } else {
      ac_open_ = false;
    }
  }
}

void CodeEditor::handle_mouse(ui::Ui& ui, const ui::Rect& area, int gutter) {
  constexpr int kCaptureId = 0x7c0de;
  auto pos_from_mouse = [&]() {
    int row = std::clamp((ui.my() - area.y) / gfx::kCharH, 0, rows_ - 1);
    int l = std::min(scroll_ + row, std::max(tabs_[tab_].first, tabs_[tab_].last));
    int col = std::max(0, (ui.mx() - area.x - gutter + 2) / gfx::kCharW + hscroll_);
    return pos_at(l, col);
  };
  if (ui.hovered(area) && ui.wheel()) {
    scroll_ -= ui.wheel() * 3;
    const Tab& t = tabs_[tab_];
    scroll_ = std::clamp(scroll_, t.first, std::max(t.first, t.last - rows_ + 2));
  }
  if (ui.clicked_in(area)) {
    end_edit();
    ac_open_ = false;
    find_focus_ = 0;
    size_t p = pos_from_mouse();
    if (ui.double_clicked()) {
      size_t a = p, b = p;
      while (a > 0 && is_ident_char(text()[a - 1])) --a;
      while (b < text().size() && is_ident_char(text()[b])) ++b;
      anchor_ = a;
      cursor_ = b;
    } else if (ui.mx() < area.x + gutter) {
      int l = line_of(p);
      anchor_ = line_start(l);
      cursor_ = l + 1 < line_count() ? line_start(l + 1) : line_end(l);
    } else {
      move_to(p, ui.in().shift());
      ui.capture(kCaptureId);
    }
  }
  if (ui.captured(kCaptureId) && ui.mouse_down()) {
    cursor_ = pos_from_mouse();
    clamp_to_tab();
    if (ui.my() < area.y) --scroll_;
    if (ui.my() > area.bottom()) ++scroll_;
  }
}

void CodeEditor::ensure_visible(int rows, int cols) {
  int line = line_of(cursor_), col = col_of(cursor_);
  const Tab& t = tabs_[tab_];
  if (line < scroll_) scroll_ = line;
  if (line >= scroll_ + rows) scroll_ = line - rows + 1;
  scroll_ = std::clamp(scroll_, t.first, std::max(t.first, t.last));
  if (col < hscroll_) hscroll_ = std::max(0, col - 4);
  if (col >= hscroll_ + cols) hscroll_ = col - cols + 4;
}

// ---- drawing ------------------------------------------------------------------

void CodeEditor::draw_tabs(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  ui.fill(r, th.raised);
  int x = r.x + 1;
  for (int i = 0; i < int(tabs_.size()); ++i) {
    std::string label = tabs_[i].name;
    if (label.size() > 12) label = label.substr(0, 11) + ".";
    int w = gfx::Gfx::text_width(label) + 6;
    ui::Rect tr{x, r.y, w, r.h};
    bool sel = i == tab_;
    if (renaming_ == i) {
      if (ui.text_field({x, r.y, 52, r.h}, rename_buf_, true) || ui.in().keyp(key_from_name("escape"))) {
        bool commit = !ui.in().keyp(key_from_name("escape")) && !trim(rename_buf_).empty() && i > 0;
        if (commit) {
          int marker = tabs_[i].first - 1;
          replace(line_start(marker), line_end(marker), std::string(kTabMarker) + std::string(trim(rename_buf_)),
                  EditKind::kOther);
          select_tab(i);
        }
        renaming_ = -1;
      }
      x += 54;
      continue;
    }
    ui.fill(tr, sel ? th.panel : (ui.hovered(tr) ? th.hot : th.raised));
    ui.text(label, x + 3, r.y + 2, sel ? th.text : th.dim);
    if (ui.clicked_in(tr)) {
      if (sel && ui.double_clicked() && i > 0) {
        renaming_ = i;
        rename_buf_ = tabs_[i].name;
        ui.select_all_text();
      } else {
        select_tab(i);
      }
    }
    if (ui.hovered(tr) && i > 0) ui.tooltip("double-click to rename");
    x += w + 1;
  }
  if (x + 9 < r.right() && ui.icon_button(x, r.y - 1, ui::Icon::kPlus, false, "new tab")) {
    std::string name = "tab " + std::to_string(tabs_.size());
    size_t end = text().size();
    std::string add = (end && text().back() != '\n' ? "\n" : "") + std::string(kTabMarker) + name + "\n";
    replace(end, end, add, EditKind::kOther);
    select_tab(int(tabs_.size()) - 1);
  }
}

void CodeEditor::draw_text(ui::Ui& ui, const ui::Rect& area, int gutter, int rows, int cols) {
  const auto& th = ui.theme();
  auto& g = ui.g();
  ui.fill(area, th.bg);
  ui.fill({area.x, area.y, gutter - 2, area.h}, th.panel);
  const Tab& t = tabs_[tab_];
  auto [sa, sb] = selection();
  int cur_line = line_of(cursor_);
  std::vector<Tok> toks;

  for (int row = 0; row < rows; ++row) {
    int l = scroll_ + row;
    if (l > t.last || l >= line_count()) break;
    int y = area.y + row * gfx::kCharH;
    std::string_view s = line_text(l);
    size_t ls = line_start(l);

    if (l == error_line_) ui.fill({area.x + gutter - 1, y, area.w - gutter + 1, gfx::kCharH}, 9);
    else if (l == cur_line && !has_selection()) ui.fill({area.x + gutter - 1, y, area.w - gutter + 1, gfx::kCharH}, th.panel);

    std::string num = std::to_string(l + 1);
    g.print(num, area.x + gutter - 4 - int(num.size()) * gfx::kCharW, y, l == cur_line ? th.text : th.raised + 1);
    if (l == error_line_) g.print("!", area.x + 1, y, th.error);

    // Selection and search matches behind the text.
    for (int c = 0; c < cols; ++c) {
      size_t p = ls + size_t(hscroll_ + c);
      if (p > ls + s.size()) break;
      int x = area.x + gutter + c * gfx::kCharW - 1;
      bool selected = has_selection() && p >= sa && p < sb && p < ls + s.size() + 1;
      if (selected) g.rectfill(x, y, x + gfx::kCharW - 1, y + gfx::kCharH - 1, 3);
    }
    if (find_open_ && !find_.empty()) {
      for (size_t m = find_in(s, find_, 0, find_case_); m != std::string_view::npos; m = find_in(s, find_, m + 1, find_case_)) {
        int x0 = area.x + gutter + (int(m) - hscroll_) * gfx::kCharW - 1;
        int x1 = x0 + int(find_.size()) * gfx::kCharW - 1;
        g.rect(x0, y - 1, x1, y + gfx::kCharH - 1, 16);
      }
    }

    lex_line(s, lex_[size_t(l)], toks);
    for (int c = 0; c < cols; ++c) {
      int i = hscroll_ + c;
      if (i >= int(s.size())) break;
      char ch = s[size_t(i)];
      if (ch == ' ') continue;
      char glyph[2] = {ch, 0};
      g.print(glyph, area.x + gutter + c * gfx::kCharW, y, tok_color(toks[size_t(i)]));
    }
  }

  // Caret.
  if (cur_line >= scroll_ && cur_line < scroll_ + rows && ((blink_ / 20) % 2 == 0) && find_focus_ == 0 && !ref_open_) {
    int x = area.x + gutter + (col_of(cursor_) - hscroll_) * gfx::kCharW - 1;
    int y = area.y + (cur_line - scroll_) * gfx::kCharH;
    if (x >= area.x + gutter - 1) g.rectfill(x, y - 1, x, y + gfx::kCharH - 1, th.accent);
  }
}

void CodeEditor::draw_find_bar(ui::Ui& ui, const ui::Rect& r) {
  const auto& th = ui.theme();
  ui.fill(r, th.raised);
  ui.icon(ui::Icon::kSearch, r.x + 1, r.y + 1, th.dim);
  int fw = replace_open_ ? 84 : 140;
  ui::Rect ff{r.x + 11, r.y + 1, fw, 7};
  if (ui.clicked_in(ff)) find_focus_ = 1;
  ui.text_field(ff, find_, find_focus_ == 1);
  int x = ff.right() + 3;
  if (replace_open_) {
    ui.text("with", x, r.y + 2, th.dim);
    ui::Rect rf{x + 18, r.y + 1, 60, 7};
    if (ui.clicked_in(rf)) find_focus_ = 2;
    ui.text_field(rf, replace_with_, find_focus_ == 2);
    x = rf.right() + 3;
    if (ui.button({x, r.y + 1, 9, 7}, "1", false, "replace this match")) replace_one();
    if (ui.button({x + 10, r.y + 1, 13, 7}, "all", false, "replace every match")) replace_all();
    x += 25;
  }
  if (ui.button({x, r.y + 1, 13, 7}, "Aa", find_case_, "match case")) find_case_ = !find_case_;
  if (ui.icon_button(r.right() - 10, r.y - 1, ui::Icon::kClose, false, "close (esc)")) {
    find_open_ = replace_open_ = false;
    find_focus_ = 0;
  }
}

void CodeEditor::draw_autocomplete(ui::Ui& ui, const ui::Rect& area, int gutter) {
  if (!ac_open_ || ac_items_.empty()) return;
  const auto& th = ui.theme();
  int n = std::min(int(ac_items_.size()), kAutocompleteRows);
  int first = std::clamp(ac_sel_ - n + 1, 0, int(ac_items_.size()) - n);
  int w = 0;
  for (const auto& it : ac_items_) w = std::max(w, int(it.size()));
  w = w * gfx::kCharW + 6;
  int line = line_of(cursor_);
  int x = area.x + gutter + (int(ac_start_ - line_start(line)) - hscroll_) * gfx::kCharW - 3;
  int y = area.y + (line - scroll_ + 1) * gfx::kCharH + 1;
  int h = n * 7 + 2;
  if (y + h > area.bottom()) y = area.y + (line - scroll_) * gfx::kCharH - h - 1;
  x = std::clamp(x, area.x, area.right() - w);
  ui::Rect box{x, y, w, h};
  ui.begin_popup(box);
  ui.frame(box, th.raised);
  for (int i = 0; i < n; ++i) {
    int idx = first + i;
    ui::Rect row{x + 1, y + 1 + i * 7, w - 2, 7};
    if (idx == ac_sel_) ui.fill(row, th.accent);
    if (ui.hovered(row) && ui.clicked()) {
      ac_sel_ = idx;
      accept_autocomplete();
      ui.end_popup();
      return;
    }
    bool api = find_api(ac_items_[size_t(idx)]) != nullptr;
    ui.text(ac_items_[size_t(idx)], x + 3, row.y + 1, idx == ac_sel_ ? th.accent_text : (api ? 25 : th.text));
  }
  ui.end_popup();
}

void CodeEditor::draw_signature(ui::Ui& ui, const ui::Rect& area, int gutter) {
  hint_.clear();
  std::string fn;
  int arg = 0;
  if (ac_open_ || !call_context(fn, arg)) return;
  const ApiDoc* doc = find_api(fn);
  if (!doc) return;
  hint_ = std::string(doc->help);
  const auto& th = ui.theme();
  std::string_view sig = doc->signature;
  int w = int(sig.size()) * gfx::kCharW + 5;
  int line = line_of(cursor_);
  int y = area.y + (line - scroll_ - 1) * gfx::kCharH - 3;
  if (y < area.y) y = area.y + (line - scroll_ + 1) * gfx::kCharH + 1;
  int x = std::clamp(area.x + gutter + (col_of(cursor_) - hscroll_) * gfx::kCharW - w / 2, area.x, area.right() - w);
  ui.frame({x, y, w, 8}, th.raised);
  // Highlight the argument under the cursor.
  int depth = 0, current = 0;
  for (size_t i = 0; i < sig.size(); ++i) {
    char c = sig[i];
    int color = th.dim;
    if (c == '(' && depth++ == 0) color = th.text;
    else if (c == ')' && --depth == 0) color = th.text;
    else if (c == ',' && depth == 1) ++current;
    else if (depth >= 1 && current == arg) color = th.accent == 29 ? 30 : th.accent;
    else if (depth == 0) color = 25;
    char glyph[2] = {c, 0};
    ui.text(glyph, x + 3 + int(i) * gfx::kCharW, y + 2, color);
  }
}

void CodeEditor::draw_reference(ui::Ui& ui, const ui::Rect& body) {
  if (!ref_open_) return;
  const auto& th = ui.theme();
  ui::Rect box{body.x + 40, body.y + 4, body.w - 44, body.h - 8};
  ui.begin_popup(box);
  ui.frame(box, th.panel);
  ui.fill({box.x + 1, box.y + 1, box.w - 2, 8}, th.accent);
  ui.text("api reference", box.x + 4, box.y + 3, th.accent_text);
  ui.text("type to filter, enter inserts", box.x + 70, box.y + 3, th.accent_text);
  ui.text_field({box.x + 3, box.y + 11, box.w - 6, 8}, ref_filter_, false);
  if (ref_filter_.empty()) ui.text("filter...", box.x + 5, box.y + 13, th.raised + 1);

  auto docs = api_docs();
  std::vector<int> shown;
  for (size_t i = 0; i < docs.size(); ++i)
    if (ref_filter_.empty() || docs[i].name.find(ref_filter_) != std::string_view::npos ||
        docs[i].group.find(ref_filter_) != std::string_view::npos)
      shown.push_back(int(i));
  int list_y = box.y + 21, rows = (box.h - 21 - 16) / 6;
  auto it = std::find(shown.begin(), shown.end(), ref_sel_);
  int sel = it == shown.end() ? -1 : int(it - shown.begin());
  if (sel >= 0) {
    if (sel < ref_scroll_) ref_scroll_ = sel;
    if (sel >= ref_scroll_ + rows) ref_scroll_ = sel - rows + 1;
  }
  if (ui.hovered(box) && ui.wheel()) ref_scroll_ = std::clamp(ref_scroll_ - ui.wheel() * 2, 0, std::max(0, int(shown.size()) - rows));
  std::string_view last_group;
  for (int r = 0; r < rows && ref_scroll_ + r < int(shown.size()); ++r) {
    const ApiDoc& d = docs[size_t(shown[size_t(ref_scroll_ + r)])];
    int y = list_y + r * 6;
    ui::Rect row{box.x + 2, y, box.w - 4, 6};
    bool selected = shown[size_t(ref_scroll_ + r)] == ref_sel_;
    if (selected) ui.fill(row, th.raised);
    if (ui.clicked_in(row)) {
      ref_sel_ = shown[size_t(ref_scroll_ + r)];
      if (ui.double_clicked()) {
        ref_open_ = false;
        std::string name(d.name);
        insert_text(name[0] == '_' ? name : name + "(", EditKind::kOther);
        ui.end_popup();
        return;
      }
    }
    if (d.group != last_group) ui.text(d.group, box.x + 4, y, th.raised + 1);
    last_group = d.group;
    ui.text(d.name, box.x + 40, y, selected ? th.text : 25);
  }
  if (sel >= 0) {
    const ApiDoc& d = docs[size_t(ref_sel_)];
    int y = box.bottom() - 14;
    ui.fill({box.x + 1, y - 1, box.w - 2, 14}, th.raised);
    std::string sig(d.signature);
    if (int(sig.size()) * gfx::kCharW > box.w - 8) sig = sig.substr(0, size_t((box.w - 8) / gfx::kCharW - 1)) + ".";
    ui.text(sig, box.x + 4, y + 1, th.text);
    ui.text(d.help, box.x + 4, y + 7, th.dim);
  }
  ui.end_popup();
}

void CodeEditor::update(ui::Ui& ui, const ui::Rect& body) {
  reindex();
  ++blink_;
  if (history_.open() && ++idle_ > kIdleCommitFrames) end_edit();

  ui::Rect tabs{body.x, body.y, body.w, kTabStripH};
  int bar = find_open_ ? kFindBarH : 0;
  ui::Rect area{body.x, body.y + kTabStripH, body.w, body.h - kTabStripH - bar};
  int digits = int(std::to_string(line_count()).size());
  int gutter = std::max(3, digits) * gfx::kCharW + 6;
  rows_ = area.h / gfx::kCharH;
  cols_ = (area.w - gutter - 2) / gfx::kCharW;

  if (renaming_ < 0) handle_keys(ui);
  if (!ref_open_) handle_mouse(ui, area, gutter);
  reindex();
  clamp_to_tab();
  if (!ui.captured(0x7c0de)) ensure_visible(rows_, cols_);

  draw_tabs(ui, tabs);
  draw_text(ui, area, gutter, rows_, cols_);
  if (find_open_) draw_find_bar(ui, {body.x, area.bottom() + 1, body.w, kFindBarH});
  draw_signature(ui, area, gutter);
  draw_autocomplete(ui, area, gutter);
  draw_reference(ui, body);
}

std::string CodeEditor::status() const {
  if (error_line_ >= 0 && !error_msg_.empty()) return "line " + std::to_string(error_line_ + 1) + ": " + error_msg_;
  auto [l, c] = cursor_pos();
  std::string s = "ln " + std::to_string(l + 1) + " col " + std::to_string(c + 1);
  if (has_selection()) s += "  (" + std::to_string(selection().second - selection().first) + " selected)";
  if (!hint_.empty()) s += "  " + hint_;
  else s += "  f1: api reference";
  return s;
}

}  // namespace glim::editors
