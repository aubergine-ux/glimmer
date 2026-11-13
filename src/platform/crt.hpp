#pragma once

#include <cstdint>
#include <vector>

#include "core/memory_map.hpp"

namespace glim::platform {

// CPU CRT filter: barrel curvature, scanlines, a soft aperture mask, bloom
// and vignette. Done on the CPU so it behaves identically on desktop and web
// without per-backend shaders.
class Crt {
 public:
  static constexpr int kScale = 4;
  static constexpr int kW = kScreenW * kScale;
  static constexpr int kH = kScreenH * kScale;

  Crt();
  void apply(const uint32_t* src, uint32_t* dst) const;

 private:
  struct Tap {
    int32_t index;   // source pixel, -1 outside the curved screen
    uint16_t frac_y; // position within the source row, 0-255
    uint16_t shade;  // vignette, 0-256
  };
  std::vector<Tap> taps_;
  std::vector<uint16_t> mask_;
};

}  // namespace glim::platform
