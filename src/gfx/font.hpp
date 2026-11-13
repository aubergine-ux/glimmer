#pragma once

#include <cstdint>

namespace glim::gfx {

inline constexpr int kGlyphW = 3;
inline constexpr int kGlyphH = 5;
inline constexpr int kCharW = 4;  // advance including spacing
inline constexpr int kCharH = 6;  // line height

// Row bitmask for glyph `c`, row 0 at the top. Bit 2 is the leftmost pixel.
// Characters without a glyph render as a hollow box.
uint8_t glyph_row(uint8_t c, int row);

}  // namespace glim::gfx
