#pragma once

#include <string>

#include "cart/cart.hpp"

namespace glim::cart {

// Where exported HTML games get the Glimmer runtime: a local single-file
// glimmer_player.js to inline (fully offline games), or the hosted copy.
void set_web_runtime(const std::string& path);
inline constexpr const char* kHostedRuntime = "https://armd.one/glimmer/play/glimmer_player.js";

// A single, self-contained HTML page that plays the cart, touch controls included.
LoadResult to_html(const Cart& cart, std::string& out);

}  // namespace glim::cart
