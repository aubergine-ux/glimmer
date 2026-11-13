#pragma once

#include <cstdint>

namespace glim::ui {

enum class Icon : uint8_t {
  kCode, kSprite, kMap, kSfx, kMusic,
  kPlay, kStop, kPencil, kFill, kLine, kRect, kCircle, kSelect, kPicker, kHand,
  kFlipH, kFlipV, kRotate, kUndo, kRedo, kEye, kEyeOff, kOnion, kLoop, kAutoTile,
  kWave, kPiano, kSearch, kHelp, kPlus, kMinus, kClose, kSave, kGrid, kCursor,
  kCount
};

// 8x8 bitmask rows for an icon, bit 7 = leftmost pixel.
const uint8_t* icon_rows(Icon icon);

}  // namespace glim::ui
