#include "cart/cart.hpp"

#include <algorithm>
#include <cstring>

#include "cart/image.hpp"
#include "gfx/palette.hpp"

namespace glim::cart {

namespace {

constexpr std::string_view kTextMagic = "glimmer cart";
constexpr int kTextFormat = 1;
constexpr std::string_view kBase32 = "0123456789abcdefghijklmnopqrstuv";
constexpr std::string_view kHex = "0123456789abcdef";
constexpr uint8_t kBlankSfxSpeed = 16;
constexpr uint8_t kPatternDisabled = 0x40;

int digit_value(char c, int base) {
  int v = -1;
  if (c >= '0' && c <= '9') v = c - '0';
  else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
  else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
  return v < base ? v : -1;
}

void put_hex(std::string& out, uint32_t v, int digits) {
  for (int i = digits - 1; i >= 0; --i) out += kHex[(v >> (i * 4)) & 15];
}

bool is_header(std::string_view line) {
  if (line.size() < 3 || line.front() != '[' || line.back() != ']') return false;
  for (char c : line.substr(1, line.size() - 2))
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

// A row-oriented section: fixed number of rows, each a fixed count of fixed-width fields.
struct Grid {
  std::string_view name;
  std::vector<uint8_t> Cart::*field;
  size_t offset;      // byte offset into the field
  int rows;
  int cols;           // fields per row
  int field_bytes;    // 1 or 2, little endian
  int digits;         // characters per field
  int base;           // 16 or 32
};

std::string blank_row(const Cart& blank, const Grid& g, int row) {
  std::string s;
  const auto& data = blank.*(g.field);
  for (int c = 0; c < g.cols; ++c) {
    size_t i = g.offset + (size_t(row) * g.cols + c) * g.field_bytes;
    uint32_t v = data[i] | (g.field_bytes == 2 ? data[i + 1] << 8 : 0);
    if (g.base == 32) s += kBase32[v & 31];
    else put_hex(s, v, g.digits);
  }
  return s;
}

std::vector<Grid> grids() {
  std::vector<Grid> g = {
      {"gfx", &Cart::gfx, 0, kSheetH * kSheets, kSheetW, 1, 1, 32},
      {"flags", &Cart::flags, 0, kSprites / 32, 32, 1, 2, 16},
  };
  static const char* kMapNames[] = {"map0", "map1", "map2", "map3"};
  for (int l = 0; l < kMapLayers; ++l)
    g.push_back({kMapNames[l], &Cart::map, size_t(l) * addr::kMapLayerSize, kMapH, kMapW, 2, 4, 16});
  g.push_back({"sfx", &Cart::sfx, 0, kSfxCount, kSfxBytes, 1, 2, 16});
  g.push_back({"wave", &Cart::wave, 0, kWavetables, kWaveSamples, 1, 2, 16});
  g.push_back({"music", &Cart::music, 0, kPatterns, kChannels, 1, 2, 16});
  g.push_back({"palette", &Cart::palette, 0, 1, kColors * 3, 1, 2, 16});
  g.push_back({"label", &Cart::label, 0, kScreenH, kScreenW, 1, 1, 32});
  return g;
}

}  // namespace

Cart::Cart()
    : gfx(addr::kGfxSize, 0),
      flags(kSprites, 0),
      map(addr::kMapSize, 0),
      sfx(addr::kSfxSize, 0),
      wave(addr::kWaveSize, 0),
      music(addr::kMusicSize, kPatternDisabled),
      palette(addr::kPaletteSize, 0) {
  for (int i = 0; i < kSfxCount; ++i) sfx[i * kSfxBytes] = kBlankSfxSpeed;
  for (int i = 0; i < kColors; ++i) {
    palette[i * 3 + 0] = uint8_t(gfx::kDefaultPalette[i] >> 16);
    palette[i * 3 + 1] = uint8_t(gfx::kDefaultPalette[i] >> 8);
    palette[i * 3 + 2] = uint8_t(gfx::kDefaultPalette[i]);
  }
  set_meta("spec", std::to_string(kSpecVersion));
}

std::string Cart::get_meta(std::string_view key, std::string_view fallback) const {
  for (const auto& [k, v] : meta)
    if (k == key) return v;
  return std::string(fallback);
}

void Cart::set_meta(std::string_view key, std::string_view value) {
  for (auto& [k, v] : meta) {
    if (k == key) {
      v = value;
      return;
    }
  }
  meta.emplace_back(key, value);
}

int Cart::spec_version() const {
  auto v = get_meta("spec", "1");
  int n = 0;
  for (char c : v) {
    if (c < '0' || c > '9') break;
    n = n * 10 + (c - '0');
  }
  return n ? n : 1;
}

void Cart::load_into(Memory& mem) const {
  auto put = [&](uint32_t a, const std::vector<uint8_t>& v, size_t len) {
    std::memcpy(mem.data() + a, v.data(), std::min(len, v.size()));
  };
  put(addr::kGfx, gfx, addr::kGfxSize);
  put(addr::kFlags, flags, kSprites);
  put(addr::kMap, map, addr::kMapSize);
  put(addr::kSfx, sfx, addr::kSfxSize);
  put(addr::kWave, wave, addr::kWaveSize);
  put(addr::kMusic, music, addr::kMusicSize);
  put(addr::kPalette, palette, addr::kPaletteSize);
}

void Cart::capture_from(const Memory& mem) {
  auto get = [&](uint32_t a, std::vector<uint8_t>& v, size_t len) {
    auto r = mem.region(a, uint32_t(len));
    v.assign(r.begin(), r.end());
  };
  get(addr::kGfx, gfx, addr::kGfxSize);
  get(addr::kFlags, flags, kSprites);
  get(addr::kMap, map, addr::kMapSize);
  get(addr::kSfx, sfx, addr::kSfxSize);
  get(addr::kWave, wave, addr::kWaveSize);
  get(addr::kMusic, music, addr::kMusicSize);
  get(addr::kPalette, palette, addr::kPaletteSize);
}

std::string to_text(const Cart& cart) {
  static const Cart blank;
  std::string out;
  out += kTextMagic;
  out += ' ';
  out += std::to_string(kTextFormat);
  out += '\n';

  out += "[meta]\n";
  for (const auto& [k, v] : cart.meta) out += k + " = " + v + "\n";

  if (!cart.code.empty()) {
    out += "[code]\n";
    size_t start = 0;
    for (;;) {
      size_t nl = cart.code.find('\n', start);
      std::string_view line = std::string_view(cart.code).substr(start, nl - start);
      // Escape lines that could be mistaken for section headers.
      if (!line.empty() && (line[0] == '[' || line[0] == '\\')) out += '\\';
      out += line;
      out += '\n';
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
  }

  for (const auto& g : grids()) {
    const auto& data = cart.*(g.field);
    if (g.name == "label" && data.empty()) continue;
    std::vector<std::string> rows;
    int last = -1;
    for (int r = 0; r < g.rows; ++r) {
      std::string s;
      for (int c = 0; c < g.cols; ++c) {
        size_t i = g.offset + (size_t(r) * g.cols + c) * g.field_bytes;
        uint32_t v = data[i] | (g.field_bytes == 2 ? data[i + 1] << 8 : 0);
        if (g.base == 32) s += kBase32[v & 31];
        else put_hex(s, v, g.digits);
      }
      bool same = g.name != "label" && s == blank_row(blank, g, r);
      if (!same) last = r;
      rows.push_back(std::move(s));
    }
    if (last < 0) continue;
    out += "[" + std::string(g.name) + "]\n";
    for (int r = 0; r <= last; ++r) out += rows[r] + "\n";
  }

  for (const auto& [name, body] : cart.extra) {
    out += "[" + name + "]\n" + body;
    if (!body.empty() && body.back() != '\n') out += '\n';
  }
  return out;
}

LoadResult from_text(std::string_view text, Cart& out) {
  out = Cart();
  out.meta.clear();
  auto all_grids = grids();

  std::vector<std::string_view> lines;
  for (size_t start = 0; start <= text.size();) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) nl = text.size();
    auto line = text.substr(start, nl - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    lines.push_back(line);
    start = nl + 1;
  }
  // A trailing newline produces one empty final element that isn't part of any section.
  if (!lines.empty() && lines.back().empty()) lines.pop_back();

  size_t i = 0;
  while (i < lines.size() && lines[i].empty()) ++i;
  if (i == lines.size() || !lines[i].starts_with(kTextMagic)) return {false, "not a glimmer cart"};
  ++i;

  std::string section;
  const Grid* grid = nullptr;
  int grid_row = 0;
  std::vector<std::string_view> code_lines;
  std::string* extra_body = nullptr;
  bool seen_label = false;

  for (; i < lines.size(); ++i) {
    auto line = lines[i];
    int lineno = int(i) + 1;
    if (is_header(line)) {
      section = std::string(line.substr(1, line.size() - 2));
      grid = nullptr;
      extra_body = nullptr;
      grid_row = 0;
      for (const auto& g : all_grids)
        if (g.name == section) grid = &g;
      if (section == "label") {
        out.label.assign(addr::kScreenSize, 0);
        seen_label = true;
      }
      if (!grid && section != "meta" && section != "code") {
        out.extra.emplace_back(section, "");
        extra_body = &out.extra.back().second;
      }
      continue;
    }
    if (section == "code") {
      if (!line.empty() && line[0] == '\\') line.remove_prefix(1);
      code_lines.push_back(line);
    } else if (section == "meta") {
      auto eq = line.find('=');
      if (eq == std::string_view::npos) continue;
      auto trim = [](std::string_view s) {
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
        return s;
      };
      out.set_meta(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    } else if (grid) {
      if (line.empty()) continue;
      if (grid_row >= grid->rows) return {false, "line " + std::to_string(lineno) + ": too many rows in [" + section + "]"};
      if (int(line.size()) != grid->cols * grid->digits)
        return {false, "line " + std::to_string(lineno) + ": bad row length in [" + section + "]"};
      auto& data = out.*(grid->field);
      for (int c = 0; c < grid->cols; ++c) {
        uint32_t v = 0;
        for (int d = 0; d < grid->digits; ++d) {
          int dv = digit_value(line[c * grid->digits + d], grid->base);
          if (dv < 0) return {false, "line " + std::to_string(lineno) + ": bad digit in [" + section + "]"};
          v = v * grid->base + dv;
        }
        size_t idx = grid->offset + (size_t(grid_row) * grid->cols + c) * grid->field_bytes;
        data[idx] = uint8_t(v);
        if (grid->field_bytes == 2) data[idx + 1] = uint8_t(v >> 8);
      }
      ++grid_row;
    } else if (extra_body) {
      *extra_body += line;
      *extra_body += '\n';
    }
  }

  for (size_t k = 0; k < code_lines.size(); ++k) {
    if (k) out.code += '\n';
    out.code += code_lines[k];
  }
  if (out.code.size() > kMaxCodeBytes) return {false, "code is larger than 128 KiB"};
  if (!seen_label) out.label.clear();
  if (out.get_meta("spec").empty()) out.set_meta("spec", "1");
  return {true, {}};
}

LoadResult load_bytes(std::span<const uint8_t> data, Cart& out) {
  static constexpr uint8_t kPngSig[] = {0x89, 'P', 'N', 'G'};
  if (data.size() >= 4 && std::equal(kPngSig, kPngSig + 4, data.begin())) return from_png(data, out);
  if (data.size() >= 4 && std::memcmp(data.data(), "GLMZ", 4) == 0) return unpack(data, out);
  return from_text(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), out);
}

LoadResult load_file(const std::string& path, Cart& out) {
  std::vector<uint8_t> bytes;
  if (!read_file(path, bytes)) return {false, "can't read " + path};
  // A bare .lua file is a code-only cart, which is the easiest way to start.
  if (path.ends_with(".lua")) {
    out = Cart();
    out.code.assign(bytes.begin(), bytes.end());
    if (out.code.size() > kMaxCodeBytes) return {false, "code is larger than 128 KiB"};
    return {true, {}};
  }
  return load_bytes(bytes, out);
}

LoadResult save_file(const std::string& path, const Cart& cart) {
  std::vector<uint8_t> bytes;
  if (path.ends_with(".png")) {
    auto r = to_png(cart, bytes);
    if (!r.ok) return r;
  } else {
    auto text = to_text(cart);
    bytes.assign(text.begin(), text.end());
  }
  if (!write_file(path, bytes)) return {false, "can't write " + path};
  return {true, {}};
}

}  // namespace glim::cart
