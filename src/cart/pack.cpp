#include <algorithm>
#include <cstring>

#include <miniz.h>

#include "cart/cart.hpp"
#include "cart/image.hpp"
#include "gfx/font.hpp"

namespace glim::cart {

namespace {

constexpr uint16_t kPackFormat = 1;
constexpr uint32_t kMaxUnpacked = 4u << 20;
constexpr char kStegoMagic[4] = {'G', 'L', 'M', '1'};
constexpr int kStegoHeader = 12;

void put32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (i * 8)));
}
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

void chunk(std::vector<uint8_t>& out, const char tag[4], std::span<const uint8_t> data) {
  out.insert(out.end(), tag, tag + 4);
  put32(out, uint32_t(data.size()));
  out.insert(out.end(), data.begin(), data.end());
}

std::span<const uint8_t> bytes_of(std::string_view s) {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

std::string kv_lines(const std::vector<std::pair<std::string, std::string>>& kv) {
  std::string s;
  for (const auto& [k, v] : kv) s += k + "=" + v + "\n";
  return s;
}

}  // namespace

std::vector<uint8_t> pack(const Cart& cart) {
  std::vector<uint8_t> raw = {'G', 'L', 'I', 'M'};
  raw.push_back(uint8_t(kPackFormat));
  raw.push_back(uint8_t(kPackFormat >> 8));
  raw.push_back(uint8_t(cart.spec_version()));
  raw.push_back(uint8_t(cart.spec_version() >> 8));
  chunk(raw, "META", bytes_of(kv_lines(cart.meta)));
  chunk(raw, "CODE", bytes_of(cart.code));
  chunk(raw, "GFX ", cart.gfx);
  chunk(raw, "FLAG", cart.flags);
  chunk(raw, "MAP ", cart.map);
  chunk(raw, "SFX ", cart.sfx);
  chunk(raw, "WAVE", cart.wave);
  chunk(raw, "MUSC", cart.music);
  chunk(raw, "PAL ", cart.palette);
  if (!cart.label.empty()) chunk(raw, "LABL", cart.label);
  // Unknown text sections round-trip as "name\nbody" chunks.
  for (const auto& [name, body] : cart.extra) chunk(raw, "XTRA", bytes_of(name + "\n" + body));

  mz_ulong bound = mz_compressBound(mz_ulong(raw.size()));
  std::vector<uint8_t> out = {'G', 'L', 'M', 'Z'};
  put32(out, uint32_t(raw.size()));
  size_t head = out.size();
  out.resize(head + bound);
  mz_ulong len = bound;
  if (mz_compress2(out.data() + head, &len, raw.data(), mz_ulong(raw.size()), MZ_BEST_COMPRESSION) != MZ_OK)
    return {};
  out.resize(head + len);
  return out;
}

LoadResult unpack(std::span<const uint8_t> data, Cart& out) {
  if (data.size() < 8 || std::memcmp(data.data(), "GLMZ", 4) != 0) return {false, "not a packed cart"};
  uint32_t raw_size = get32(data.data() + 4);
  if (raw_size > kMaxUnpacked) return {false, "packed cart is too large"};
  std::vector<uint8_t> raw(raw_size);
  mz_ulong len = raw_size;
  if (mz_uncompress(raw.data(), &len, data.data() + 8, mz_ulong(data.size() - 8)) != MZ_OK || len != raw_size)
    return {false, "packed cart is corrupt"};
  if (raw.size() < 8 || std::memcmp(raw.data(), "GLIM", 4) != 0) return {false, "packed cart is corrupt"};

  out = Cart();
  out.meta.clear();
  size_t p = 8;
  while (p + 8 <= raw.size()) {
    std::string_view tag(reinterpret_cast<const char*>(raw.data() + p), 4);
    uint32_t n = get32(raw.data() + p + 4);
    p += 8;
    if (n > raw.size() - p) return {false, "packed cart is truncated"};
    std::span<const uint8_t> body(raw.data() + p, n);
    std::string_view text(reinterpret_cast<const char*>(body.data()), n);
    auto fill = [&](std::vector<uint8_t>& v) { std::copy_n(body.begin(), std::min(body.size(), v.size()), v.begin()); };
    if (tag == "META") {
      while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        if (auto eq = line.find('='); eq != std::string_view::npos) out.set_meta(line.substr(0, eq), line.substr(eq + 1));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
      }
    } else if (tag == "CODE") {
      out.code.assign(text);
    } else if (tag == "GFX ") {
      fill(out.gfx);
    } else if (tag == "FLAG") {
      fill(out.flags);
    } else if (tag == "MAP ") {
      fill(out.map);
    } else if (tag == "SFX ") {
      fill(out.sfx);
    } else if (tag == "WAVE") {
      fill(out.wave);
    } else if (tag == "MUSC") {
      fill(out.music);
    } else if (tag == "PAL ") {
      fill(out.palette);
    } else if (tag == "LABL") {
      out.label.assign(addr::kScreenSize, 0);
      fill(out.label);
    } else if (tag == "XTRA") {
      auto nl = text.find('\n');
      if (nl != std::string_view::npos) out.extra.emplace_back(text.substr(0, nl), text.substr(nl + 1));
    }
    p += n;
  }
  if (out.get_meta("spec").empty()) out.set_meta("spec", "1");
  return {true, {}};
}

namespace {

// Cartridge artwork, drawn in the cart's own palette so it matches the game.
class Cover {
 public:
  Cover(const Cart& cart) : img_(kPngWidth, kPngHeight), cart_(cart) {}

  void rect(int x0, int y0, int x1, int y1, int c) {
    for (int y = std::max(0, y0); y <= std::min(kPngHeight - 1, y1); ++y)
      for (int x = std::max(0, x0); x <= std::min(kPngWidth - 1, x1); ++x) set(x, y, c);
  }

  void rounded(int x0, int y0, int x1, int y1, int r, int c) {
    for (int y = y0; y <= y1; ++y) {
      int inset = 0;
      int dy = std::max(0, std::max(y0 + r - y, y - (y1 - r)));
      while (inset < r && (r - inset) * (r - inset) + dy * dy > r * r) ++inset;
      if (dy == 0) inset = 0;
      rect(x0 + inset, y, x1 - inset, y, c);
    }
  }

  void text(std::string_view s, int x, int y, int scale, int c) {
    for (char ch : s) {
      for (int row = 0; row < gfx::kGlyphRows; ++row) {
        uint8_t bits = gfx::glyph_row(uint8_t(ch), row);
        for (int col = 0; col < gfx::kGlyphW; ++col)
          if ((bits >> (gfx::kGlyphW - 1 - col)) & 1)
            rect(x + col * scale, y + row * scale, x + (col + 1) * scale - 1, y + (row + 1) * scale - 1, c);
      }
      x += gfx::kCharW * scale;
    }
  }

  void label(int x0, int y0) {
    if (cart_.label.size() == addr::kScreenSize) {
      for (int y = 0; y < kScreenH; ++y)
        for (int x = 0; x < kScreenW; ++x) set(x0 + x, y0 + y, cart_.label[y * kScreenW + x]);
      return;
    }
    // No label yet: a starfield placeholder rather than a blank hole.
    rect(x0, y0, x0 + kScreenW - 1, y0 + kScreenH - 1, 1);
    uint32_t seed = 0x9E3779B9u;
    for (int i = 0; i < 90; ++i) {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      set(x0 + int(seed % kScreenW), y0 + int((seed >> 8) % kScreenH), i % 3 ? 7 : 5);
    }
    text("?", x0 + kScreenW / 2 - 6, y0 + kScreenH / 2 - 7, 3, 4);
  }

  Image& image() { return img_; }

 private:
  void set(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= kPngWidth || y >= kPngHeight) return;
    uint8_t* px = img_.at(x, y);
    const uint8_t* rgb = cart_.palette.data() + (c & 31) * 3;
    px[0] = rgb[0];
    px[1] = rgb[1];
    px[2] = rgb[2];
    px[3] = 255;
  }

  Image img_;
  const Cart& cart_;
};

std::string fit(std::string s, size_t max) {
  if (s.size() > max) s = s.substr(0, max - 1) + ".";
  return s;
}

}  // namespace

LoadResult to_png(const Cart& cart, std::vector<uint8_t>& out) {
  auto packed = pack(cart);
  if (packed.empty()) return {false, "compression failed"};
  if (packed.size() > kPngCapacity)
    return {false, "cart is " + std::to_string(packed.size()) + " bytes packed; PNG carts hold " +
                       std::to_string(kPngCapacity)};

  Cover cover(cart);
  cover.rect(0, 0, kPngWidth - 1, kPngHeight - 1, 0);
  cover.rounded(10, 4, 309, 235, 8, 3);  // light top edge shows as a bevel
  cover.rounded(10, 7, 309, 235, 8, 2);
  cover.rect(24, 14, 295, 15, 1);
  cover.text("GLIMMER", 24, 18, 2, 5);
  cover.text("\x86", 286, 18, 2, 17);
  cover.rect(28, 34, 291, 185, 1);
  cover.label(32, 38);
  cover.text(fit(cart.get_meta("title", "untitled"), 32), 32, 194, 2, 31);
  cover.text(fit(cart.get_meta("author"), 56), 32, 212, 1, 6);
  for (int i = 0; i < 4; ++i) cover.rect(260, 210 + i * 5, 291, 211 + i * 5, 1);  // grip ridges

  Image& img = cover.image();
  std::vector<uint8_t> stream(kStegoMagic, kStegoMagic + 4);
  put32(stream, uint32_t(packed.size()));
  put32(stream, uint32_t(mz_crc32(MZ_CRC32_INIT, packed.data(), packed.size())));
  stream.insert(stream.end(), packed.begin(), packed.end());
  for (size_t i = 0; i < stream.size(); ++i) {
    uint8_t* px = img.rgba.data() + i * 4;
    uint8_t b = stream[i];
    for (int ch = 0; ch < 4; ++ch) px[ch] = uint8_t((px[ch] & ~3) | ((b >> (6 - ch * 2)) & 3));
  }
  out = encode_png(img);
  if (out.empty()) return {false, "PNG encoding failed"};
  return {true, {}};
}

LoadResult from_png(std::span<const uint8_t> png, Cart& out) {
  Image img;
  if (!decode_png(png, img)) return {false, "not a readable PNG"};
  size_t pixels = size_t(img.w) * img.h;
  auto byte_at = [&](size_t i) {
    const uint8_t* px = img.rgba.data() + i * 4;
    return uint8_t(((px[0] & 3) << 6) | ((px[1] & 3) << 4) | ((px[2] & 3) << 2) | (px[3] & 3));
  };
  if (pixels < kStegoHeader) return {false, "PNG has no cart data"};
  uint8_t head[kStegoHeader];
  for (int i = 0; i < kStegoHeader; ++i) head[i] = byte_at(i);
  if (std::memcmp(head, kStegoMagic, 4) != 0) return {false, "PNG has no cart data"};
  uint32_t len = get32(head + 4), crc = get32(head + 8);
  if (len > pixels - kStegoHeader) return {false, "PNG cart data is truncated"};
  std::vector<uint8_t> packed(len);
  for (uint32_t i = 0; i < len; ++i) packed[i] = byte_at(kStegoHeader + i);
  if (uint32_t(mz_crc32(MZ_CRC32_INIT, packed.data(), packed.size())) != crc)
    return {false, "PNG cart data is damaged (was the image resized or recompressed?)"};
  return unpack(packed, out);
}

}  // namespace glim::cart
