#include "platform/boot_cart.hpp"

namespace glim::platform {

// Shown when Glimmer starts without a cart. It also doubles as a small
// example: the logo is printed, copied into the sprite sheet with memcpy,
// then drawn back enlarged with sspr.
const char* const kBootCartCode = R"lua(
local stars = {}
local colors = {29, 30, 17, 21, 28, 25, 4}

function _init()
  for i = 1, 70 do
    add(stars, {x = rnd(256), y = rnd(144), speed = rnd(0.8) + 0.1})
  end
  cls(0)
  print("GLIMMER", 0, 0, 31)
  for row = 0, 5 do
    memcpy(row * 128, 0x1a000 + row * 256, 32)
  end
end

function _draw()
  cls(1)
  for s in all(stars) do
    s.x = (s.x - s.speed) % 256
    pset(s.x, s.y, s.speed > 0.6 and 5 or 7)
  end

  local left = 128 - 7 * 20 / 2
  for i = 0, 6 do
    local y = 36 + sin(t() * 0.6 + i * 0.09) * 4
    pal(31, 2)
    sspr(i * 4, 0, 3, 5, left + i * 20 + 2, y + 2, 15, 25)
    pal(31, colors[i + 1])
    sspr(i * 4, 0, 3, 5, left + i * 20, y, 15, 25)
  end
  pal()

  local msg = "drop a .glim or .png cart here"
  print(msg, 128 - textwidth(msg) / 2, 92, 6)
  if t() % 1 < 0.6 then
    local hint = "or run: glimmer mygame.glim"
    print(hint, 128 - textwidth(hint) / 2, 102, 7)
  end
  print("v0.1", 2, 137, 7)
end
)lua";

}  // namespace glim::platform
