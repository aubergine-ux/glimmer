#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "editors/studio.hpp"
#include "platform/crt.hpp"
#include "runtime/console.hpp"

namespace glim::platform {

struct Options {
  std::string cart_path;
  bool fullscreen = false;
  bool crt = false;
  int scale = 4;
};

// Desktop/web frontend: owns the SDL window, audio device and gamepads, and
// drives the console at a fixed 60 Hz regardless of display refresh rate.
class App {
 public:
  App();
  ~App();

  bool init(const Options& opts);
  bool iterate();               // false once the app should quit
  bool handle(SDL_Event& e);    // false once the app should quit

 private:
  void load_cart(const std::string& path, bool play);
  void new_cart();
  void draw();
  void toggle_fullscreen();
  void save_persist();
  void load_settings();
  void save_settings();
  void on_key(const SDL_KeyboardEvent& k, bool down);
  void on_gamepad_button(SDL_JoystickID id, int button, bool down);
  void on_gamepad_axis(SDL_JoystickID id, int axis, int value);
  int player_for(SDL_JoystickID id) const;
  std::string pref_path(const std::string& file) const;
  static void SDLCALL audio_callback(void* user, SDL_AudioStream* stream, int additional, int total);

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* screen_tex_ = nullptr;
  SDL_Texture* crt_tex_ = nullptr;
  SDL_AudioStream* audio_ = nullptr;
  std::array<SDL_Gamepad*, kPlayers> pads_{};

  Console console_;
  editors::Studio studio_;
  Crt crt_;
  std::vector<uint32_t> crt_buf_;
  std::vector<float> audio_buf_;
  std::string prefs_;
  bool crt_on_ = false;
  bool fullscreen_ = false;
  bool persist_dirty_ = false;
  bool cursor_visible_ = true;
  uint8_t mouse_buttons_ = 0;
  std::array<std::array<int, 2>, kPlayers> stick_{};  // last digital stick direction per axis
  uint64_t last_ns_ = 0;
  uint64_t accum_ns_ = 0;
};

}  // namespace glim::platform
