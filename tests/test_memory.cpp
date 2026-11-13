#include <doctest.h>

#include "core/memory.hpp"

using namespace glim;

TEST_CASE("memory peek and poke are little endian and bounds safe") {
  Memory m;
  m.poke32(addr::kUser, 0xDEADBEEF);
  CHECK(m.peek(addr::kUser) == 0xEF);
  CHECK(m.peek16(addr::kUser + 2) == 0xDEAD);
  CHECK(m.peek32(addr::kUser) == 0xDEADBEEF);

  m.poke(addr::kRamSize, 7);
  CHECK(m.peek(addr::kRamSize) == 0);
  m.poke16(addr::kRamSize - 1, 0xABCD);
  CHECK(m.peek(addr::kRamSize - 1) == 0xCD);
}

TEST_CASE("memory copy handles overlap and clamps at the end of RAM") {
  Memory m;
  for (int i = 0; i < 8; ++i) m.poke(addr::kUser + i, uint8_t(i + 1));
  m.copy(addr::kUser + 2, addr::kUser, 6);
  CHECK(m.peek(addr::kUser + 2) == 1);
  CHECK(m.peek(addr::kUser + 7) == 6);

  m.fill(addr::kRamSize - 4, 9, 100);
  CHECK(m.peek(addr::kRamSize - 1) == 9);
}
