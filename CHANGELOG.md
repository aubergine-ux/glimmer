# Changelog

All notable changes to Glimmer are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). Console behaviour is versioned
separately in [SPEC.md](SPEC.md).

## [Unreleased]

## [0.1.0] - 2026-11-13

The core runtime: everything needed to run carts, without the built-in editors yet.

### Added

- **Console spec v1:** 256×144 display, 32-color palette, two sprite sheets of
  256 sprites, a 4-layer 128×64 map, 4 synth channels, 4 players, and a
  1,000,000-cycle frame budget.
- **Software rasterizer:** pixels, lines, rectangles, circles, ovals,
  triangles, sprites (flip, stretch, rotate), map layers, text, clipping,
  camera, draw and screen palettes, and fill patterns.
- **Real-time synthesizer:** 8 waveforms plus 8 custom wavetables, slide,
  vibrato, drop, fades and arpeggios, and a pattern-based music sequencer.
- **Lua 5.4 runtime:** `_init` / `_update` / `_draw`, a sandbox, a 16 MiB heap
  cap, a deterministic cycle budget, and an error screen with line numbers and
  tracebacks.
- **Input:** keyboard, mouse, text and up to four gamepads, with rebindable
  keys via `keys.cfg`.
- **Cart formats:** `.glim` (diffable text), a compressed binary pack, and
  `.png` cartridge pictures with the cart hidden in the pixels.
- **Desktop app:** pixel-perfect integer scaling, a CRT filter (F9),
  fullscreen (F11), drag-and-drop carts, and persistent `dget`/`dset` saves.
- **Command-line tools:** `--export` converts between formats (and captures
  a label automatically), and `--headless` runs carts with scripted input,
  screenshots and GIF recording.
- **Plain `.lua` files:** these run as code-only carts.
- **Demo carts:** `hello` and `orbit`.
- **Builds and CI:** WebAssembly build, and CI on Linux, macOS, Windows and
  WebAssembly.

[Unreleased]: https://github.com/aubergine-ux/glimmer/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/aubergine-ux/glimmer/releases/tag/v0.1.0
