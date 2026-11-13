# Contributing to Glimmer

Thanks for wanting to help! Bug reports, demo carts, docs fixes and code are
all welcome.

## Building

You need a C++20 compiler (GCC 12+, Clang 15+, MSVC 2022), CMake 3.24+ and,
optionally, Ninja.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

SDL3 comes from your system if it's installed. Otherwise, add
`-DGLIMMER_FETCH_SDL=ON` to download and build it. For the web build, see the
`web` job in [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Running carts without a window

`glimmer --headless` is the quickest way to check a change to the runtime:

```sh
./build/glimmer --headless carts/hello.glim --frames 120 --press right@0-60 --screenshot out.png
```

It exits with status 1 and prints the Lua traceback if the cart crashes. CI
runs every cart in `carts/` this way.

## Ground rules

- **The spec is a promise.** Anything a cart can observe is defined in
  [SPEC.md](SPEC.md): drawing results, timing, the memory layout, the cycle
  costs. Changing any of it needs a new spec version, with the old behaviour
  kept for carts that declare the old one. If you're unsure whether a change
  is observable, ask in the PR.
- **Keep the layers clean.** Only `src/platform` and `src/app` may include SDL.
  Everything else must build and run headless (see
  [ARCHITECTURE.md](ARCHITECTURE.md)).
- **Test what you touch.** Drawing, audio and cart format changes need doctest
  coverage in `tests/`. Audio tests should check sample-exact behaviour, not
  just "it made a sound".
- **Comments explain why.** Don't narrate code that already reads clearly.

## Code style

The code uses C++20 with 2-space indentation and lines up to about 120
columns. Names follow these patterns:

| Kind | Style | Example |
|---|---|---|
| Types | `PascalCase` | `KeyMap` |
| Functions and variables | `snake_case` | `begin_frame` |
| Constants | `kPascalCase` | `kScreenW` |
| Members | trailing underscore | `mem_` |

Prefer value types and RAII; no owning raw pointers. Running `clang-format`
on the files you touch is appreciated.

## Commits and pull requests

- Write commit messages in the conventional style, present tense:
  `feat(gfx): add oval fill`, `fix(audio): avoid click on loop wrap`,
  `docs: clarify fill pattern bit order`.
- Keep commits small and focused; a PR may contain several.
- Update [CHANGELOG.md](CHANGELOG.md) under *Unreleased* for anything a player
  or cart author would notice.

## Translating

The console UI is written in English and looked up through `tr()`. A
translation is a plain text file in `lang/`, named after the language code
(`lang/sq.txt` is Albanian). The rules:

- **Format:** each line maps English to the translation:
  `english text = translated text`. Lines starting with `#` are comments, and
  `language = Name` gives the language's own name.
- **Placeholders:** `{0}`, `{1}`… stand for values filled in at runtime.
  Keep them, in any order.
- **Characters:** the font covers ASCII plus the Albanian `ë ç Ë Ç`. A language
  that needs other letters needs glyphs in `src/gfx/font.cpp` first.

`tools/extract_strings.py lang/xx.txt` lists every string your file doesn't
translate yet, ready to paste in. The files are compiled into Glimmer. Players
can also drop a file into `lang/` in the user data folder to try a
translation without rebuilding.

## Demo carts

New demo carts go in `carts/`. A good demo:

- shows off one idea clearly,
- is commented so it can be read top to bottom as a tutorial,
- runs within the CPU budget with headroom (check `stat(1)`).
