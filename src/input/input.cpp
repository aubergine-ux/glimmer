#include "input/input.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace glim::input {

namespace {

constexpr int kRepeatDelay = 15;  // frames before btnp starts repeating
constexpr int kRepeatRate = 4;
constexpr int kKeyRepeatDelay = 24;  // text editing repeats faster than buttons
constexpr int kKeyRepeatRate = 2;

struct NamedKey {
  std::string_view name;
  Key key;
};

// Named keys outside the contiguous a-z / 0-9 / f1-f12 ranges.
constexpr NamedKey kNamedKeys[] = {
    {"return", 40},  {"escape", 41},    {"backspace", 42}, {"tab", 43},       {"space", 44},
    {"-", 45},       {"=", 46},         {"[", 47},         {"]", 48},         {"\\", 49},
    {";", 51},       {"'", 52},         {"`", 53},         {",", 54},         {".", 55},
    {"/", 56},       {"capslock", 57},  {"insert", 73},    {"home", 74},      {"pageup", 75},
    {"delete", 76},  {"end", 77},       {"pagedown", 78},  {"right", 79},     {"left", 80},
    {"down", 81},    {"up", 82},        {"lctrl", 224},    {"lshift", 225},   {"lalt", 226},
    {"lgui", 227},   {"rctrl", 228},    {"rshift", 229},   {"ralt", 230},     {"rgui", 231},
};

constexpr std::string_view kButtonNames[kButtonCount] = {"left", "right", "up", "down", "a",
                                                         "b",    "x",     "y",  "l",    "r"};

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(uint8_t(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(uint8_t(s.back()))) s.remove_suffix(1);
  return s;
}

}  // namespace

Key key_from_name(std::string_view name) {
  if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return Key(4 + (name[0] - 'a'));
  if (name.size() == 1 && name[0] >= '1' && name[0] <= '9') return Key(30 + (name[0] - '1'));
  if (name == "0") return 39;
  if (name.size() >= 2 && name[0] == 'f') {
    int n = 0;
    for (char c : name.substr(1)) {
      if (c < '0' || c > '9') return 0;
      n = n * 10 + (c - '0');
    }
    return (n >= 1 && n <= 12) ? Key(58 + n - 1) : 0;
  }
  for (const auto& k : kNamedKeys)
    if (k.name == name) return k.key;
  return 0;
}

std::string_view key_name(Key k) {
  static const std::array<std::string, kKeyCount> names = [] {
    std::array<std::string, kKeyCount> n{};
    for (int i = 0; i < 26; ++i) n[4 + i] = std::string(1, char('a' + i));
    for (int i = 0; i < 9; ++i) n[30 + i] = std::string(1, char('1' + i));
    n[39] = "0";
    for (int i = 0; i < 12; ++i) n[58 + i] = "f" + std::to_string(i + 1);
    for (const auto& nk : kNamedKeys) n[nk.key] = std::string(nk.name);
    return n;
  }();
  return k < kKeyCount ? std::string_view(names[k]) : std::string_view();
}

int button_from_name(std::string_view name) {
  for (int i = 0; i < kButtonCount; ++i)
    if (kButtonNames[i] == name) return i;
  return -1;
}

std::string_view button_name(int b) { return (b >= 0 && b < kButtonCount) ? kButtonNames[b] : ""; }

void KeyMap::reset() {
  bindings_.clear();
  auto add = [this](int p, int b, std::string_view k) { bind(p, b, key_from_name(k)); };
  add(0, kLeft, "left");
  add(0, kRight, "right");
  add(0, kUp, "up");
  add(0, kDown, "down");
  add(0, kA, "z");
  add(0, kB, "x");
  add(0, kX, "a");
  add(0, kY, "s");
  add(0, kL, "q");
  add(0, kR, "w");
  add(1, kLeft, "j");
  add(1, kRight, "l");
  add(1, kUp, "i");
  add(1, kDown, "k");
  add(1, kA, "n");
  add(1, kB, "m");
  add(1, kX, "u");
  add(1, kY, "o");
  add(1, kL, "7");
  add(1, kR, "9");
}

void KeyMap::bind(int player, int button, Key key) {
  if (!key || player < 0 || player >= kPlayers || button < 0 || button >= kButtonCount) return;
  bindings_.push_back({key, uint8_t(player), uint8_t(button)});
}

void KeyMap::clear(int player, int button) {
  std::erase_if(bindings_, [&](const Binding& b) { return b.player == player && b.button == button; });
}

void KeyMap::load(std::string_view text) {
  bool cleared = false;
  while (!text.empty()) {
    auto nl = text.find('\n');
    auto line = trim(text.substr(0, nl));
    text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
    if (auto hash = line.find('#'); hash != std::string_view::npos) line = trim(line.substr(0, hash));
    auto eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    auto lhs = trim(line.substr(0, eq)), rhs = trim(line.substr(eq + 1));
    auto dot = lhs.find('.');
    if (lhs.size() < 4 || lhs[0] != 'p' || dot != 2) continue;
    int player = lhs[1] - '1';
    int button = button_from_name(lhs.substr(3));
    if (player < 0 || player >= kPlayers || button < 0) continue;
    // A config file replaces the defaults wholesale, but only once it proves valid.
    if (!cleared) {
      bindings_.clear();
      cleared = true;
    }
    while (!rhs.empty()) {
      auto comma = rhs.find(',');
      auto name = trim(rhs.substr(0, comma));
      bind(player, button, key_from_name(name));
      rhs = comma == std::string_view::npos ? std::string_view() : rhs.substr(comma + 1);
    }
  }
}

std::string KeyMap::save() const {
  std::string out = "# Glimmer key bindings: p<player>.<button> = key[, key...]\n";
  for (int p = 0; p < kPlayers; ++p) {
    for (int b = 0; b < kButtonCount; ++b) {
      std::string keys;
      for (const auto& bind : bindings_) {
        if (bind.player != p || bind.button != b) continue;
        if (!keys.empty()) keys += ", ";
        keys += key_name(bind.key);
      }
      if (keys.empty()) continue;
      out += "p" + std::to_string(p + 1) + "." + std::string(button_name(b)) + " = " + keys + "\n";
    }
  }
  return out;
}

void Input::set_key(Key k, bool down) {
  if (k < kKeyCount) live_keys_[k] = down;
}

void Input::set_pad_button(int player, int button, bool down) {
  if (player < 0 || player >= kPlayers || button < 0 || button >= kButtonCount) return;
  uint16_t bit = uint16_t(1u << button);
  pad_live_[player] = down ? (pad_live_[player] | bit) : (pad_live_[player] & ~bit);
}

void Input::set_mouse(int x, int y, uint8_t buttons) {
  live_mouse_.x = x;
  live_mouse_.y = y;
  live_mouse_.buttons = buttons;
}

void Input::release_all() {
  live_keys_.fill(false);
  pad_live_.fill(0);
  live_mouse_.buttons = 0;
}

void Input::begin_frame() {
  prev_keys_ = keys_;
  keys_ = live_keys_;
  for (int k = 0; k < kKeyCount; ++k) key_frames_[k] = keys_[k] ? uint16_t(std::min(key_frames_[k] + 1, 60000)) : 0;
  prev_ = cur_;
  cur_ = pad_live_;
  for (const auto& b : keymap_.bindings())
    if (keys_[b.key]) cur_[b.player] |= uint16_t(1u << b.button);
  for (int p = 0; p < kPlayers; ++p)
    for (int b = 0; b < kButtonCount; ++b)
      held_frames_[p][b] = ((cur_[p] >> b) & 1) ? uint16_t(std::min(held_frames_[p][b] + 1, 60000)) : 0;

  prev_mouse_buttons_ = mouse_.buttons;
  mouse_ = live_mouse_;
  mouse_.wheel = std::exchange(wheel_accum_, 0);
  text_ = std::exchange(text_accum_, {});
}

bool Input::btn(int button, int player) const {
  if (player < 0 || player >= kPlayers || button < 0 || button >= kButtonCount) return false;
  return (cur_[player] >> button) & 1;
}

bool Input::btnp(int button, int player) const {
  if (!btn(button, player)) return false;
  int held = held_frames_[player][button];
  if (held == 1) return true;
  return held > kRepeatDelay && (held - kRepeatDelay - 1) % kRepeatRate == 0;
}

uint16_t Input::buttons(int player) const {
  return (player >= 0 && player < kPlayers) ? cur_[player] : 0;
}

bool Input::keyr(Key k) const {
  if (k >= kKeyCount || !keys_[k]) return false;
  int held = key_frames_[k];
  return held == 1 || (held > kKeyRepeatDelay && (held - kKeyRepeatDelay - 1) % kKeyRepeatRate == 0);
}

bool Input::ctrl() const { return keys_[224] || keys_[228] || keys_[227] || keys_[231]; }
bool Input::shift() const { return keys_[225] || keys_[229]; }
bool Input::alt() const { return keys_[226] || keys_[230]; }

bool Input::any_key_pressed() const {
  for (int k = 0; k < kKeyCount; ++k)
    if (keys_[k] && !prev_keys_[k]) return true;
  return false;
}

void Input::write_registers(Memory& mem) const {
  for (int p = 0; p < kPlayers; ++p) mem.poke16(addr::kButtons + p * 2, cur_[p]);
  mem.pokes16(addr::kMouse, mouse_.x);
  mem.pokes16(addr::kMouse + 2, mouse_.y);
  mem.poke(addr::kMouse + 4, mouse_.buttons);
  mem.poke(addr::kMouse + 5, uint8_t(int8_t(std::clamp(mouse_.wheel, -128, 127))));
}

}  // namespace glim::input
