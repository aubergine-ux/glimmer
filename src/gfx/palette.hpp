#pragma once

#include <array>
#include <cstdint>

namespace glim::gfx {

// "Dusk 32": the default palette. Ramps run dark to light within each hue so
// palette swaps along a ramp give cheap shading and fades.
inline constexpr std::array<uint32_t, 32> kDefaultPalette = {
    0x0b0a12, 0x1f1a33, 0x3a2f5b, 0x6b4f9e, 0xa77bd8, 0xf2e9ff, 0x8a8fa8, 0x4a4e69,
    0x2b2d42, 0x5c1f2e, 0xa32d3c, 0xe8534a, 0xff8c69, 0xffc2a1, 0x7a3e1d, 0xc06a2b,
    0xf39c34, 0xffd166, 0xfff4a3, 0x1e4d2b, 0x2f8a3e, 0x6cc551, 0xc2f07a, 0x133b5c,
    0x1f6fb2, 0x3fa7f0, 0x8fdcff, 0x1d6b6b, 0x36c2a6, 0xb03f86, 0xff6fb5, 0xffffff,
};

}  // namespace glim::gfx
