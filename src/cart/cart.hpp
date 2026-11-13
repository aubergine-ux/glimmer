#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/memory.hpp"

namespace glim::cart {

// Bumped whenever console behaviour visible to carts changes (see SPEC.md).
inline constexpr int kSpecVersion = 1;
inline constexpr size_t kMaxCodeBytes = 128 * 1024;
inline constexpr int kPngWidth = 320;
inline constexpr int kPngHeight = 240;
// One payload byte per pixel, minus the stego header.
inline constexpr size_t kPngCapacity = size_t(kPngWidth) * kPngHeight - 12;

struct Cart {
  std::vector<std::pair<std::string, std::string>> meta;
  std::string code;
  std::vector<uint8_t> gfx, flags, map, sfx, wave, music, palette;
  std::vector<uint8_t> label;  // 256x144 color indices, empty when the cart has none
  // Sections this build doesn't understand, kept verbatim so saving never loses data.
  std::vector<std::pair<std::string, std::string>> extra;

  Cart();  // blank cart: default palette, sfx speed 16, empty music

  std::string get_meta(std::string_view key, std::string_view fallback = {}) const;
  void set_meta(std::string_view key, std::string_view value);
  int spec_version() const;

  void load_into(Memory& mem) const;
  void capture_from(const Memory& mem);
};

struct LoadResult {
  bool ok = false;
  std::string error;
};

std::string to_text(const Cart& cart);
LoadResult from_text(std::string_view text, Cart& out);

std::vector<uint8_t> pack(const Cart& cart);
LoadResult unpack(std::span<const uint8_t> data, Cart& out);

// Cartridge picture with the packed cart hidden in the low two bits of each channel.
LoadResult to_png(const Cart& cart, std::vector<uint8_t>& out);
LoadResult from_png(std::span<const uint8_t> png, Cart& out);

// Dispatches on content: PNG signature, packed magic, or text.
LoadResult load_bytes(std::span<const uint8_t> data, Cart& out);
LoadResult load_file(const std::string& path, Cart& out);
LoadResult save_file(const std::string& path, const Cart& cart);

}  // namespace glim::cart
