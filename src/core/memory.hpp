#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "core/memory_map.hpp"

namespace glim {

// Console RAM. Every subsystem reads and writes its state here so that
// peek/poke, save states and the editors all see one source of truth.
// Out-of-range reads return 0 and writes are dropped; carts must never be
// able to crash the host through memory access.
class Memory {
 public:
  Memory() { clear(); }

  void clear() { ram_.fill(0); }

  uint8_t peek(uint32_t a) const { return a < addr::kRamSize ? ram_[a] : 0; }
  void poke(uint32_t a, uint8_t v) {
    if (a < addr::kRamSize) ram_[a] = v;
  }

  uint16_t peek16(uint32_t a) const { return uint16_t(peek(a) | (peek(a + 1) << 8)); }
  void poke16(uint32_t a, uint16_t v) {
    poke(a, uint8_t(v));
    poke(a + 1, uint8_t(v >> 8));
  }
  uint32_t peek32(uint32_t a) const { return uint32_t(peek16(a)) | (uint32_t(peek16(a + 2)) << 16); }
  void poke32(uint32_t a, uint32_t v) {
    poke16(a, uint16_t(v));
    poke16(a + 2, uint16_t(v >> 16));
  }
  int16_t peeks16(uint32_t a) const { return int16_t(peek16(a)); }
  void pokes16(uint32_t a, int v) { poke16(a, uint16_t(int16_t(v))); }

  // memcpy/memset semantics with clamping to RAM; overlapping copies are safe.
  void copy(uint32_t dst, uint32_t src, uint32_t len);
  void fill(uint32_t dst, uint8_t v, uint32_t len);

  std::span<uint8_t> region(uint32_t a, uint32_t len) { return {ram_.data() + a, len}; }
  std::span<const uint8_t> region(uint32_t a, uint32_t len) const { return {ram_.data() + a, len}; }
  uint8_t* data() { return ram_.data(); }
  const uint8_t* data() const { return ram_.data(); }

 private:
  std::array<uint8_t, addr::kRamSize> ram_{};
};

}  // namespace glim
