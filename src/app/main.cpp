#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "app/tools.hpp"
#include "cart/bundle.hpp"
#include "platform/app.hpp"

namespace {

void usage() {
  std::puts(
      "usage: glimmer [options] [cart]\n"
      "\n"
      "  cart                    a .glim, .png or .lua cart to run\n"
      "  --fullscreen            start fullscreen (toggle with F11 or Alt+Enter)\n"
      "  --crt                   start with the CRT filter (toggle with F9)\n"
      "  --scale N               initial window scale (default 4)\n"
      "\n"
      "tools:\n"
      "  --export IN OUT         convert a cart; OUT's extension picks .glim or .png\n"
      "  --bundle CART --out GAME [--runtime EXE]\n"
      "                          build a standalone game from a copy of the runtime\n"
      "  --player                play the cart without the editors\n"
      "  --headless CART         run without a window, exit 1 on a runtime error\n"
      "      --frames N          frames to run (default 120)\n"
      "      --screenshot OUT    save the final frame as a PNG (4x with --crt)\n"
      "      --record OUT.gif    record the run as a 2x GIF at 30 fps\n"
      "      --record-from N     start recording at frame N\n"
      "      --press SPEC        hold buttons, e.g. a@30-60 or right@0-90 (repeatable)\n"
      "  --studio-script FILE [CART]  drive the editors from a script of user actions\n"
      "  --version               print the version\n");
}

}  // namespace

namespace {
glim::platform::App* g_app = nullptr;
}  // namespace

#ifdef __EMSCRIPTEN__
// Called by the web player's on-screen touch controls.
extern "C" EMSCRIPTEN_KEEPALIVE void glim_touch(int button, int down) {
  if (g_app) g_app->touch(button, down != 0);
}
#endif

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
#ifdef _WIN32
  // The exe uses the GUI subsystem; reattach to the terminal it was started
  // from so --help, --export and --headless can print.
  if (argc > 1 && AttachConsole(ATTACH_PARENT_PROCESS)) {
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
  }
#endif
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
    } else if (a == "--player") {
      opts.player = true;
    } else if (a == "--bundle") {
      std::string cart = next(), out, runtime;
      for (++i; i < argc; ++i) {
        std::string_view b = argv[i];
        if (b == "--out") out = next();
        else if (b == "--runtime") runtime = next();
      }
      return glim::tools::bundle_game(cart, out, runtime) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    } else if (a == "--export") {
      std::string in = next(), out = next();
      return glim::tools::export_cart(in, out) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    } else if (a == "--studio-script") {
      std::string script = next();
      std::string cart = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "";
      return glim::tools::run_studio_script(script, cart) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
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

#ifdef __EMSCRIPTEN__
  // On the web the cart comes from the page URL: ?cart=orbit loads /carts/orbit.glim.
  if (opts.cart_path.empty()) {
    const char* name = emscripten_run_script_string(
        "(new URLSearchParams(location.search).get('cart') || '').replace(/[^a-z0-9_-]/gi, '')");
    if (name && *name) opts.cart_path = std::string("/carts/") + name + ".glim";
  }
#endif

  auto* app = new glim::platform::App();
  *appstate = app;
  g_app = app;
  return app->init(opts) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
  return static_cast<glim::platform::App*>(appstate)->iterate() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
  return static_cast<glim::platform::App*>(appstate)->handle(*event) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) { delete static_cast<glim::platform::App*>(appstate); }
