#pragma once

#include <string>
#include <vector>

namespace glim::tools {

struct Headless {
  std::string cart;
  int frames = 120;
  std::string screenshot;
  bool crt = false;  // screenshot through the CRT filter at 4x
  std::string record;  // GIF of the run at 2x, 30 fps
  int record_from = 0;
  std::vector<std::string> presses;  // "button@from-to"
};

bool export_cart(const std::string& in, const std::string& out);

// Drives the editors from a script of user actions (click, drag, type, key,
// shot, record...). Used to test the studio and to capture docs footage.
bool run_studio_script(const std::string& script_path, const std::string& cart_path);
// Runs a cart without a window: used by CI to smoke-test demo carts and to
// capture screenshots for the docs.
bool run_headless(const Headless& opts);

}  // namespace glim::tools
