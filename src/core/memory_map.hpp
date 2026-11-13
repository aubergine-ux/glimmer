#pragma once

// Addresses of every region in console RAM. These values are part of the
// public spec (SPEC.md, "Memory map") and must never move once released.

#include <cstdint>

namespace glim {

inline constexpr int kScreenW = 256;
inline constexpr int kScreenH = 144;
inline constexpr int kColors = 32;
inline constexpr int kFps = 60;

inline constexpr int kSheetW = 128;           // pixels
inline constexpr int kSheetH = 128;
inline constexpr int kSheets = 2;
inline constexpr int kSprites = 256 * kSheets;
inline constexpr int kMapW = 128;              // cells
inline constexpr int kMapH = 64;
inline constexpr int kMapLayers = 4;

inline constexpr int kSfxCount = 64;
inline constexpr int kSfxSteps = 32;
inline constexpr int kSfxBytes = 4 + kSfxSteps * 2;
inline constexpr int kWavetables = 8;
inline constexpr int kWaveSamples = 32;
inline constexpr int kPatterns = 128;
inline constexpr int kChannels = 4;
inline constexpr int kPlayers = 4;

namespace addr {

inline constexpr uint32_t kGfx = 0x00000;         // 128x256 px, sheet 1 starts at row 128
inline constexpr uint32_t kGfxSize = kSheetW * kSheetH * kSheets;
inline constexpr uint32_t kMap = 0x08000;         // 4 layers, 2 bytes per cell
inline constexpr uint32_t kMapLayerSize = kMapW * kMapH * 2;
inline constexpr uint32_t kMapSize = kMapLayerSize * kMapLayers;
inline constexpr uint32_t kFlags = 0x18000;       // 1 byte per sprite
inline constexpr uint32_t kPalette = 0x18200;     // 32 x RGB
inline constexpr uint32_t kPaletteSize = kColors * 3;

inline constexpr uint32_t kDrawPal = 0x18260;     // 32 bytes, source -> drawn color
inline constexpr uint32_t kScreenPal = 0x18280;   // 32 bytes, screen -> displayed color
inline constexpr uint32_t kTransparent = 0x182A0; // 32-bit mask, bit n = color n skipped by spr/map
inline constexpr uint32_t kClip = 0x182A4;        // int16 x0, y0, x1, y1 (exclusive)
inline constexpr uint32_t kCamera = 0x182AC;      // int16 x, y
inline constexpr uint32_t kPen = 0x182B0;         // u8 current color
inline constexpr uint32_t kFillPattern = 0x182B1; // u16 4x4 pattern
inline constexpr uint32_t kFillAlt = 0x182B3;     // u8 color for set bits, 0xFF = transparent
inline constexpr uint32_t kCursor = 0x182B4;      // int16 x, y for print without coordinates

inline constexpr uint32_t kSfx = 0x18400;
inline constexpr uint32_t kSfxSize = kSfxCount * kSfxBytes;
inline constexpr uint32_t kWave = 0x19600;
inline constexpr uint32_t kWaveSize = kWavetables * kWaveSamples;
inline constexpr uint32_t kMusic = 0x19700;
inline constexpr uint32_t kMusicSize = kPatterns * kChannels;
inline constexpr uint32_t kPersist = 0x19900;
inline constexpr uint32_t kPersistSize = 256;

inline constexpr uint32_t kButtons = 0x19A00;     // u16 per player
inline constexpr uint32_t kMouse = 0x19A08;       // int16 x, int16 y, u8 buttons, int8 wheel
inline constexpr uint32_t kChanSfx = 0x19A10;     // int8 per channel, -1 when idle
inline constexpr uint32_t kChanStep = 0x19A14;    // u8 per channel
inline constexpr uint32_t kMusicPattern = 0x19A18;// int8, -1 when stopped

inline constexpr uint32_t kScreen = 0x1A000;      // 1 byte per pixel
inline constexpr uint32_t kScreenSize = kScreenW * kScreenH;
inline constexpr uint32_t kUser = 0x23000;
inline constexpr uint32_t kRamSize = 0x40000;

static_assert(kGfx + kGfxSize <= kMap);
static_assert(kMap + kMapSize <= kFlags);
static_assert(kSfx + kSfxSize <= kWave);
static_assert(kScreen + kScreenSize <= kUser);

}  // namespace addr
}  // namespace glim
