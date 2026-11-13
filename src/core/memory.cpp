#include "core/memory.hpp"

#include <algorithm>

namespace glim {

namespace {
uint32_t clamp_len(uint32_t a, uint32_t len) {
  if (a >= addr::kRamSize) return 0;
  return std::min(len, addr::kRamSize - a);
}
}  // namespace

void Memory::copy(uint32_t dst, uint32_t src, uint32_t len) {
  len = std::min(clamp_len(dst, len), clamp_len(src, len));
  if (len) std::memmove(ram_.data() + dst, ram_.data() + src, len);
}

void Memory::fill(uint32_t dst, uint8_t v, uint32_t len) {
  len = clamp_len(dst, len);
  if (len) std::memset(ram_.data() + dst, v, len);
}

}  // namespace glim
