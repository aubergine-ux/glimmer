#include "cart/bundle.hpp"

#include <cstring>
#include <filesystem>

#include "cart/image.hpp"

namespace glim::cart {

namespace {

namespace fs = std::filesystem;
constexpr char kTrailer[8] = {'G', 'L', 'I', 'M', 'C', 'A', 'R', 'T'};
constexpr std::string_view kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

}  // namespace

LoadResult bundle(const std::string& runtime, const Cart& cart, const std::string& out) {
  std::error_code ec;
  if (fs::is_directory(runtime, ec)) {
    // A macOS .app: copy it and drop the cart into Resources.
    fs::remove_all(out, ec);
    fs::copy(runtime, out, fs::copy_options::recursive, ec);
    if (ec) return {false, "can't copy " + runtime + ": " + ec.message()};
    fs::create_directories(fs::path(out) / "Contents" / "Resources", ec);
    return save_file((fs::path(out) / "Contents" / "Resources" / "game.glim").string(), cart);
  }
  std::vector<uint8_t> exe;
  if (!read_file(runtime, exe)) return {false, "can't read " + runtime};
  // Re-bundling a game replaces its cart rather than stacking a second one.
  if (exe.size() > 16 && std::memcmp(exe.data() + exe.size() - 8, kTrailer, 8) == 0) {
    uint64_t n = 0;
    std::memcpy(&n, exe.data() + exe.size() - 16, 8);
    if (n + 16 <= exe.size()) exe.resize(exe.size() - 16 - size_t(n));
  }
  auto packed = pack(cart);
  uint64_t n = packed.size();
  exe.insert(exe.end(), packed.begin(), packed.end());
  const uint8_t* nb = reinterpret_cast<const uint8_t*>(&n);
  exe.insert(exe.end(), nb, nb + 8);
  exe.insert(exe.end(), kTrailer, kTrailer + 8);
  if (!write_file(out, exe)) return {false, "can't write " + out};
  fs::permissions(out, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
  return {true, {}};
}

bool find_appended(std::span<const uint8_t> exe, Cart& out) {
  if (exe.size() < 16 || std::memcmp(exe.data() + exe.size() - 8, kTrailer, 8) != 0) return false;
  uint64_t n = 0;
  std::memcpy(&n, exe.data() + exe.size() - 16, 8);
  if (n + 16 > exe.size()) return false;
  return unpack(exe.subspan(exe.size() - 16 - size_t(n), size_t(n)), out).ok;
}

bool find_bundled(const std::string& exe_path, const std::string& base_dir, Cart& out) {
  if (!exe_path.empty()) {
    std::vector<uint8_t> exe;
    if (read_file(exe_path, exe) && find_appended(exe, out)) return true;
  }
  std::error_code ec;
  if (!base_dir.empty() && fs::exists(fs::path(base_dir) / "game.glim", ec))
    return load_file((fs::path(base_dir) / "game.glim").string(), out).ok;
  return false;
}

std::string base64url_encode(std::span<const uint8_t> data) {
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t v = uint32_t(data[i]) << 16;
    if (i + 1 < data.size()) v |= uint32_t(data[i + 1]) << 8;
    if (i + 2 < data.size()) v |= data[i + 2];
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    if (i + 1 < data.size()) out += kB64[(v >> 6) & 63];
    if (i + 2 < data.size()) out += kB64[v & 63];
  }
  return out;
}

bool base64url_decode(std::string_view text, std::vector<uint8_t>& out) {
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    if (c == '=') break;
    if (c == '+') c = '-';
    if (c == '/') c = '_';
    auto pos = kB64.find(c);
    if (pos == std::string_view::npos) return false;
    acc = (acc << 6) | uint32_t(pos);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t(acc >> bits));
    }
  }
  return true;
}

}  // namespace glim::cart
