#include "gfx/font.hpp"

#include <array>
#include <string_view>

namespace glim::gfx {

namespace {

// One glyph per line: the character, then five rows of three pixels. Glyphs
// with descenders (g j p q y , ;) give a sixth row, which sits in the gap
// between lines. Codes 128+ are console symbols: arrows, heart, star, note.
constexpr std::string_view kFontSource = R"(
  ... ... ... ... ...
! .#. .#. .#. ... .#.
" #.# #.# ... ... ...
# #.# ### #.# ### #.#
$ .## ##. .#. .## ##.
% #.# ..# .#. #.. #.#
& .#. #.# .## #.# .##
' .#. .#. ... ... ...
( ..# .#. .#. .#. ..#
) #.. .#. .#. .#. #..
* #.# .#. ### .#. #.#
+ ... .#. ### .#. ...
, ... ... ... ... .#. #..
- ... ... ### ... ...
. ... ... ... ... .#.
/ ..# ..# .#. #.. #..
0 ### #.# #.# #.# ###
1 .#. ##. .#. .#. ###
2 ### ..# ### #.. ###
3 ### ..# .## ..# ###
4 #.# #.# ### ..# ..#
5 ### #.. ### ..# ###
6 #.. #.. ### #.# ###
7 ### ..# ..# ..# ..#
8 ### #.# ### #.# ###
9 ### #.# ### ..# ..#
: ... .#. ... .#. ...
; ... .#. ... ... .#. #..
< ..# .#. #.. .#. ..#
= ... ### ... ### ...
> #.. .#. ..# .#. #..
? ### ..# .## ... .#.
@ .#. #.# #.# #.. .##
A .#. #.# ### #.# #.#
B ##. #.# ##. #.# ##.
C .## #.. #.. #.. .##
D ##. #.# #.# #.# ##.
E ### #.. ##. #.. ###
F ### #.. ##. #.. #..
G .## #.. #.# #.# .##
H #.# #.# ### #.# #.#
I ### .#. .#. .#. ###
J ..# ..# ..# #.# .#.
K #.# #.# ##. #.# #.#
L #.. #.. #.. #.. ###
M #.# ### ### #.# #.#
N ##. #.# #.# #.# #.#
O .#. #.# #.# #.# .#.
P ##. #.# ##. #.. #..
Q .#. #.# #.# ##. .##
R ##. #.# ##. #.# #.#
S .## #.. .#. ..# ##.
T ### .#. .#. .#. .#.
U #.# #.# #.# #.# .##
V #.# #.# #.# .#. .#.
W #.# #.# ### ### #.#
X #.# #.# .#. #.# #.#
Y #.# #.# .#. .#. .#.
Z ### ..# .#. #.. ###
[ ##. #.. #.. #.. ##.
\ #.. #.. .#. ..# ..#
] .## ..# ..# ..# .##
^ .#. #.# ... ... ...
_ ... ... ... ... ###
` #.. .#. ... ... ...
a ... .## #.# #.# .##
b #.. ##. #.# #.# ##.
c ... .## #.. #.. .##
d ..# .## #.# #.# .##
e ... .#. ### #.. .##
f .## #.. ##. #.. #..
g ... .## #.# #.# .## ##.
h #.. ##. #.# #.# #.#
i .#. ... .#. .#. .#.
j ..# ... ..# ..# ..# ##.
k #.. #.# ##. #.# #.#
l ##. .#. .#. .#. .##
m ... ### ### #.# #.#
n ... ##. #.# #.# #.#
o ... .#. #.# #.# .#.
p ... ##. #.# #.# ##. #..
q ... .## #.# #.# .## ..#
r ... .## #.. #.. #..
s ... .## ##. ..# ##.
t .#. ### .#. .#. ..#
u ... #.# #.# #.# .##
v ... #.# #.# ### .#.
w ... #.# #.# ### ###
x ... ... #.# .#. #.#
y ... #.# #.# #.# .## ##.
z ... ### .## #.. ###
{ .## .#. ##. .#. .##
| .#. .#. .#. .#. .#.
} ##. .#. .## .#. ##.
~ ... ..# ### #.. ...
)";

// Symbols use explicit codes since they have no ASCII spelling.
constexpr std::string_view kSymbolSource = R"(
128 .#. ##. ### ##. .#.
129 .#. .## ### .## .#.
130 .#. ### ### .#. .#.
131 .#. .#. ### ### .#.
132 ### ### ### ### ###
133 #.# ### ### .#. ...
134 .#. ### .#. #.# ...
135 .## .#. .#. ##. ##.
136 ... .#. ### .#. ...
137 ### #.# #.# #.# ###
)";

constexpr uint8_t parse_row(std::string_view s) {
  uint8_t bits = 0;
  for (char ch : s) bits = uint8_t((bits << 1) | (ch == '#'));
  return bits;
}

struct FontTable {
  std::array<std::array<uint8_t, kGlyphRows>, 256> rows{};
  std::array<bool, 256> present{};

  constexpr void add(int code, std::string_view line) {
    for (int r = 0; r < kGlyphRows && size_t(r * 4 + 3) <= line.size(); ++r)
      rows[code][r] = parse_row(line.substr(r * 4, 3));
    present[code] = true;
  }

  constexpr FontTable() {
    std::string_view src = kFontSource;
    while (!src.empty()) {
      auto nl = src.find('\n');
      auto line = src.substr(0, nl);
      if (line.size() >= 21) add(uint8_t(line[0]), line.substr(2));
      if (nl == std::string_view::npos) break;
      src.remove_prefix(nl + 1);
    }
    src = kSymbolSource;
    while (!src.empty()) {
      auto nl = src.find('\n');
      auto line = src.substr(0, nl);
      if (line.size() >= 23) {
        int code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
        add(code, line.substr(4));
      }
      if (nl == std::string_view::npos) break;
      src.remove_prefix(nl + 1);
    }
  }
};

constexpr FontTable kFont{};

static_assert(kFont.present['A'] && kFont.present['~'] && kFont.present[' ']);

}  // namespace

uint8_t glyph_row(uint8_t c, int row) {
  if (row < 0 || row >= kGlyphRows) return 0;
  if (kFont.present[c]) return kFont.rows[c][row];
  if (row >= kGlyphH) return 0;
  return (row == 0 || row == kGlyphH - 1) ? 0b111 : 0b101;
}

}  // namespace glim::gfx
