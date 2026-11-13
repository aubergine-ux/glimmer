#include "editors/api_docs.hpp"

#include <array>

namespace glim::editors {

namespace {

constexpr std::array kDocs = {
    // Graphics
    ApiDoc{"cls", "graphics", "cls([c])", "clear the screen to color c"},
    ApiDoc{"pset", "graphics", "pset(x, y, [c])", "set one pixel"},
    ApiDoc{"pget", "graphics", "pget(x, y)", "color of a pixel, 0 off screen"},
    ApiDoc{"line", "graphics", "line(x0, y0, x1, y1, [c])", "line with both ends"},
    ApiDoc{"rect", "graphics", "rect(x0, y0, x1, y1, [c])", "rectangle outline"},
    ApiDoc{"rectfill", "graphics", "rectfill(x0, y0, x1, y1, [c])", "filled rectangle"},
    ApiDoc{"circ", "graphics", "circ(x, y, [r], [c])", "circle outline"},
    ApiDoc{"circfill", "graphics", "circfill(x, y, [r], [c])", "filled circle"},
    ApiDoc{"oval", "graphics", "oval(x0, y0, x1, y1, [c])", "ellipse in a box"},
    ApiDoc{"ovalfill", "graphics", "ovalfill(x0, y0, x1, y1, [c])", "filled ellipse"},
    ApiDoc{"tri", "graphics", "tri(x0, y0, x1, y1, x2, y2, [c])", "triangle outline"},
    ApiDoc{"trifill", "graphics", "trifill(x0, y0, x1, y1, x2, y2, [c])", "filled triangle"},
    // Sprites and map
    ApiDoc{"spr", "sprites", "spr(n, x, y, [w], [h], [flip_x], [flip_y])", "draw sprite n (0-511)"},
    ApiDoc{"sspr", "sprites", "sspr(sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])", "stretch a sheet area"},
    ApiDoc{"rspr", "sprites", "rspr(n, x, y, [angle], [scale], [w], [h], [flip_x], [flip_y])", "rotated sprite, angle in turns"},
    ApiDoc{"sget", "sprites", "sget(x, y)", "sprite sheet pixel"},
    ApiDoc{"sset", "sprites", "sset(x, y, [c])", "set sprite sheet pixel"},
    ApiDoc{"fget", "sprites", "fget(n, [bit])", "sprite flags, or one flag"},
    ApiDoc{"fset", "sprites", "fset(n, [bit], value)", "set sprite flags"},
    ApiDoc{"map", "sprites", "map([cx], [cy], [sx], [sy], [w], [h], [layers], [flags])", "draw map cells"},
    ApiDoc{"mget", "sprites", "mget(x, y, [layer])", "map cell value"},
    ApiDoc{"mset", "sprites", "mset(x, y, value, [layer])", "set a map cell"},
    // Text
    ApiDoc{"print", "text", "print(value, [x, y], [c])", "print text, returns end x"},
    ApiDoc{"cursor", "text", "cursor([x], [y], [c])", "move the print cursor"},
    ApiDoc{"textwidth", "text", "textwidth(value)", "width of text in pixels"},
    // Draw state
    ApiDoc{"camera", "state", "camera([x], [y])", "offset drawing, returns old x, y"},
    ApiDoc{"clip", "state", "clip([x, y, w, h])", "restrict drawing to a rectangle"},
    ApiDoc{"color", "state", "color([c])", "set the pen color"},
    ApiDoc{"pal", "state", "pal([c0, c1], [screen])", "swap colors, pal() resets"},
    ApiDoc{"palt", "state", "palt([c, transparent])", "sprite transparency"},
    ApiDoc{"fillp", "state", "fillp([pattern], [alt])", "4x4 fill pattern for shapes"},
    ApiDoc{"reset", "state", "reset()", "reset all draw state"},
    // Audio
    ApiDoc{"sfx", "audio", "sfx(n, [channel], [offset], [length])", "play sound effect, -1 stops"},
    ApiDoc{"music", "audio", "music(n, [fade_ms], [channel_mask])", "play music from pattern n"},
    ApiDoc{"note", "audio", "note(channel, [pitch], [wave], [volume], [effect])", "hold a live note"},
    // Input
    ApiDoc{"btn", "input", "btn([b], [player])", "is button held? b: 0-9 or name"},
    ApiDoc{"btnp", "input", "btnp([b], [player])", "button pressed, with repeat"},
    ApiDoc{"key", "input", "key(name)", "is keyboard key held?"},
    ApiDoc{"keyp", "input", "keyp(name)", "key pressed this frame"},
    ApiDoc{"mouse", "input", "mouse()", "returns x, y, buttons, wheel"},
    ApiDoc{"textin", "input", "textin()", "text typed since last frame"},
    // Memory
    ApiDoc{"peek", "memory", "peek(addr, [n])", "read n bytes"},
    ApiDoc{"poke", "memory", "poke(addr, v, ...)", "write bytes"},
    ApiDoc{"peek2", "memory", "peek2(addr)", "read unsigned 16-bit"},
    ApiDoc{"poke2", "memory", "poke2(addr, v)", "write 16-bit"},
    ApiDoc{"peek4", "memory", "peek4(addr)", "read signed 32-bit"},
    ApiDoc{"poke4", "memory", "poke4(addr, v)", "write 32-bit"},
    ApiDoc{"memcpy", "memory", "memcpy(dst, src, len)", "copy bytes"},
    ApiDoc{"memset", "memory", "memset(dst, value, len)", "fill bytes"},
    ApiDoc{"dget", "memory", "dget(slot)", "read persistent number 0-63"},
    ApiDoc{"dset", "memory", "dset(slot, value)", "save persistent number"},
    // Math
    ApiDoc{"flr", "math", "flr(x)", "round down"},
    ApiDoc{"ceil", "math", "ceil(x)", "round up"},
    ApiDoc{"abs", "math", "abs(x)", "absolute value"},
    ApiDoc{"sqrt", "math", "sqrt(x)", "square root"},
    ApiDoc{"min", "math", "min(a, b)", "smaller value"},
    ApiDoc{"max", "math", "max(a, b)", "larger value"},
    ApiDoc{"mid", "math", "mid(a, b, c)", "middle value"},
    ApiDoc{"clamp", "math", "clamp(v, lo, hi)", "limit v to a range"},
    ApiDoc{"lerp", "math", "lerp(a, b, t)", "linear interpolation"},
    ApiDoc{"sgn", "math", "sgn(x)", "-1 or 1"},
    ApiDoc{"sin", "math", "sin(t)", "sine, t in turns"},
    ApiDoc{"cos", "math", "cos(t)", "cosine, t in turns"},
    ApiDoc{"atan2", "math", "atan2(dx, dy)", "angle in turns"},
    ApiDoc{"rnd", "math", "rnd([x])", "random in [0, x), or table item"},
    ApiDoc{"srand", "math", "srand(seed)", "seed the random generator"},
    // Tables and strings
    ApiDoc{"add", "tables", "add(t, v, [i])", "append or insert"},
    ApiDoc{"del", "tables", "del(t, v)", "remove first v"},
    ApiDoc{"deli", "tables", "deli(t, [i])", "remove at index"},
    ApiDoc{"count", "tables", "count(t, [v])", "length or occurrences"},
    ApiDoc{"all", "tables", "all(t)", "iterate values, del-safe"},
    ApiDoc{"foreach", "tables", "foreach(t, f)", "call f for each value"},
    ApiDoc{"split", "tables", "split(s, [sep], [convert])", "split a string"},
    ApiDoc{"sub", "tables", "sub(s, i, [j])", "substring"},
    ApiDoc{"chr", "tables", "chr(...)", "characters from codes"},
    ApiDoc{"ord", "tables", "ord(s, [i], [n])", "character codes"},
    ApiDoc{"tostr", "tables", "tostr(v, [hex])", "value as text"},
    ApiDoc{"tonum", "tables", "tonum(v)", "number from text"},
    // System
    ApiDoc{"time", "system", "time()", "seconds since start"},
    ApiDoc{"t", "system", "t()", "seconds since start"},
    ApiDoc{"stat", "system", "stat(n)", "system info: 1 is cpu"},
    ApiDoc{"printh", "system", "printh(...)", "print to the host terminal"},
    ApiDoc{"trace", "system", "trace([msg])", "stack traceback"},
    // Callbacks
    ApiDoc{"_init", "callbacks", "function _init()", "runs once at start"},
    ApiDoc{"_update", "callbacks", "function _update()", "runs every frame"},
    ApiDoc{"_draw", "callbacks", "function _draw()", "draws every frame"},
};

}  // namespace

std::span<const ApiDoc> api_docs() { return kDocs; }

const ApiDoc* find_api(std::string_view name) {
  for (const auto& d : kDocs)
    if (d.name == name) return &d;
  return nullptr;
}

}  // namespace glim::editors
