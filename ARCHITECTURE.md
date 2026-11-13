# Architecture

Glimmer is a small C++20 codebase built as a stack of static libraries. Only
the bottom layers are shown here; the editors (Milestone 2) and shell
(Milestone 3) will sit on top of `runtime`.

```
            ┌────────────────────────────┐
            │  app       (glimmer exe)   │  CLI, headless tools
            ├────────────────────────────┤
            │  platform  (SDL3)          │  window, audio device, gamepads, CRT
            ├────────────────────────────┤
            │  runtime   Console         │  boots carts, ticks frames, error screen
            ├──────────────┬─────────────┤
            │  script      │  cart       │  Lua VM + API  │  file formats
            ├──────┬───────┼──────┬──────┤
            │ gfx  │ audio │input │      │
            ├──────┴───────┴──────┴──────┤
            │  core      Memory + map    │
            └────────────────────────────┘
```

**Rule of thumb:** nothing below `platform` includes SDL or touches the OS
beyond plain file I/O in `cart`. The whole console therefore runs headless,
which is how the tests, the `--headless` tool and CI work, and the web build
reuses the same code unchanged.

## core

`Memory` (`src/core/memory.hpp`) is the console's 256 KiB of RAM.
`memory_map.hpp` names every address from [SPEC.md](SPEC.md#memory-map).

All console state that a cart can observe lives in this RAM: sprites, the map,
sound data, the palettes, and draw state such as the camera, clip rectangle and
fill pattern. That one choice keeps several things simple:

- `peek` and `poke` are complete with no special cases.
- A save state or snapshot is a single memcpy.
- Editors (Milestone 2) edit RAM directly, and a running cart sees the change
  immediately.

Out-of-range access is clamped, never undefined.

## gfx

`gfx::Gfx` is a software rasterizer that writes 1 byte per pixel into the
screen region of RAM. It reads its state from RAM on every call, so
`camera(4, 0)` and `poke2(0x182AC, 4)` do exactly the same thing.

Primitives work in screen space after the camera is subtracted, and go through
two paths:

- `plot` and `span` handle clipping and the fill pattern for shapes.
- `blit_sprite_pixel` handles transparency for sprites.

`to_rgb` resolves the screen palette and RGB palette into the final image.

The font (`font.cpp`) is drawn as ASCII art in a string and parsed at compile
time into a `constexpr` table, so changing a glyph means editing a picture of it.

`take_work()` reports how many pixels were touched, and the script layer turns
that into CPU cycles.

## audio

`audio::Audio` is the synthesizer. It is the only module shared between
threads:

- **Game thread:** issues commands (`sfx`, `music`, `note`) and calls `sync()`
  once per tick to snapshot sound data from RAM.
- **Audio device thread:** calls `render()`.

One mutex guards both sides. Its critical sections are short, and `render()`
never allocates.

Each channel keeps a continuous oscillator phase across notes and smooths its
amplitude over about 1 ms, so there are no clicks at step boundaries. Square
and saw waves use PolyBLEP to keep aliasing down. Sequencing is counted in
samples (one step is `speed × 360` samples), so timing is exact and doesn't
depend on the audio buffer size. The tests check this by rendering the same
song in 800-sample and 37-sample chunks and comparing the output.

## input

`input::Input` collects live state from the platform: keys (as USB HID usage
ids, which are also SDL scancodes), gamepad buttons and the mouse.
`begin_frame()` latches that state once per frame, so every read during a frame
sees the same values.

Keyboard keys map to player buttons through a `KeyMap` that loads from and
saves to a small text file (`keys.cfg`). Gamepad buttons are fed in directly.

## cart

A `Cart` is the persistent form of a game: code, metadata, the data regions
of RAM, and an optional label image. There are three encodings:

| Format | File | Notes |
|---|---|---|
| `.glim` text | `cart.cpp` | Diff-friendly. Sections are described by a table of `Grid` rows (field width, base, size), so one loop serializes them all. Unknown sections are kept unchanged when saving. |
| Packed | `pack.cpp` | Tagged chunks deflated with miniz. Compact, and used for PNG carts and web embedding. |
| `.png` | `pack.cpp` | Draws cartridge art with the label, then hides the packed cart in the low two bits of every RGBA channel, behind a CRC-checked header. |

## script

`script::Vm` owns a Lua 5.4 state and is the most delicate part of the system.

- **Lua is compiled as C++** (`third_party/CMakeLists.txt`), so `lua_error`
  unwinds with exceptions and destructors in the bindings run. The web build
  turns on wasm exceptions for the same reason.
- **The cart runs as jobs on one coroutine:** loading the chunk, then `_init`,
  then one job per frame (`_update` followed by `_draw`).
- **The CPU budget:** a count hook adds cycles every 1,000 instructions. When
  the budget is spent, the hook calls `lua_yield`, so the job stops mid-frame
  and resumes on the next tick. A hook only yields the frame thread itself.
  Yielding inside a cart's own coroutine would look like a spurious
  `coroutine.yield()` to the cart, so there the hook keeps counting instead.
  Code that can't yield, such as a `table.sort` comparator, may overrun up to
  64 frames' budget before it is stopped with an error.
- **The sandbox:** only base, table, string, math, coroutine and utf8 are
  opened, and `load`, `dofile`, `loadfile` and `string.dump` are removed. A
  custom allocator caps the heap at 16 MiB.
- **Bindings:** `api.cpp` holds the API, each function charging its cost. Small
  helpers that are natural to write in Lua (`all`, `split`, `add`, …) live in
  `prelude.cpp` and are charged like any other Lua code.
- **The Vm pointer** sits in the Lua state's extra space, which new threads
  inherit, so bindings fetch it in O(1).

## runtime

`Console` ties it all together. `run(cart)` resets RAM, loads the cart, and
creates a fresh `Vm`. Persistent `dget`/`dset` data is carried across, since it
belongs to the player rather than the cart. `tick()` advances one 60 Hz frame:

1. If a new frame is starting, latch input and write the hardware registers.
2. Run or resume the current job within the budget.
3. Snapshot sound data for the synth.
4. If the job finished, copy the screen to the RGB `display()` buffer. A half
   drawn frame is never shown.

A runtime error stops audio and draws an error screen with the message, cart
line and traceback, using a clean palette so the cart's palette tricks can't
hide it.

## platform

`platform::App` is the SDL3 frontend. It uses SDL's main-callbacks API, which
keeps one code path for desktop and Emscripten.

- **Fixed timestep:** the console ticks at exactly 60 Hz from a nanosecond
  accumulator, whatever the display's refresh rate. After a stall it catches up
  at most 4 frames.
- **Presentation:** logical presentation with integer scaling and nearest
  filtering, so pixels stay square at every window size. With the CRT filter
  on, a 4× image from `Crt` is drawn into the same logical rectangle.
- **CRT:** `Crt` runs on the CPU from a precomputed table of source taps
  (curvature and vignette), plus a per-pixel scanline profile, light bloom and
  an RGB mask. Doing it in software avoids per-backend shaders and keeps web
  and desktop pixel-identical.
- **Audio:** an `SDL_AudioStream` callback pulls exactly the samples SDL asks
  for straight from `Audio::render`.
- **Gamepads:** assigned to player slots in connection order. The left stick
  is mapped onto the d-pad buttons with a deadzone.

## app

`main.cpp` parses the command line. Besides launching the console, it offers
tools that don't open a window:

- `--export IN OUT` converts between formats. A cart with no label gets one
  captured from a short run.
- `--headless CART` runs a cart, optionally holding buttons on a schedule
  (`--press a@30-60`) and saving a screenshot. CI uses it to smoke-test every
  cart in `carts/`.

## Tests

`tests/` is a doctest suite that runs natively and under node (WebAssembly):

| Area | What it checks |
|---|---|
| gfx | Primitive geometry, clipping, fill patterns, sprite flips and rotation, map layers and flags, font output |
| audio | Pitch accuracy, sample-exact note starts, click-free volume changes, loops, music flow, buffer-size independence |
| cart | Text, packed and PNG round trips, corrupt-data detection, size limits |
| runtime | Callback order, the error screen and line numbers, budget spill, infinite loops, coroutines, the sandbox, the standard helpers |
