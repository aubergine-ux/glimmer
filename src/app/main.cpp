#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "app/tools.hpp"
#include "platform/app.hpp"

namespace {

void usage() {
  std::puts(
      "usage: glimmer [options] [cart]\n"
      "\n"
      "  cart                    a .glim or .png cart to run\n"
      "  --fullscreen            start fullscreen (toggle with F11 or Alt+Enter)\n"
      "  --crt                   start with the CRT filter (toggle with F9)\n"
      "  --scale N               initial window scale (default 4)\n"
      "\n"
      "tools:\n"
      "  --export IN OUT         convert a cart; OUT's extension picks .glim or .png\n"
      "  --headless CART         run without a window, exit 1 on a runtime error\n"
      "      --frames N          frames to run (default 120)\n"
      "      --screenshot OUT    save the final frame as a PNG (4x with --crt)\n"
      "      --record OUT.gif    record the run as a 2x GIF at 30 fps\n"
      "      --record-from N     start recording at frame N\n"
      "      --press SPEC        hold buttons, e.g. a@30-60 or right@0-90 (repeatable)\n"
      "  --version               print the version\n");
}

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
  glim::platform::Options opts;
  glim::tools::Headless headless;
  bool tool = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--help" || a == "-h") {
      usage();
      return SDL_APP_SUCCESS;
    } else if (a == "--version") {
      std::puts("glimmer 0.1.0");
      return SDL_APP_SUCCESS;
    } else if (a == "--fullscreen") {
      opts.fullscreen = true;
    } else if (a == "--crt") {
      opts.crt = true;
      headless.crt = true;
    } else if (a == "--scale") {
      opts.scale = std::atoi(next());
    } else if (a == "--export") {
      std::string in = next(), out = next();
      return glim::tools::export_cart(in, out) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    } else if (a == "--headless") {
      headless.cart = next();
      tool = true;
    } else if (a == "--frames") {
      headless.frames = std::atoi(next());
    } else if (a == "--screenshot") {
      headless.screenshot = next();
    } else if (a == "--record") {
      headless.record = next();
    } else if (a == "--record-from") {
      headless.record_from = std::atoi(next());
    } else if (a == "--press") {
      headless.presses.emplace_back(next());
    } else if (!a.empty() && a[0] != '-') {
      opts.cart_path = std::string(a);
    } else {
      std::fprintf(stderr, "unknown option %s\n", argv[i]);
      usage();
      return SDL_APP_FAILURE;
    }
  }
  if (tool) return glim::tools::run_headless(headless) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;

  auto* app = new glim::platform::App();
  *appstate = app;
  return app->init(opts) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
  return static_cast<glim::platform::App*>(appstate)->iterate() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
  return static_cast<glim::platform::App*>(appstate)->handle(*event) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) { delete static_cast<glim::platform::App*>(appstate); }
