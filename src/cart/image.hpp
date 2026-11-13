#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace glim::cart {

// Minimal RGBA8 image used for PNG import/export, screenshots and test goldens.
struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;

  Image() = default;
  Image(int width, int height) : w(width), h(height), rgba(size_t(width) * height * 4, 0) {}
  uint8_t* at(int x, int y) { return rgba.data() + (size_t(y) * w + x) * 4; }
  const uint8_t* at(int x, int y) const { return rgba.data() + (size_t(y) * w + x) * 4; }
};

std::vector<uint8_t> encode_png(const Image& img);
bool decode_png(std::span<const uint8_t> data, Image& out);

bool read_file(const std::string& path, std::vector<uint8_t>& out);
bool write_file(const std::string& path, std::span<const uint8_t> data);

}  // namespace glim::cart
