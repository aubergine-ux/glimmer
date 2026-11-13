#pragma once

#include <string>
#include <vector>

namespace glim::tools {

struct Headless {
  std::string cart;
  int frames = 120;
  std::string screenshot;
  bool crt = false;  // screenshot through the CRT filter at 4x
  std::vector<std::string> presses;  // "button@from-to"
};

bool export_cart(const std::string& in, const std::string& out);
// Runs a cart without a window: used by CI to smoke-test demo carts and to
// capture screenshots for the docs.
bool run_headless(const Headless& opts);

}  // namespace glim::tools
