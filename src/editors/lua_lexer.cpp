#include "editors/lua_lexer.hpp"

#include <array>

#include "editors/api_docs.hpp"

namespace glim::editors {

namespace {

constexpr std::array<std::string_view, 19> kKeywords = {
    "and", "break", "do", "else", "elseif", "end", "for", "function", "goto", "if",
    "in", "local", "not", "or", "repeat", "return", "then", "until", "while"};
constexpr std::array<std::string_view, 4> kConsts = {"nil", "true", "false", "self"};

template <size_t N>
bool contains(const std::array<std::string_view, N>& words, std::string_view w) {
  for (auto k : words)
    if (k == w) return true;
  return false;
}

// At `i`, an opening long bracket "[", "=" * level, "["? Returns level or -1.
int long_bracket_open(std::string_view s, size_t i) {
  if (i >= s.size() || s[i] != '[') return -1;
  size_t j = i + 1;
  int level = 0;
  while (j < s.size() && s[j] == '=') {
    ++level;
    ++j;
  }
  return (j < s.size() && s[j] == '[') ? level : -1;
}

// Finds the matching "]", "=" * level, "]" from `i`; returns the index after it or npos.
size_t long_bracket_close(std::string_view s, size_t i, int level) {
  for (; i < s.size(); ++i) {
    if (s[i] != ']') continue;
    size_t j = i + 1;
    int n = 0;
    while (j < s.size() && s[j] == '=') {
      ++n;
      ++j;
    }
    if (n == level && j < s.size() && s[j] == ']') return j + 1;
  }
  return std::string_view::npos;
}

}  // namespace

bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

LexState lex_line(std::string_view s, LexState state, std::vector<Tok>& out) {
  out.assign(s.size(), Tok::kText);
  size_t i = 0;
  auto fill = [&](size_t from, size_t to, Tok t) {
    for (size_t k = from; k < to && k < s.size(); ++k) out[k] = t;
  };

  if (state.mode != LexState::kNormal) {
    Tok t = state.mode == LexState::kLongString ? Tok::kString : Tok::kComment;
    size_t end = long_bracket_close(s, 0, state.level);
    if (end == std::string_view::npos) {
      fill(0, s.size(), t);
      return state;
    }
    fill(0, end, t);
    i = end;
    state = {};
  }

  while (i < s.size()) {
    char c = s[i];
    if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
      int level = long_bracket_open(s, i + 2);
      if (level >= 0) {
        size_t end = long_bracket_close(s, i + 4 + level, level);
        if (end == std::string_view::npos) {
          fill(i, s.size(), Tok::kComment);
          return {LexState::kLongComment, level};
        }
        fill(i, end, Tok::kComment);
        i = end;
        continue;
      }
      fill(i, s.size(), Tok::kComment);
      return state;
    }
    if (int level = long_bracket_open(s, i); level >= 0) {
      size_t end = long_bracket_close(s, i + 2 + level, level);
      if (end == std::string_view::npos) {
        fill(i, s.size(), Tok::kString);
        return {LexState::kLongString, level};
      }
      fill(i, end, Tok::kString);
      i = end;
      continue;
    }
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < s.size() && s[j] != c) j += (s[j] == '\\') ? 2 : 1;
      size_t end = std::min(j + 1, s.size());
      fill(i, end, Tok::kString);
      i = end;
      continue;
    }
    if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')) {
      size_t j = i;
      bool hex = c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X');
      if (hex) j += 2;
      while (j < s.size()) {
        char d = s[j];
        bool ok = (d >= '0' && d <= '9') || d == '.' || (hex && ((d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))) ||
                  (!hex && (d == 'e' || d == 'E')) || ((d == '-' || d == '+') && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E'));
        if (!ok) break;
        ++j;
      }
      fill(i, j, Tok::kNumber);
      i = j;
      continue;
    }
    if (is_ident_start(c)) {
      size_t j = i;
      while (j < s.size() && is_ident_char(s[j])) ++j;
      std::string_view w = s.substr(i, j - i);
      Tok t = Tok::kText;
      // Field access like obj.spr isn't the API function.
      bool member = i > 0 && (s[i - 1] == '.' || s[i - 1] == ':') && !(i > 1 && s[i - 2] == '.');
      if (contains(kKeywords, w)) t = Tok::kKeyword;
      else if (contains(kConsts, w)) t = Tok::kConst;
      else if (!member && (w == "_init" || w == "_update" || w == "_draw")) t = Tok::kCallback;
      else if (!member && find_api(w)) t = Tok::kApi;
      fill(i, j, t);
      i = j;
      continue;
    }
    if (c != ' ' && c != '\t') out[i] = Tok::kOp;
    ++i;
  }
  return state;
}

}  // namespace glim::editors
