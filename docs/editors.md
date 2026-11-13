# Glimmer Editors

Glimmer's editors run inside the console at its own 256×144 resolution and
work with the mouse and keyboard. Running `glimmer` with no arguments boots
into the [shell](#shell). Pressing <kbd>Esc</kbd> while a cart is playing
pauses it and opens the editors.

## Everywhere

| Action | Keys |
|---|---|
| Run the cart (from the start) | <kbd>Ctrl</kbd>+<kbd>R</kbd> or the ▶ button |
| Pause the game and edit | <kbd>Esc</kbd> |
| Resume with your changes | <kbd>Esc</kbd> again, or <kbd>Ctrl</kbd>+<kbd>Enter</kbd> |
| Save | <kbd>Ctrl</kbd>+<kbd>S</kbd> |
| Switch editor | <kbd>Alt</kbd>+<kbd>1</kbd>–<kbd>5</kbd>, <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Undo / redo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Y</kbd> (or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>) |
| Copy, cut, paste | <kbd>Ctrl</kbd>+<kbd>C</kbd>, <kbd>X</kbd>, <kbd>V</kbd> |
| Capture the cart label while playing | <kbd>F7</kbd> |
| Screenshot (4×) | <kbd>F6</kbd> |
| Start / stop a GIF recording | <kbd>F8</kbd> |
| CPU and memory overlay | <kbd>F3</kbd> |
| Quit | <kbd>Ctrl</kbd>+<kbd>Q</kbd> |

Screenshots and recordings are saved to `Pictures/glimmer`. Recordings run
at 30 fps and stop by themselves after 30 seconds.

### Hot reload

Press <kbd>Esc</kbd> while a game runs, change anything (code, sprites,
sounds, the map), and press <kbd>Esc</kbd> again. The game carries on with
your changes, without restarting.

The cart's code is run again in the live game, which redefines your
functions. `_init` isn't called again, so state you set up there survives.
Map edits are applied only if you changed the map, so tiles the game changed
with `mset` stay as they are. If the new code doesn't compile, the game stays
paused and the editor shows the error.

Each editor has its own undo history.

Copies go through the system clipboard as plain text, so you can paste
sprites, map areas, sound effects and patterns into another cart, or even
into another Glimmer window. Drop a cart onto the window to open it while
keeping what you copied.

When a cart crashes, any key takes you back to the code editor with the
failing line highlighted.

## Shell

The first tab is the console's command line. It boots with a short
animation; any key skips it.

| Command | What it does |
|---|---|
| `ls`, `cd <folder>` | List and change folders (`cd ..` goes up) |
| `load <cart>` | Open a cart in the editors; the extension is optional |
| `run` | Play the current cart |
| `save [name]` | Save; a `.png` name saves a cartridge picture |
| `export <file>` | Write a `.png`, `.html` or `.glim` copy (PNGs get a label captured automatically) |
| `share` | Copy a link that plays the cart in any browser |
| `bundle <name>` | Build a standalone game: the runtime with your cart inside |
| `new` | Start a fresh cart |
| `browse` | Pick a cart from a grid of covers |
| `info` | Code size, packed size against the PNG limit, sprites used |
| `title <text>`, `author <text>` | Set the cart's metadata |
| `edit`, `cls`, `reboot`, `help` | |

<kbd>Tab</kbd> completes file names, and <kbd>↑</kbd> and <kbd>↓</kbd> step
through earlier commands.

### Sharing

| Format | What it is |
|---|---|
| `share` link | The whole cart, compressed into the address of the web player, e.g. `https://armd.one/glimmer/play/#c=…`. It works for carts up to a few kilobytes packed, which covers most small games. |
| `.html` | One self-contained web page with the runtime, your cart and on-screen touch controls. Host it anywhere or upload it to itch.io as an HTML game. A desktop release inlines the runtime so the page works offline; otherwise the page loads it from the website. |
| `bundle` | A copy of the Glimmer program with your cart attached, which starts straight into the game. Make one on each platform you want to publish for. On macOS the result is an `.app`. In a bundled game, <kbd>Esc</kbd> pauses and <kbd>Ctrl</kbd>+<kbd>R</kbd> restarts; there are no editors. |
| `.png` | The cartridge picture. Anyone with Glimmer can load it. |

In the **cart browser**, use the arrow keys to choose, <kbd>Enter</kbd> to
play and <kbd>E</kbd> to edit. Carts without a saved label get a cover from a
short run.

## Code

| Action | Keys |
|---|---|
| Find / find and replace | <kbd>Ctrl</kbd>+<kbd>F</kbd> / <kbd>Ctrl</kbd>+<kbd>H</kbd> (<kbd>Enter</kbd> next, <kbd>Shift</kbd>+<kbd>Enter</kbd> previous) |
| API reference | <kbd>F1</kbd>: type to filter, <kbd>Enter</kbd> inserts |
| Accept autocomplete | <kbd>Tab</kbd>, or <kbd>Enter</kbd> after browsing with the arrows |
| Indent / outdent | <kbd>Tab</kbd> / <kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Toggle comment | <kbd>Ctrl</kbd>+<kbd>/</kbd> |
| Duplicate line | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| Move line | <kbd>Alt</kbd>+<kbd>↑</kbd> / <kbd>↓</kbd> |
| Next / previous tab | <kbd>Ctrl</kbd>+<kbd>PgDn</kbd> / <kbd>PgUp</kbd> |

### Debugger

Click the left edge of the gutter, or press <kbd>Ctrl</kbd>+<kbd>B</kbd>, to
set a breakpoint on a line. When the running game reaches it, the game stops
and the debugger opens beside the code. The panel shows the call stack with
each function's locals and upvalues (prefixed `^`), followed by the cart's
own globals. Click a table to expand it.

| Action | Keys |
|---|---|
| Continue | <kbd>F5</kbd> |
| Step over | <kbd>F10</kbd> |
| Step into | <kbd>F11</kbd> |
| Step out | <kbd>Shift</kbd>+<kbd>F11</kbd> |

Breakpoints only stop the main game code, not code running inside a cart's
own coroutines.

### Tabs

**Tabs** split long code into sections. Click **+** to add a tab, and
double-click a tab to rename it. In the cart file, tabs are stored as
`--8<-- name` comment lines, so the code stays one Lua chunk and error line
numbers match.

**Writing code:**

- **Signature hints:** inside a call to an API function, a hint shows its
  arguments with the current one highlighted.
- **Indentation:** pressing <kbd>Enter</kbd> after `then`, `do`, `function(...)`
  or `{` indents the next line, and typing `end` snaps it back.

## Sprites

| Tool | Key | Notes |
|---|---|---|
| Pencil | <kbd>P</kbd> | Right-click picks a color from the canvas |
| Fill | <kbd>F</kbd> | Stays inside the selection, if there is one |
| Line, rectangle, circle | <kbd>L</kbd> <kbd>R</kbd> <kbd>C</kbd> | Hold <kbd>Shift</kbd> for filled shapes |
| Select | <kbd>S</kbd> | Drag inside a selection to move it; <kbd>Enter</kbd> drops it |
| Color picker | <kbd>I</kbd> | |
| Flip, rotate | <kbd>H</kbd> <kbd>V</kbd> <kbd>T</kbd> | Act on the selection or the whole block |
| Onion skin | <kbd>O</kbd> | Shows the previous sprite under the canvas |
| Grid | <kbd>G</kbd> | |
| Zoom (edit 8, 16 or 32 px) | <kbd>1</kbd> <kbd>2</kbd> <kbd>4</kbd>, mouse wheel | |
| Previous / next sprite | <kbd>[</kbd> <kbd>]</kbd> or the arrow keys | |
| Previous / next color | <kbd>-</kbd> <kbd>=</kbd> | |
| Play animation | <kbd>Space</kbd> | Wheel over the preview sets the frame count; <kbd>Shift</kbd>+wheel sets the speed |

The eight dots under the palette are the sprite's **flags**, which carts read
with `fget()`, for example to mark solid tiles.

## Map

| Tool | Key | Notes |
|---|---|---|
| Paint | <kbd>P</kbd> | Right-click picks the tile under the mouse |
| Rectangle fill | <kbd>R</kbd> | Multi-tile stamps repeat across the rectangle |
| Flood fill | <kbd>F</kbd> | |
| Select | <kbd>S</kbd> | <kbd>Ctrl</kbd>+<kbd>C</kbd> copies; pasting turns the clipboard into a stamp |
| Auto-tile | <kbd>A</kbd> | See below |
| Tile picker | <kbd>Tab</kbd> | Drag across the sheet for a multi-tile stamp |
| Layers | <kbd>1</kbd>–<kbd>4</kbd> | Right-click a layer button to hide it |
| Zoom | mouse wheel | ¼ zoom shows the whole map |
| Pan | <kbd>Space</kbd>+drag, middle-drag, arrow keys | |
| Screen guides | <kbd>G</kbd> | Lines every 32×18 cells, one game screen |

### Auto-tiling

An auto-tile **terrain** is a 4×4 block of 16 sprites on the sheet. The
auto-tile brush paints with the block that contains your current tile. For
each painted cell it picks the sprite whose position in the block matches
which of its neighbours are the same terrain:

| Neighbour | Value |
|---|---|
| north | 1 |
| east | 2 |
| south | 4 |
| west | 8 |

Add up the values for the neighbours that are present. The total, from 0 to
15, counts through the block left to right and then top to bottom. So the
block's top-left sprite is a lone tile, and its bottom-right sprite is
surrounded on all four sides. Painting also updates the neighbours, so paths
and platforms join up as you draw.

## Sound

Draw **pitch** in the big area and **volume** in the strip below it.
Right-drag erases notes. New notes take the wave and effect pens from the
bottom bar. Click a cell in the wave or effect rows to stamp the pen, or
scroll over it to cycle through the values.

| Action | Keys |
|---|---|
| Play / stop | <kbd>Space</kbd> |
| Previous / next sound | <kbd>[</kbd> <kbd>]</kbd> |
| Custom wavetables | <kbd>W</kbd>: pick wave 8–f below, then draw its shape |

Every edit plays the step you changed, so you hear it right away.

## Music

Each pattern has four channels, and each channel plays one sound effect. The
grid shows that effect's notes, so editing a note here changes the sound
effect itself.

| Action | Keys |
|---|---|
| Notes | Bottom row <kbd>Z</kbd> <kbd>S</kbd> <kbd>X</kbd> <kbd>D</kbd> <kbd>C</kbd> <kbd>V</kbd> … <kbd>M</kbd> for one octave, top row <kbd>Q</kbd> <kbd>2</kbd> <kbd>W</kbd> <kbd>3</kbd> <kbd>E</kbd> … <kbd>I</kbd> for the next |
| Octave | <kbd>-</kbd> <kbd>=</kbd> |
| Rest | <kbd>Delete</kbd> / <kbd>Backspace</kbd> |
| Transpose note | <kbd>Shift</kbd>+<kbd>↑</kbd> / <kbd>↓</kbd> |
| Play song from here / stop | <kbd>Space</kbd> |
| Play this pattern once | <kbd>Enter</kbd> |
| Previous / next pattern | <kbd>[</kbd> <kbd>]</kbd> |

The **loop<**, **>loop** and **stop** buttons set the pattern flags. With
**follow** on, the view tracks the music as it plays.
