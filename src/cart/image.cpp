#include "cart/image.hpp"

#include <fstream>
#include <iterator>

#include <miniz.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace glim::cart {

std::vector<uint8_t> encode_png(const Image& img) {
  size_t len = 0;
  void* png = tdefl_write_image_to_png_file_in_memory_ex(img.rgba.data(), img.w, img.h, 4, &len,
                                                          MZ_BEST_COMPRESSION, MZ_FALSE);
  if (!png) return {};
  std::vector<uint8_t> out(static_cast<uint8_t*>(png), static_cast<uint8_t*>(png) + len);
  mz_free(png);
  return out;
}

bool decode_png(std::span<const uint8_t> data, Image& out) {
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &comp, 4);
  if (!px) return false;
  out = Image(w, h);
  std::copy(px, px + size_t(w) * h * 4, out.rgba.begin());
  stbi_image_free(px);
  return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool write_file(const std::string& path, std::span<const uint8_t> data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
  return bool(f);
}

}  // namespace glim::cart
