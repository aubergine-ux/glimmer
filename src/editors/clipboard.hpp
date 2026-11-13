#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace glim::editors {

// Clipboard payloads are plain text with a "glimmer <kind>" header line, so
// copies survive the system clipboard and paste between carts, between
// Glimmer windows, and even through a chat message.

struct PixelClip {
  int w = 0, h = 0;
  std::vector<uint8_t> px;  // color indices, row-major
};

struct CellClip {
  int w = 0, h = 0;
  std::vector<uint16_t> cells;
};

std::string encode_pixels(const PixelClip& clip);
bool decode_pixels(std::string_view text, PixelClip& out);

std::string encode_cells(const CellClip& clip);
bool decode_cells(std::string_view text, CellClip& out);

// Raw bytes for a named kind ("sfx", "pattern", "wave"...).
std::string encode_bytes(std::string_view kind, const std::vector<uint8_t>& bytes);
bool decode_bytes(std::string_view text, std::string_view kind, std::vector<uint8_t>& out);

}  // namespace glim::editors
