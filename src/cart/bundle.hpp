#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cart/cart.hpp"

namespace glim::cart {

// Standalone games are a copy of the Glimmer runtime with a cart attached:
// on Windows and Linux the packed cart is appended to the executable, on
// macOS it goes into the app bundle's Resources as game.glim. A runtime
// that finds a cart this way starts straight into it.
LoadResult bundle(const std::string& runtime, const Cart& cart, const std::string& out);
bool find_appended(std::span<const uint8_t> exe, Cart& out);
// Looks for a cart attached to the running program.
bool find_bundled(const std::string& exe_path, const std::string& base_dir, Cart& out);

std::string base64url_encode(std::span<const uint8_t> data);
bool base64url_decode(std::string_view text, std::vector<uint8_t>& out);

}  // namespace glim::cart
