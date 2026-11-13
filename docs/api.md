# Glimmer API Reference

Spec version 1. Arguments in `[brackets]` are optional. Coordinates are
floored to integers, and a color argument that is left out uses the current
pen color (see `color`).

- [Callbacks](#callbacks)
- [Graphics](#graphics)
- [Sprites and map](#sprites-and-map)
- [Text](#text)
- [Draw state](#draw-state)
- [Audio](#audio)
- [Input](#input)
- [Memory](#memory)
- [Math](#math)
- [Tables and strings](#tables-and-strings)
- [System](#system)

## Callbacks

Define any of these as globals and Glimmer calls them for you.

| Function | When it runs |
|---|---|
| `_init()` | Once, after the cart's top-level code has run |
| `_update()` | Every frame, 60 times a second, before `_draw` |
| `_draw()` | Every frame, after `_update` |

Top-level code runs first, so you can also draw straight from it. The screen
shows a frame only once both callbacks have returned.

```lua
function _init()  x = 128 end
function _update() if btn("right") then x = x + 1 end end
function _draw()  cls(1) circfill(x, 72, 6, 17) end
```

## Graphics

| Function | Description |
|---|---|
| `cls([c])` | Clear the screen to color `c` (default 0) and reset the print cursor |
| `pset(x, y, [c])` | Set one pixel |
| `pget(x, y)` | Color of a pixel; 0 off screen |
| `line(x0, y0, x1, y1, [c])` | Line with both endpoints included |
| `rect(x0, y0, x1, y1, [c])` | Rectangle outline; corners may come in any order |
| `rectfill(x0, y0, x1, y1, [c])` | Filled rectangle |
| `circ(x, y, [r], [c])` | Circle outline, radius `r` (default 4) |
| `circfill(x, y, [r], [c])` | Filled circle |
| `oval(x0, y0, x1, y1, [c])` | Ellipse fitted to a bounding box |
| `ovalfill(x0, y0, x1, y1, [c])` | Filled ellipse |
| `tri(x0, y0, x1, y1, x2, y2, [c])` | Triangle outline |
| `trifill(x0, y0, x1, y1, x2, y2, [c])` | Filled triangle |

Shapes honour the camera, the clip rectangle, the draw palette and the fill pattern.

## Sprites and map

| Function | Description |
|---|---|
| `spr(n, x, y, [w], [h], [flip_x], [flip_y])` | Draw sprite `n` (0–511), or a `w`×`h` block of sprites starting at `n` |
| `sspr(sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])` | Stretch any rectangle of the sprite sheets. Sheet 1 starts at `sy = 128` |
| `rspr(n, x, y, [angle], [scale], [w], [h], [flip_x], [flip_y])` | Rotated and scaled sprite. `angle` is in turns (0.25 = 90° clockwise); it rotates around the block's center |
| `sget(x, y)` / `sset(x, y, [c])` | Read or write a sprite sheet pixel (0 ≤ x < 128, 0 ≤ y < 256) |
| `fget(n, [bit])` | Sprite flags as a byte, or one flag as a boolean |
| `fset(n, [bit], value)` | Set all 8 flags, or one flag |
| `map([cx], [cy], [sx], [sy], [w], [h], [layers], [flags])` | Draw `w`×`h` cells from map cell (`cx`, `cy`) at screen position (`sx`, `sy`). Defaults to a full screen of cells. `layers` is a bitmask (default `0xF`, all four); with `flags`, only tiles whose sprite has all those flag bits are drawn |
| `mget(x, y, [layer])` | Map cell value: sprite id in bits 0–8, flips in bits 9–10 |
| `mset(x, y, value, [layer])` | Set a map cell |

Color 0 is transparent for sprites by default (see `palt`). Map cells holding
sprite 0 are never drawn.

## Text

| Function | Description |
|---|---|
| `print(value, x, y, [c])` | Print at a position. Returns the x just past the text |
| `print(value, [c])` | Print at the cursor and move to the next line; scrolls at the bottom |
| `cursor([x], [y], [c])` | Move the print cursor and optionally set the pen |
| `textwidth(value)` | Width in pixels of the widest line |

The font is 3×5 pixels in a 4×6 cell. `\n` starts a new line. Codes 128–137
are symbols: ◀ ▶ ▲ ▼ ■ ♥ ✦ ♪ ✚ ▢.

## Draw state

| Function | Description |
|---|---|
| `camera([x], [y])` | Offset all drawing by (−x, −y). Returns the previous x, y |
| `clip([x, y, w, h])` | Restrict drawing to a screen rectangle; call with no arguments to reset |
| `color([c])` | Set the pen color (default 31). Returns the previous pen |
| `pal(c0, c1, [screen])` | Draw color `c0` as `c1`. With `screen = true`, remap the whole displayed frame instead |
| `pal(table, [screen])` | Several remaps at once, e.g. `pal({[1] = 2, [3] = 4})` |
| `pal()` | Reset both palettes |
| `palt(c, transparent)` | Mark color `c` as transparent (or opaque) for sprites |
| `palt(mask)` / `palt()` | Set all 32 transparency bits at once, or reset to only color 0 |
| `fillp([pattern], [alt])` | 4×4 fill pattern for shapes. Set bits draw `alt`, or nothing if `alt` is omitted. Bit 15 is the top-left pixel |
| `reset()` | Reset camera, clip, palettes, pen, fill pattern and cursor |

```lua
fillp(0b1010010110100101, 2) -- checkerboard of the pen color and color 2
rectfill(10, 10, 60, 40, 3)
fillp()
```

## Audio

| Function | Description |
|---|---|
| `sfx(n, [channel], [offset], [length])` | Play sound effect `n` (0–63). With no channel, a free one not used by music is picked |
| `sfx(-1, [channel])` | Stop sound effects, on one channel or all |
| `sfx(-2, [channel])` | Let a looping sound effect play out to its end |
| `music(n, [fade_ms], [channel_mask])` | Play music from pattern `n`. The mask (default `0xF`) reserves channels for music |
| `music(-1, [fade_ms])` | Stop music, optionally fading out |
| `note(channel, pitch, [wave], [volume], [effect])` | Hold a live note until `note(channel)` stops it. Pitch 0–63 (33 = A4); wave 0–15, volume 0–7 (default 5) |

## Input

Buttons can be given as numbers 0–9 or names: `left right up down a b x y l r`.

| Function | Description |
|---|---|
| `btn(b, [player])` | Is the button held? Player 0–3 |
| `btn()` | All buttons of player 0 as a bitfield |
| `btnp(b, [player])` | Pressed this frame; repeats while held (after 15 frames, every 4) |
| `key(name)` | Is a keyboard key held? Names: `a`–`z`, `0`–`9`, `f1`–`f12`, `space`, `return`, `escape`, `backspace`, `tab`, `left`, `right`, `up`, `down`, `lshift`, `rshift`, `lctrl`, `rctrl`, `lalt`, `ralt`, punctuation such as `,` and `/` |
| `keyp(name)` | Key pressed this frame |
| `mouse()` | Returns `x, y, buttons, wheel`. Buttons: bit 0 left, 1 right, 2 middle |
| `textin()` | Text typed since the last frame |

## Memory

| Function | Description |
|---|---|
| `peek(addr, [n])` | Read `n` bytes (default 1), returned as `n` values |
| `poke(addr, v, ...)` | Write bytes |
| `peek2(addr)` / `poke2(addr, v)` | Unsigned 16-bit value |
| `peek4(addr)` / `poke4(addr, v)` | Signed 32-bit value |
| `memcpy(dst, src, len)` | Copy bytes; overlapping ranges are safe |
| `memset(dst, value, len)` | Fill bytes |
| `dget(slot)` / `dset(slot, value)` | Persistent storage: 64 numbers (stored as 32-bit floats) that survive restarts |

See the memory map in [SPEC.md](../SPEC.md#memory-map).

## Math

| Function | Description |
|---|---|
| `flr(x)` / `ceil(x)` | Round down or up to an integer |
| `abs(x)`, `sqrt(x)`, `min(a, b)`, `max(a, b)` | As usual |
| `mid(a, b, c)` | The middle value; handy for clamping |
| `clamp(v, lo, hi)` | Clamp `v` to the range [lo, hi] |
| `lerp(a, b, t)` | Linear interpolation |
| `sgn(x)` | −1 for negative numbers, otherwise 1 |
| `sin(t)` / `cos(t)` | Angles in turns: `sin(0.25) == 1` |
| `atan2(dx, dy)` | Angle in turns, 0 to 1 |
| `rnd([x])` | Random number in [0, x) (default 1); `rnd(table)` picks a random element |
| `srand(seed)` | Seed the random generator for repeatable sequences |

The whole Lua `math` library is also available.

## Tables and strings

| Function | Description |
|---|---|
| `add(t, v, [i])` | Append, or insert at `i`. Returns `v` |
| `del(t, v)` | Remove the first `v` |
| `deli(t, [i])` | Remove at index `i` (default: the last item) |
| `count(t, [v])` | Length, or number of occurrences of `v` |
| `all(t)` | Iterator over values. You may `del` the current item while looping |
| `foreach(t, f)` | Call `f(v)` for each value |
| `split(s, [sep], [convert])` | Split on a separator (default `,`); numeric parts become numbers unless `convert` is `false` |
| `sub(s, i, [j])`, `chr(...)`, `ord(s, [i], [n])` | String helpers |
| `tostr(v, [hex])` | Readable string: `tostr(2.0) == "2"`, `tostr(255, true) == "0xff"` |
| `tonum(v)` | Number from a string or boolean |

The Lua `string`, `table`, `utf8` and `coroutine` libraries are available.

## System

| Function | Description |
|---|---|
| `time()` / `t()` | Seconds since the cart started, counted in completed frames |
| `stat(n)` | System info: see below |
| `printh(...)` | Print to the host terminal, for debugging |
| `trace([msg])` | Current stack traceback as a string |

| `stat` | Value |
|---|---|
| 0 | Lua memory in use, in KiB |
| 1 | CPU used by the last frame (1.0 = the whole budget) |
| 2 | Frames completed |
| 3 | Target frame rate (60) |
| 4–7 | Sfx playing on channels 0–3, or −1 |
| 8–11 | Current step on channels 0–3 |
| 12 | Music pattern playing, or −1 |
| 13 | CPU used so far this frame |

## Sandbox

Carts can't touch files, the network or the OS. `io`, `os`, `package`,
`debug`, `load`, `dofile`, `loadfile` and `string.dump` are not available.
