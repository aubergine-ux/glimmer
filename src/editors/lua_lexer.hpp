#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace glim::editors {

enum class Tok : uint8_t { kText, kKeyword, kApi, kCallback, kConst, kString, kNumber, kComment, kOp };

// State carried from one line to the next: whether we're inside a long
// string or comment, and the level of its brackets ([[ ]] is level 0).
struct LexState {
  enum Mode : uint8_t { kNormal, kLongString, kLongComment } mode = kNormal;
  int level = 0;
  bool operator==(const LexState&) const = default;
};

// Classifies each byte of `line`, writing one Tok per byte to `out`, and
// returns the state at the end of the line.
LexState lex_line(std::string_view line, LexState state, std::vector<Tok>& out);

bool is_ident_start(char c);
bool is_ident_char(char c);

}  // namespace glim::editors
