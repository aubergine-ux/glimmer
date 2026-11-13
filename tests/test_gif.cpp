#include <doctest.h>

#include <stb_image.h>

#include <vector>

#include "cart/gif.hpp"

using namespace glim::cart;

TEST_CASE("gif frames decode back to the exact pixels") {
  const int w = 37, h = 11;
  std::vector<uint32_t> a(w * h), b(w * h);
  uint32_t seed = 7;
  for (int i = 0; i < w * h; ++i) {
    seed = seed * 1664525u + 1013904223u;
    a[i] = (seed >> 27) * 0x070503u;  // up to 32 distinct colors
    b[i] = (i % 3) ? 0xFF6FB5 : 0x1F1A33;
  }
  GifWriter gif(w, h, 2);
  gif.add_frame(a.data(), 3);
  gif.add_frame(b.data(), 4);
  auto data = gif.finish();

  int* delays = nullptr;
  int gw = 0, gh = 0, frames = 0, comp = 0;
  stbi_uc* px = stbi_load_gif_from_memory(data.data(), int(data.size()), &delays, &gw, &gh, &frames, &comp, 4);
  REQUIRE(px != nullptr);
  CHECK(gw == w * 2);
  CHECK(gh == h * 2);
  REQUIRE(frames == 2);
  CHECK(delays[0] == 30);  // stb reports milliseconds
  CHECK(delays[1] == 40);
  for (int f = 0; f < 2; ++f) {
    const auto& src = f == 0 ? a : b;
    const stbi_uc* frame = px + size_t(f) * gw * gh * 4;
    for (int y = 0; y < gh; ++y) {
      for (int x = 0; x < gw; ++x) {
        uint32_t want = src[(y / 2) * w + x / 2];
        const stbi_uc* p = frame + (size_t(y) * gw + x) * 4;
        REQUIRE(p[0] == uint8_t(want >> 16));
        REQUIRE(p[1] == uint8_t(want >> 8));
        REQUIRE(p[2] == uint8_t(want));
      }
    }
  }
  stbi_image_free(px);
  stbi_image_free(delays);
}

TEST_CASE("long recordings survive LZW dictionary resets") {
  const int w = 256, h = 144;
  std::vector<uint32_t> noise(w * h);
  uint32_t seed = 1;
  for (auto& p : noise) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    p = (seed & 31) * 0x050708u;
  }
  GifWriter gif(w, h);
  gif.add_frame(noise.data(), 2);
  auto data = gif.finish();
  int gw = 0, gh = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(data.data(), int(data.size()), &gw, &gh, &comp, 4);
  REQUIRE(px != nullptr);
  bool same = true;
  for (int i = 0; i < w * h && same; ++i) same = px[i * 4] == uint8_t(noise[i] >> 16) && px[i * 4 + 2] == uint8_t(noise[i]);
  CHECK(same);
  stbi_image_free(px);
}
