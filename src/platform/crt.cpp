#include "platform/crt.hpp"

#include <algorithm>
#include <cmath>

namespace glim::platform {

namespace {
constexpr double kCurvature = 0.035;
constexpr double kVignette = 0.25;
constexpr int kScanlineFloor = 150;  // brightness at the gap between scanlines, of 256
constexpr int kBloom = 48;           // neighbour bleed, of 256

inline uint32_t channel(uint32_t c, int shift) { return (c >> shift) & 0xFF; }
}  // namespace

Crt::Crt() : taps_(size_t(kW) * kH), mask_(kW) {
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      double nx = (x + 0.5) / kW * 2.0 - 1.0, ny = (y + 0.5) / kH * 2.0 - 1.0;
      double r2 = nx * nx + ny * ny;
      double cx = nx * (1.0 + kCurvature * r2), cy = ny * (1.0 + kCurvature * r2);
      Tap& t = taps_[size_t(y) * kW + x];
      if (std::abs(cx) >= 1.0 || std::abs(cy) >= 1.0) {
        t = {-1, 0, 0};
        continue;
      }
      double sx = (cx + 1.0) * 0.5 * kScreenW, sy = (cy + 1.0) * 0.5 * kScreenH;
      int ix = std::clamp(int(sx), 0, kScreenW - 1), iy = std::clamp(int(sy), 0, kScreenH - 1);
      double edge = std::max(std::abs(cx), std::abs(cy));
      double v = 1.0 - kVignette * std::pow(edge, 4.0);
      t.index = iy * kScreenW + ix;
      t.frac_y = uint16_t(std::clamp((sy - iy) * 256.0, 0.0, 255.0));
      t.shade = uint16_t(std::clamp(v * 256.0, 0.0, 256.0));
    }
  }
  // A light RGB triad; strong masks look great in stills but darken the image.
  for (int x = 0; x < kW; ++x) mask_[x] = x % 3;
}

void Crt::apply(const uint32_t* src, uint32_t* dst) const {
  for (int y = 0; y < kH; ++y) {
    const Tap* row = &taps_[size_t(y) * kW];
    uint32_t* out = dst + size_t(y) * kW;
    for (int x = 0; x < kW; ++x) {
      const Tap& t = row[x];
      if (t.index < 0) {
        out[x] = 0;
        continue;
      }
      int sx = t.index % kScreenW;
      uint32_t c = src[t.index];
      uint32_t l = src[t.index - (sx > 0)];
      uint32_t r = src[t.index + (sx < kScreenW - 1)];
      // Scanline profile: bright in the middle of each source row.
      int d = std::abs(int(t.frac_y) - 128);
      int scan = 256 - (256 - kScanlineFloor) * d * d / (128 * 128);
      int weight = scan * t.shade >> 8;
      uint32_t px = 0;
      for (int shift = 0; shift <= 16; shift += 8) {
        int v = int(channel(c, shift)) * (256 - kBloom) + int((channel(l, shift) + channel(r, shift)) * kBloom / 2);
        v >>= 8;
        // Mask channel order: shift 16 is red, 8 green, 0 blue.
        int lane = shift == 16 ? 0 : (shift == 8 ? 1 : 2);
        int mask = mask_[x] == lane ? 272 : 236;
        v = v * weight >> 8;
        v = v * mask >> 8;
        px |= uint32_t(std::min(v, 255)) << shift;
      }
      out[x] = px;
    }
  }
}

}  // namespace glim::platform
