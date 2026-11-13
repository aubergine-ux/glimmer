#include "editors/clipboard.hpp"

#include <cstdio>

namespace glim::editors {

namespace {

constexpr std::string_view kBase32 = "0123456789abcdefghijklmnopqrstuv";
constexpr std::string_view kHex = "0123456789abcdef";
constexpr int kMaxClipSide = 512;

int digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'v') return c - 'a' + 10;
  return -1;
}

std::vector<std::string_view> lines(std::string_view text) {
  std::vector<std::string_view> out;
  while (!text.empty()) {
    auto nl = text.find('\n');
    auto line = text.substr(0, nl);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) out.push_back(line);
    if (nl == std::string_view::npos) break;
    text.remove_prefix(nl + 1);
  }
  return out;
}

// Parses "glimmer <kind> W H"; W and H are optional for byte payloads.
bool header(std::string_view line, std::string_view kind, int* w, int* h) {
  std::string prefix = "glimmer " + std::string(kind);
  if (!line.starts_with(prefix)) return false;
  if (!w) return line.size() == prefix.size();
  return std::sscanf(std::string(line.substr(prefix.size())).c_str(), "%d %d", w, h) == 2 && *w > 0 && *h > 0 &&
         *w <= kMaxClipSide && *h <= kMaxClipSide;
}

}  // namespace

std::string encode_pixels(const PixelClip& clip) {
  std::string out = "glimmer pixels " + std::to_string(clip.w) + " " + std::to_string(clip.h) + "\n";
  for (int y = 0; y < clip.h; ++y) {
    for (int x = 0; x < clip.w; ++x) out += kBase32[clip.px[size_t(y) * clip.w + x] & 31];
    out += '\n';
  }
  return out;
}

bool decode_pixels(std::string_view text, PixelClip& out) {
  auto ls = lines(text);
  if (ls.empty() || !header(ls[0], "pixels", &out.w, &out.h) || int(ls.size()) != out.h + 1) return false;
  out.px.assign(size_t(out.w) * out.h, 0);
  for (int y = 0; y < out.h; ++y) {
    if (int(ls[y + 1].size()) != out.w) return false;
    for (int x = 0; x < out.w; ++x) {
      int d = digit(ls[y + 1][x]);
      if (d < 0) return false;
      out.px[size_t(y) * out.w + x] = uint8_t(d);
    }
  }
  return true;
}

std::string encode_cells(const CellClip& clip) {
  std::string out = "glimmer cells " + std::to_string(clip.w) + " " + std::to_string(clip.h) + "\n";
  for (int y = 0; y < clip.h; ++y) {
    for (int x = 0; x < clip.w; ++x) {
      uint16_t v = clip.cells[size_t(y) * clip.w + x];
      for (int s = 12; s >= 0; s -= 4) out += kHex[(v >> s) & 15];
    }
    out += '\n';
  }
  return out;
}

bool decode_cells(std::string_view text, CellClip& out) {
  auto ls = lines(text);
  if (ls.empty() || !header(ls[0], "cells", &out.w, &out.h) || int(ls.size()) != out.h + 1) return false;
  out.cells.assign(size_t(out.w) * out.h, 0);
  for (int y = 0; y < out.h; ++y) {
    if (int(ls[y + 1].size()) != out.w * 4) return false;
    for (int x = 0; x < out.w; ++x) {
      uint16_t v = 0;
      for (int k = 0; k < 4; ++k) {
        int d = digit(ls[y + 1][x * 4 + k]);
        if (d < 0 || d > 15) return false;
        v = uint16_t((v << 4) | d);
      }
      out.cells[size_t(y) * out.w + x] = v;
    }
  }
  return true;
}

std::string encode_bytes(std::string_view kind, const std::vector<uint8_t>& bytes) {
  std::string out = "glimmer " + std::string(kind) + "\n";
  for (uint8_t b : bytes) {
    out += kHex[b >> 4];
    out += kHex[b & 15];
  }
  out += '\n';
  return out;
}

bool decode_bytes(std::string_view text, std::string_view kind, std::vector<uint8_t>& out) {
  auto ls = lines(text);
  if (ls.size() != 2 || !header(ls[0], kind, nullptr, nullptr) || ls[1].size() % 2) return false;
  out.clear();
  for (size_t i = 0; i < ls[1].size(); i += 2) {
    int hi = digit(ls[1][i]), lo = digit(ls[1][i + 1]);
    if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return false;
    out.push_back(uint8_t(hi << 4 | lo));
  }
  return true;
}

}  // namespace glim::editors
