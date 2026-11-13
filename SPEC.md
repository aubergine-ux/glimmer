# Glimmer Console Specification

**Spec version 1** · status: stable

This document defines the virtual hardware every Glimmer cart runs on. Carts
record the spec version they were made for (`spec = 1` in `[meta]`), and every
future Glimmer release keeps running them exactly as described here. New
features arrive as new spec versions; behaviour within a version never changes.

## At a glance

| Part | Spec |
|---|---|
| Display | 256 × 144 pixels, 60 fps |
| Palette | 32 colors from a programmable RGB palette, with draw and screen palette swaps |
| Sprites | 8 × 8 pixels, 256 per sheet, 2 sheets (512 sprites), 8 flags each |
| Map | 128 × 64 cells, 4 layers |
| Sound | 4 channels, 8 built-in waveforms + 8 custom wavetables, 8 per-note effects |
| Music | 128 patterns of 4 channels, with loop and stop flags |
| Input | 4 players × (d-pad + A B X Y L R), mouse, keyboard |
| CPU | 1,000,000 cycles per frame |
| RAM | 256 KiB, all of it reachable with `peek` / `poke` |
| Lua heap | 16 MiB |
| Code | 128 KiB of Lua 5.4 source |
| Cart size | unlimited as `.glim`; 76,788 bytes compressed as `.png` |

## Display

The screen is 256 × 144 pixels (16:9) and refreshes at exactly 60 Hz. Each
pixel holds a color index from 0 to 31.

When a frame finishes, every pixel goes through two lookups:

1. **Screen palette** (`pal(a, b, true)`): maps the stored index to a displayed index.
2. **RGB palette** (`0x18200`): maps the displayed index to a 24-bit color.

The **draw palette** (`pal(a, b)`) is applied when pixels are written, so it
affects only what is drawn afterwards. The screen palette affects the whole
frame at once, which is how fades and flashes are done.

### Default palette: Dusk 32

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| **0** | `0b0a12` | `1f1a33` | `3a2f5b` | `6b4f9e` | `a77bd8` | `f2e9ff` | `8a8fa8` | `4a4e69` |
| **8** | `2b2d42` | `5c1f2e` | `a32d3c` | `e8534a` | `ff8c69` | `ffc2a1` | `7a3e1d` | `c06a2b` |
| **16** | `f39c34` | `ffd166` | `fff4a3` | `1e4d2b` | `2f8a3e` | `6cc551` | `c2f07a` | `133b5c` |
| **24** | `1f6fb2` | `3fa7f0` | `8fdcff` | `1d6b6b` | `36c2a6` | `b03f86` | `ff6fb5` | `ffffff` |

A cart may ship its own palette in a `[palette]` section, or poke new RGB
values into `0x18200` at runtime.

### Drawing rules

- Coordinates are floored to integers. The camera offset is subtracted before
  clipping, and the clip rectangle is in screen coordinates.
- Shapes and lines use the **fill pattern**, a 4 × 4 bitmask. A set bit draws
  the alternate color, or nothing if the alternate is transparent. Bit 15 is
  the top-left pixel, and the pattern is anchored to screen coordinates.
- Sprites and map tiles skip any color whose transparency bit is set. Only
  color 0 is transparent by default. Fill patterns do not apply to sprites.
- Text uses the built-in 3 × 5 font in a 4 × 6 cell, so 64 columns by 24 rows
  fit on screen.

## Sprites and map

The two sprite sheets are stored as one 128 × 256 pixel strip: sheet 0 holds
rows 0–127 and sheet 1 holds rows 128–255. Sprite `n` sits at
`x = (n % 16) * 8`, `y = (n // 16) * 8`, so sprites 256–511 live on sheet 1.

Each map cell is 16 bits:

| Bits | Meaning |
|---|---|
| 0–8 | sprite id (0–511), where 0 means an empty cell |
| 9 | flip horizontally |
| 10 | flip vertically |
| 11–15 | reserved, kept as stored |

Layers draw in order 0 → 3 when several are requested.

## Sound

Audio runs at 48,000 samples per second, which is exactly 800 samples per
frame. The four channels are mixed with fixed headroom.

### Sound effects

There are 64 sound effects, each with 32 steps and a 4-byte header:

| Byte | Meaning |
|---|---|
| 0 | speed: each step lasts `speed × 360` samples (7.5 ms per unit), and 0 is treated as 1 |
| 1 | loop start step |
| 2 | loop end step; the sfx loops while `loop_end > loop_start` |
| 3 | reserved |

Each step is 16 bits, little endian:

| Bits | Field | Range |
|---|---|---|
| 0–5 | pitch | 0–63 semitones above C2 (65.41 Hz), so pitch 33 = A4 = 440 Hz |
| 6–9 | waveform | 0–15 |
| 10–12 | volume | 0–7, where 0 is a rest |
| 13–15 | effect | 0–7 |

**Waveforms:** 0 square, 1 pulse (25%), 2 triangle, 3 saw, 4 noise (pitched),
5 organ, 6 sine, 7 soft square, and 8–15 the custom wavetables 0–7. A custom
wavetable is 32 signed 8-bit samples played with linear interpolation.

**Effects:**

| # | Effect | Behaviour over the step |
|---|---|---|
| 0 | none | |
| 1 | slide | pitch glides from the previous audible note |
| 2 | vibrato | ±0.35 semitone at 6 Hz |
| 3 | drop | pitch falls three octaves |
| 4 | fade in | volume rises from 0 |
| 5 | fade out | volume falls to 0 |
| 6 | fast arpeggio | cycles the 4-step group's pitches, one per frame |
| 7 | slow arpeggio | as above, one every 2 frames |

Volume changes are smoothed over about 1 ms, so notes never click. Square, pulse
and saw waves are band-limited.

### Music

There are 128 patterns of 4 bytes, one byte per channel:

| Bits | Meaning |
|---|---|
| 0–5 | sfx to play on this channel |
| 6 | channel disabled |
| 7 | flag: on channel 0 the pattern is a **loop start**; on channel 1 a **loop end**; on channel 2 music **stops** after this pattern |

A pattern lasts as long as its first enabled channel that doesn't loop. If
every enabled channel loops, it lasts as long as its longest sfx. When a
pattern ends, music jumps back to the nearest loop start if the pattern is
flagged loop end, stops if it is flagged stop, and otherwise moves to the next
pattern. Music also stops on reaching a pattern with every channel disabled.

## Input

Every player has ten buttons:

| Index | Name | Player 1 keys | Player 2 keys | Gamepad |
|---|---|---|---|---|
| 0 | `left` | ← | J | d-pad / left stick |
| 1 | `right` | → | L | d-pad / left stick |
| 2 | `up` | ↑ | I | d-pad / left stick |
| 3 | `down` | ↓ | K | d-pad / left stick |
| 4 | `a` | Z | N | south (A / ✕) |
| 5 | `b` | X | M | east (B / ○) |
| 6 | `x` | A | U | west (X / □) |
| 7 | `y` | S | O | north (Y / △) |
| 8 | `l` | Q | 7 | left shoulder |
| 9 | `r` | W | 9 | right shoulder |

Keys are rebindable in `keys.cfg`, found in the user data folder. Input is
latched once at the start of each frame. `btnp` fires on the press, then
repeats every 4 frames once a button has been held for 15.

## CPU

A frame may spend **1,000,000 cycles** across `_update` and `_draw`:

- Each Lua VM instruction costs 1 cycle. The cost is measured in blocks of 1,000.
- Each API call costs 4 cycles, plus 1 cycle per 4 pixels drawn and 1 cycle
  per 4 bytes copied by `memcpy` or `memset`. `map` also costs 1 cycle per cell.

If a frame runs over budget, it is paused where it is and resumed on the next
tick. Input is not re-read and the screen is not updated until the frame
completes, so an overloaded cart slows down by whole frames, identically on
every machine. `stat(1)` reports the cost of the last frame as a fraction of
the budget.

## Memory map

| Address | Size | Contents |
|---|---|---|
| `0x00000` | 32 KiB | Sprite sheets (128 × 256, 1 byte per pixel) |
| `0x08000` | 64 KiB | Map: 4 layers × 128 × 64 cells × 2 bytes |
| `0x18000` | 512 B | Sprite flags |
| `0x18200` | 96 B | RGB palette (32 × r, g, b) |
| `0x18260` | 32 B | Draw palette |
| `0x18280` | 32 B | Screen palette |
| `0x182A0` | 4 B | Transparency mask (bit n = color n) |
| `0x182A4` | 8 B | Clip rectangle: int16 x0, y0, x1, y1 (end exclusive) |
| `0x182AC` | 4 B | Camera: int16 x, y |
| `0x182B0` | 1 B | Pen color |
| `0x182B1` | 2 B | Fill pattern |
| `0x182B3` | 1 B | Fill pattern alternate color (`0xFF` = transparent) |
| `0x182B4` | 4 B | Print cursor: int16 x, y |
| `0x18400` | 4,352 B | 64 sound effects × 68 bytes |
| `0x19600` | 256 B | 8 custom wavetables × 32 samples |
| `0x19700` | 512 B | 128 music patterns × 4 bytes |
| `0x19900` | 256 B | Persistent data (`dget` / `dset`, 64 × float32) |
| `0x19A00` | 8 B | Buttons, uint16 per player (read only) |
| `0x19A08` | 6 B | Mouse: int16 x, int16 y, uint8 buttons, int8 wheel (read only) |
| `0x19A10` | 4 B | Sfx playing per channel, int8, -1 when idle (read only) |
| `0x19A14` | 4 B | Step per channel (read only) |
| `0x19A18` | 1 B | Music pattern, int8, -1 when stopped (read only) |
| `0x1A000` | 36 KiB | Screen (256 × 144, 1 byte per pixel) |
| `0x23000` | 116 KiB | General-purpose RAM, free for carts |

Multi-byte values are little endian. Reads outside RAM return 0, and writes
outside RAM are ignored. "Read only" registers are rewritten by the console at
the start of every frame.

## Cart formats

### `.glim` (text)

This is the canonical format: UTF-8 text designed to diff well in version control.

```
glimmer cart 1
[meta]
title = My Game
author = me
spec = 1
[code]
function _draw() cls(1) end
[gfx]
0000aaaa0000...
```

The rules are:

- **Sections** are a line containing only `[name]`. Known sections are `meta`,
  `code`, `gfx`, `flags`, `map0`–`map3`, `sfx`, `wave`, `music`, `palette` and
  `label`. Unknown sections are kept unchanged when a cart is saved.
- **Code:** a code line that starts with `[` or `\` is written with an extra
  leading `\`, which is removed on load.
- **gfx and label:** one character per pixel in base 32 (`0`–`9`, `a`–`v`).
  `gfx` has 128 characters per row and up to 256 rows. `label` has 256 per row
  and up to 144 rows.
- **Hex sections:** `flags`, `sfx`, `wave`, `music` and `palette` are rows of
  bytes in hex. Map rows hold 128 cells of 4 hex digits each.
- **Trailing rows:** trailing rows that equal the blank default may be omitted
  on save. Missing rows load as the blank default: sfx speed 16, music
  channels disabled, and the default palette.
- **Line endings:** CRLF line endings are accepted.

### Packed

The packed format is used inside PNG carts and by the web player. It is
`GLMZ`, then a uint32 for the unpacked size, then a zlib stream. Unpacked, the
data is `GLIM`, a uint16 format version and a uint16 spec version, followed by
chunks. Each chunk is a 4-byte tag, a uint32 length and then the data. The tags
are `META`, `CODE`, `GFX `, `FLAG`, `MAP `, `SFX `, `WAVE`, `MUSC`, `PAL `,
`LABL` and `XTRA`.

### `.png` (cartridge picture)

This is a 320 × 240 image of a cartridge with the cart's label on it. The
packed cart is stored in the low two bits of each RGBA channel, one byte per
pixel in reading order. The bits go most significant first, in the order
R, G, B, A. The stream starts with a 12-byte header: `GLM1`, a uint32 length
and the CRC-32 of the payload. Re-encoding the image with lossy compression or
resizing it destroys the data, and the CRC check reports it.
