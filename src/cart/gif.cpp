#include "cart/gif.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace glim::cart {

namespace {

constexpr int kPaletteBits = 5;
constexpr int kPaletteSize = 1 << kPaletteBits;
constexpr int kMaxCode = 4095;

void put16(std::vector<uint8_t>& out, int v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}

class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

  void put(int code, int bits) {
    acc_ |= uint32_t(code) << nbits_;
    nbits_ += bits;
    while (nbits_ >= 8) {
      push(uint8_t(acc_));
      acc_ >>= 8;
      nbits_ -= 8;
    }
  }

  void finish() {
    if (nbits_ > 0) push(uint8_t(acc_));
    flush_block();
    out_.push_back(0);  // block terminator
  }

 private:
  void push(uint8_t b) {
    block_[block_len_++] = b;
    if (block_len_ == 255) flush_block();
  }
  void flush_block() {
    if (!block_len_) return;
    out_.push_back(uint8_t(block_len_));
    out_.insert(out_.end(), block_.begin(), block_.begin() + block_len_);
    block_len_ = 0;
  }

  std::vector<uint8_t>& out_;
  std::array<uint8_t, 255> block_{};
  int block_len_ = 0;
  uint32_t acc_ = 0;
  int nbits_ = 0;
};

void lzw(const std::vector<uint8_t>& px, std::vector<uint8_t>& out) {
  const int clear = 1 << kPaletteBits, eoi = clear + 1;
  out.push_back(uint8_t(kPaletteBits));
  BitWriter bw(out);
  std::unordered_map<uint32_t, int> dict;
  dict.reserve(8192);
  int bits = kPaletteBits + 1, last = eoi;
  bw.put(clear, bits);
  int prefix = px[0];
  for (size_t i = 1; i < px.size(); ++i) {
    uint32_t key = (uint32_t(prefix) << 8) | px[i];
    if (auto it = dict.find(key); it != dict.end()) {
      prefix = it->second;
      continue;
    }
    bw.put(prefix, bits);
    dict.emplace(key, ++last);
    if (last >= (1 << bits)) ++bits;
    if (last == kMaxCode) {
      bw.put(clear, bits);
      dict.clear();
      bits = kPaletteBits + 1;
      last = eoi;
    }
    prefix = px[i];
  }
  bw.put(prefix, bits);
  bw.put(eoi, bits);
  bw.finish();
}

}  // namespace

GifWriter::GifWriter(int width, int height, int scale) : w_(width), h_(height), scale_(scale < 1 ? 1 : scale) {
  const char sig[] = "GIF89a";
  out_.insert(out_.end(), sig, sig + 6);
  put16(out_, w_ * scale_);
  put16(out_, h_ * scale_);
  out_.push_back(0);  // no global color table
  out_.push_back(0);
  out_.push_back(0);
  // NETSCAPE2.0 extension: loop forever.
  const uint8_t loop[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0, 0, 0};
  out_.insert(out_.end(), loop, loop + sizeof loop);
}

void GifWriter::add_frame(const uint32_t* rgb, int delay_cs) {
  // Bounding box of what changed since the last frame.
  int x0 = 0, y0 = 0, x1 = w_ - 1, y1 = h_ - 1;
  if (!prev_.empty()) {
    x0 = w_;
    y0 = h_;
    x1 = y1 = -1;
    for (int y = 0; y < h_; ++y)
      for (int x = 0; x < w_; ++x)
        if ((rgb[size_t(y) * w_ + x] & 0xFFFFFF) != prev_[size_t(y) * w_ + x]) {
          x0 = std::min(x0, x);
          x1 = std::max(x1, x);
          y0 = std::min(y0, y);
          y1 = std::max(y1, y);
        }
    if (x1 < 0) {
      // Nothing changed: hold the previous frame longer instead.
      last_delay_ += delay_cs;
      out_[last_delay_at_] = uint8_t(last_delay_);
      out_[last_delay_at_ + 1] = uint8_t(last_delay_ >> 8);
      return;
    }
  }
  prev_.assign(rgb, rgb + size_t(w_) * h_);
  for (auto& p : prev_) p &= 0xFFFFFF;
  int fw = x1 - x0 + 1, fh = y1 - y0 + 1;

  // Build the frame's color table. A console frame never has more than 32
  // colors; anything beyond that (it can't happen today) folds onto the last slot.
  std::array<uint32_t, kPaletteSize> table{};
  int used = 0;
  std::unordered_map<uint32_t, uint8_t> index;
  std::vector<uint8_t> src(size_t(fw) * fh);
  for (size_t i = 0; i < src.size(); ++i) {
    uint32_t c = rgb[(size_t(y0) + i / fw) * w_ + x0 + i % fw] & 0xFFFFFF;
    auto it = index.find(c);
    if (it == index.end()) {
      uint8_t slot = uint8_t(used < kPaletteSize ? used++ : kPaletteSize - 1);
      table[slot] = c;
      it = index.emplace(c, slot).first;
    }
    src[i] = it->second;
  }

  std::vector<uint8_t> px;
  px.reserve(src.size() * scale_ * scale_);
  for (int y = 0; y < fh * scale_; ++y) {
    const uint8_t* row = &src[size_t(y / scale_) * fw];
    for (int x = 0; x < fw * scale_; ++x) px.push_back(row[x / scale_]);
  }

  const uint8_t gce[] = {0x21, 0xF9, 0x04, 0x04, uint8_t(delay_cs), uint8_t(delay_cs >> 8), 0, 0};
  last_delay_at_ = out_.size() + 4;
  last_delay_ = delay_cs;
  out_.insert(out_.end(), gce, gce + sizeof gce);
  out_.push_back(0x2C);
  put16(out_, x0 * scale_);
  put16(out_, y0 * scale_);
  put16(out_, fw * scale_);
  put16(out_, fh * scale_);
  out_.push_back(uint8_t(0x80 | (kPaletteBits - 1)));  // local color table, 2^5 entries
  for (uint32_t c : table) {
    out_.push_back(uint8_t(c >> 16));
    out_.push_back(uint8_t(c >> 8));
    out_.push_back(uint8_t(c));
  }
  lzw(px, out_);
  ++frames_;
}

std::vector<uint8_t> GifWriter::finish() {
  std::vector<uint8_t> out = out_;
  out.push_back(0x3B);
  return out;
}

}  // namespace glim::cart
