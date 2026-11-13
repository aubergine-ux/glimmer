#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace glim::gfx {

inline constexpr int kGlyphW = 3;
inline constexpr int kGlyphH = 5;     // cap height
inline constexpr int kGlyphRows = 6;  // including the descender row
inline constexpr int kCharW = 4;  // advance including spacing
inline constexpr int kCharH = 6;  // line height

// Row bitmask for glyph `c`, row 0 at the top. Bit 2 is the leftmost pixel.
// Characters without a glyph render as a hollow box.
uint8_t glyph_row(uint8_t c, int row);

// Text is UTF-8: ASCII maps to itself, the Albanian letters ë ç Ë Ç map to
// glyphs 138-141, and other characters show as '?'. Bytes that aren't valid
// UTF-8 pass through unchanged, so carts can still print glyph codes directly.
std::vector<uint8_t> glyphs(std::string_view text);

}  // namespace glim::gfx
