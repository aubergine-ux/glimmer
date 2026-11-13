#pragma once

#include <cstdint>
#include <vector>

namespace glim::cart {

// Streaming animated GIF encoder for console frames. Frames come from a
// 32-color palette, so each frame gets a 32-entry local color table and 5-bit
// LZW, which keeps recordings small.
class GifWriter {
 public:
  GifWriter(int width, int height, int scale = 1);

  // `rgb` is width*height 0x00RRGGBB pixels; delay is in hundredths of a second.
  void add_frame(const uint32_t* rgb, int delay_cs);
  std::vector<uint8_t> finish();
  int frames() const { return frames_; }

 private:
  int w_, h_, scale_;
  int frames_ = 0;
  std::vector<uint8_t> out_;
};

}  // namespace glim::cart
