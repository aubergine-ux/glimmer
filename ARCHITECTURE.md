# Architecture

Glimmer is a small C++20 codebase built as a stack of static libraries.

```
            ┌────────────────────────────┐
            │  app       (glimmer exe)   │  CLI, headless and scripted tools
            ├────────────────────────────┤
            │  platform  (SDL3)          │  window, audio device, gamepads, CRT
            ├────────────────────────────┤
            │  editors   Studio + tabs   │  code, sprites, map, sound, music
            ├──────────────┬─────────────┤
            │  ui          │  runtime    │  immediate-mode widgets │ Console
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

`load_file` also accepts a bare `.lua` file as a code-only cart, which is
the gentlest way to start.

`gif.cpp` is a small animated GIF encoder for recordings:

- **Colors:** every console frame fits in a 32-color local table, so each
  frame is 5-bit LZW and nothing needs quantizing.
- **Changed areas only:** a frame stores just the rectangle that changed, and
  identical frames lengthen the previous frame's delay. That keeps editor
  recordings small.

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

## ui

`ui::Ui` is an immediate-mode toolkit for the 256×144 screen. It has its own
`Memory` and `Gfx`, so the editors draw with the same rasterizer as carts
without touching the cart's RAM.

- **Widgets:** each widget is a function that draws itself and returns what
  happened this frame (`button`, `icon_button`, `spinner`, `slider`,
  `text_field`, `palette`).
- **Mouse capture:** a drag that starts on a widget keeps going when the
  pointer leaves it.
- **Popups:** `begin_popup`/`end_popup` stop clicks from reaching widgets
  underneath.
- **Themes:** colors come from a `Theme` of palette indices, so a theme is
  pure data. A high-contrast theme exists for accessibility.
- **Icons:** drawn from ASCII art in `icons.cpp`, like the font.
- **Cursor:** a software cursor is drawn into the frame, so it scales with the
  screen and shows up in recordings.
- **Palette:** the editor framebuffer uses the cart's RGB palette, so art is
  always shown in the colors the game will use.

### Translations

UI text goes through `ui::tr("English text", args...)`:

- **Keys:** the English string is the key, so English needs no file and an
  untranslated string simply stays English.
- **Language files:** each file in `lang/` is compiled into the binary by
  `cmake/embed_langs.cmake` (as bytes, which avoids compiler limits on string
  literals). Files in the user data folder can add or override languages.
- **Placeholders:** `{0}`, `{1}`… stand for values, so a translation can
  reorder them.
- **Text rendering:** text is UTF-8. `gfx::glyphs()` decodes it, mapping the
  Albanian letters to glyphs 138–141, and passes invalid bytes through as
  raw glyph codes, so carts that print glyph bytes directly keep working.

## editors

`Studio` owns the cart being edited and switches between editing and
playing.

- **Editing a copy:** editors change `Studio::cart()`. Running copies that
  cart into the console, so a running game can never corrupt its own source.
- **Hotkeys:** Esc, F7 and Ctrl+R watch live key state with their own edge
  detection. The cart's input is only latched at frame starts, which never
  happens if the cart crashes while booting.
- **Crashes:** the code editor opens on the failing line, found from the
  error's line number.
- **Sound previews:** these play the edited cart's sound data, which
  `Studio::sync_audio` copies into a scratch `Memory` every frame.

**Play, pause and hot reload:**

- **Pausing:** Esc during play pauses rather than stops.
- **Resuming:** resuming calls `Console::reload`. It copies the edited
  assets into RAM (the map only if it changed, so runtime `mset`s survive)
  and runs the new code with `Vm::reload`. That runs the chunk on the main
  Lua state, which never yields, under the same runaway guard, so functions
  are redefined while `_init` state is kept.

**The debugger** lives in the VM:

- **Line hook:** a line hook is installed only while there are breakpoints
  or a step is pending, so normal play pays nothing for it.
- **Stopping:** on a matching line, the hook captures a `DebugState` (frames
  with locals and upvalues, plus the cart's own globals, with tables expanded
  one level) and yields the frame thread. The cart is frozen mid-frame, and
  `tick()` reports `kBreak` until the debugger resumes it.
- **Stepping:** step over and step out compare the number of cart frames on
  the stack.
- **API names:** names that exist before the cart runs are recorded at
  startup, so the globals list shows only what the cart defined.

**Captures:** F6 screenshots and F8 GIF recordings use the studio's base
frame. The recording dot, toasts and the F3 performance overlay are drawn on
a copy afterwards, so they never end up in a capture.

Each editor is an `Editor` subclass whose `update()` handles input and draws
in one pass. Every editor has its own `History`, a diff-based undo stack:

- `begin()` snapshots a region of the cart (code, gfx, map, sfx, …).
- `commit()` stores only the changed bytes, after trimming the common prefix
  and suffix.
- An open step can absorb a burst of edits, which is how typing becomes a
  single undo step.

**Copy and paste:** copies are text with a `glimmer <kind>` header
(`clipboard.cpp`). They go through the system clipboard, so they work
between carts and between Glimmer windows.

| Editor | Notes |
|---|---|
| `CodeEditor` | Works directly on the cart's code string, with a line index and per-line lexer state rebuilt only when the text changes. Tabs are `--8<-- name` marker comments. Autocomplete and signature hints come from `api_docs.cpp`, which a test keeps in sync with the functions the VM actually registers. |
| `SpriteEditor` | Edits a 1×1, 2×2 or 4×4 sprite block. Shape tools redraw from a snapshot every frame for live previews. Selections lift into a floating layer that can be dragged and dropped. |
| `MapEditor` | Three zoom levels sample sprite pixels, so the quarter-zoom view shows the whole map. Auto-tiling takes the tile for each cell from a 4-bit neighbour mask within a 4×4 block (see [editor guide](docs/editors.md#auto-tiling)). |
| `SfxEditor` | Draws steps, interpolating between mouse samples so fast drags leave no gaps, and plays each edited step on channel 3. |
| `Shell` | The first tab: a command line with folder browsing, the cart browser, and the boot animation. The browser makes covers for label-less carts with `capture_label`, which runs a cart headless for a moment. |
| `MusicEditor` | A tracker over the pattern table, showing the notes of each channel's sound effect. It has piano and keyboard note entry, and the view follows playback through the audio status. |

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
- `--studio-script FILE [CART]` drives the editors with scripted mouse and
  keyboard actions (`click`, `drag`, `type`, `key ctrl+s`, `shot`, `record`).
  The README's hero GIF is one of these scripts: `docs/assets/src/hero.script`.
- `--headless CART` runs a cart, optionally holding buttons on a schedule
  (`--press a@30-60`), saving a screenshot (`--crt` for the filtered 4×
  version) or recording a GIF (`--record`). CI uses it to smoke-test every
  cart in `carts/`, and `tools/readme_assets.py` uses it to render every
  image in the README.

## Web and sharing

The web build produces two programs from the same sources:

| Program | Used for |
|---|---|
| `glimmer.js` / `.wasm` / `.data` | The full console, with the demo carts preloaded into the Emscripten file system. Its page is `web/shell.html`. |
| `glimmer_player.js` | The same code as one file with the wasm inlined (`-sSINGLE_FILE`), for exported games. |

**The player page:** `web/player.html` is a template. The site build fills it
in for the hosted player at `/play/`, and `cart/html.cpp` fills it in for
exported games, with the runtime and a base64url-packed cart inlined. The
page gets its cart from, in order:

1. a cart inlined by an export,
2. a `#c=` share link,
3. `?url=`,
4. `?cart=name`.

It writes the cart into the virtual file system before `main` runs, and
starts the program with `--player`.

**Touch controls:** these are HTML. The d-pad reads the finger's direction so
it can slide between arrows, and presses reach the console through the
exported `glim_touch()` function.

**Scaling:** on screens too small for 2× integer scaling, the frontend
switches to fractional letterboxing.

**Embedding:** `embed.js` defines `<glimmer-player>`, which wraps the player
page in an iframe.

**The site:** `tools/build_site.py` assembles the landing page, `/console/`,
`/play/` and the markdown docs rendered to HTML, and Pages deploys it on
every push.

**Native games:** `cart/bundle.cpp` appends the packed cart, its size and a
`GLIMCART` trailer to a copy of the executable, or writes `game.glim` into a
macOS app's Resources. At startup the frontend looks for either and switches
the studio to player mode, which has no editors; Esc shows a pause overlay.

## Builds and CI

| Workflow | What it does |
|---|---|
| `ci.yml` | Builds and tests on Linux, macOS, Windows and WebAssembly (tests under node), smoke-tests the carts natively and in headless Chrome (`tools/web_smoke.py`), and runs the suite under ASan and UBSan |
| `pages.yml` | Deploys the web console to GitHub Pages |
| `release.yml` | On a `v*` tag, builds self-contained desktop packages (SDL linked statically; a universal binary on macOS) and publishes a GitHub release with notes taken from the changelog |

## Tests

`tests/` is a doctest suite that runs natively and under node (WebAssembly):

| Area | What it checks |
|---|---|
| gfx | Primitive geometry, clipping, fill patterns, sprite flips and rotation, map layers and flags, font output |
| audio | Pitch accuracy, sample-exact note starts, click-free volume changes, loops, music flow, buffer-size independence |
| cart | Text, packed and PNG round trips, corrupt-data detection, size limits, `.lua` carts, lossless GIF frames |
| runtime | Callback order, the error screen and line numbers, budget spill, infinite loops, coroutines, the sandbox, the standard helpers, hot reload, breakpoints and stepping |
| editors | Undo history, clipboard formats, the Lua lexer, API docs coverage, shell commands, and each editor driven through real input events (typing, indentation, tabs, error navigation, pause and resume, breakpoints, flips, auto-tiling, note entry) |
