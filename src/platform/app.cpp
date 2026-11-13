#include "platform/app.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "app/tools.hpp"
#include "cart/bundle.hpp"
#include "cart/image.hpp"
#include "editors/shell.hpp"

namespace glim::platform {

namespace {

constexpr uint64_t kFrameNs = 1'000'000'000ull / kFps;
// After a stall (window drag, breakpoint) skip ahead instead of fast-forwarding.
constexpr int kMaxCatchUpFrames = 4;
constexpr int kStickDeadzone = 12000;

int button_for(SDL_GamepadButton b) {
  switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return input::kLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return input::kRight;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return input::kUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return input::kDown;
    case SDL_GAMEPAD_BUTTON_SOUTH: return input::kA;
    case SDL_GAMEPAD_BUTTON_EAST: return input::kB;
    case SDL_GAMEPAD_BUTTON_WEST: return input::kX;
    case SDL_GAMEPAD_BUTTON_NORTH: return input::kY;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return input::kL;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return input::kR;
    default: return -1;
  }
}

std::string read_text(const std::string& path) {
  std::vector<uint8_t> bytes;
  if (!cart::read_file(path, bytes)) return {};
  return std::string(bytes.begin(), bytes.end());
}


}  // namespace

App::App()
    : studio_(console_, {[] {
                           std::string text;
                           if (char* c = SDL_GetClipboardText()) {
                             text = c;
                             SDL_free(c);
                           }
                           return text;
                         },
                         [](const std::string& text) { SDL_SetClipboardText(text.c_str()); },
                         {}}) {}

App::~App() {
  save_persist();
  if (audio_) SDL_DestroyAudioStream(audio_);
  for (auto* p : pads_)
    if (p) SDL_CloseGamepad(p);
  if (crt_tex_) SDL_DestroyTexture(crt_tex_);
  if (screen_tex_) SDL_DestroyTexture(screen_tex_);
  if (renderer_) SDL_DestroyRenderer(renderer_);
  if (window_) SDL_DestroyWindow(window_);
}

std::string App::pref_path(const std::string& file) const { return prefs_ + file; }

bool App::init(const Options& opts) {
  SDL_SetAppMetadata("Glimmer", GLIMMER_VERSION, "io.github.aubergine-ux.glimmer");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  if (char* p = SDL_GetPrefPath("glimmer", "glimmer")) {
    prefs_ = p;
    SDL_free(p);
  }
  crt_on_ = opts.crt;
  load_settings();
  // Captures land somewhere easy to find: the user's pictures folder if SDL knows it.
  if (const char* pics = SDL_GetUserFolder(SDL_FOLDER_PICTURES)) studio_.set_capture_dir(std::string(pics) + "glimmer");
  else if (!prefs_.empty()) studio_.set_capture_dir(prefs_ + "captures");

  int scale = std::max(1, opts.scale);
  if (!SDL_CreateWindowAndRenderer("Glimmer", kScreenW * scale, kScreenH * scale,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window_, &renderer_)) {
    SDL_Log("window creation failed: %s", SDL_GetError());
    return false;
  }
  SDL_SetWindowMinimumSize(window_, kScreenW, kScreenH);
  SDL_SetRenderVSync(renderer_, 1);
  SDL_SetRenderLogicalPresentation(renderer_, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

  screen_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
  SDL_SetTextureScaleMode(screen_tex_, SDL_SCALEMODE_NEAREST);
  crt_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, Crt::kW, Crt::kH);
  SDL_SetTextureScaleMode(crt_tex_, SDL_SCALEMODE_LINEAR);
  crt_buf_.resize(size_t(Crt::kW) * Crt::kH);

  SDL_AudioSpec spec{SDL_AUDIO_F32, 1, audio::kSampleRate};
  audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &App::audio_callback, this);
  if (audio_) SDL_ResumeAudioStreamDevice(audio_);
  else SDL_Log("audio unavailable: %s", SDL_GetError());

  SDL_StartTextInput(window_);
  console_.on_log = [](std::string_view s) { SDL_Log("%.*s", int(s.size()), s.data()); };
  console_.on_persist = [this] { persist_dirty_ = true; };

  studio_.shell().set_dir(carts_dir());
  studio_.set_runtime(tools::self_path());
  if (opts.fullscreen) toggle_fullscreen();

  // A standalone game carries its cart inside the executable or app bundle.
  cart::Cart bundled;
  const char* base = SDL_GetBasePath();
  if (opts.cart_path.empty() && cart::find_bundled(tools::self_path(), base ? base : "", bundled)) {
    studio_.set_player_mode(true);
    studio_.open(bundled, "");
    studio_.play();
    SDL_SetWindowTitle(window_, bundled.get_meta("title", "Glimmer").c_str());
  } else if (opts.player && !opts.cart_path.empty()) {
    studio_.set_player_mode(true);
    load_cart(opts.cart_path, true);
  } else if (opts.cart_path.empty()) {
    new_cart();
  } else {
    load_cart(opts.cart_path, true);
  }
  last_ns_ = SDL_GetTicksNS();
  return true;
}

void App::load_settings() {
  auto keys = read_text(pref_path("keys.cfg"));
  if (!keys.empty()) {
    console_.input.keymap().load(keys);
  } else if (!prefs_.empty()) {
    // Write the defaults out so players have a file to edit.
    auto text = console_.input.keymap().save();
    cart::write_file(pref_path("keys.cfg"), {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
  }
  auto settings = read_text(pref_path("settings.cfg"));
  if (settings.find("crt = on") != std::string::npos) crt_on_ = true;
}

void App::save_settings() {
  if (prefs_.empty()) return;
  std::string text = std::string("crt = ") + (crt_on_ ? "on" : "off") + "\n";
  cart::write_file(pref_path("settings.cfg"), {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

void App::new_cart() {
  studio_.new_cart();
  studio_.boot();
  SDL_SetWindowTitle(window_, "Glimmer");
}

std::string App::carts_dir() const {
  // Next to the executable when a release ships its carts there, otherwise
  // in the user data folder.
#ifdef __EMSCRIPTEN__
  return "/carts";
#else
  std::error_code ec;
  if (const char* base = SDL_GetBasePath()) {
    for (const char* rel : {"carts", "../carts", "../../carts", "../Resources/carts"}) {
      auto p = std::filesystem::path(base) / rel;
      if (std::filesystem::is_directory(p, ec)) return std::filesystem::weakly_canonical(p, ec).string();
    }
  }
  return prefs_.empty() ? std::string(".") : prefs_ + "carts";
#endif
}

void App::load_cart(const std::string& path, bool play) {
  save_persist();
  cart::Cart c;
  auto r = cart::load_file(path, c);
  if (!r.ok) {
    SDL_Log("can't load %s: %s", path.c_str(), r.error.c_str());
    studio_.message(r.error, true);
    return;
  }
  std::string stem = std::filesystem::path(path).stem().string();
  SDL_SetWindowTitle(window_, ("Glimmer - " + c.get_meta("title", stem)).c_str());

  std::vector<uint8_t> save;
  std::vector<uint8_t> zero(addr::kPersistSize, 0);
  console_.set_persist(cart::read_file(pref_path("saves/" + stem + ".sav"), save) ? save : zero);
  // A .lua file isn't a cart file; saving it writes a .glim beside it instead.
  std::string cart_path = path.ends_with(".lua") ? path.substr(0, path.size() - 4) + ".glim" : path;
  studio_.open(c, cart_path);
  if (play) studio_.play();
}

void App::save_persist() {
  if (!persist_dirty_ || studio_.path().empty() || prefs_.empty()) return;
  persist_dirty_ = false;
  std::error_code ec;
  std::filesystem::create_directories(pref_path("saves"), ec);
  std::string stem = std::filesystem::path(studio_.path()).stem().string();
  cart::write_file(pref_path("saves/" + stem + ".sav"), console_.persist());
}

void App::toggle_fullscreen() {
  fullscreen_ = !fullscreen_;
  SDL_SetWindowFullscreen(window_, fullscreen_);
}

void SDLCALL App::audio_callback(void* user, SDL_AudioStream* stream, int additional, int) {
  auto* app = static_cast<App*>(user);
  int frames = additional / int(sizeof(float));
  if (frames <= 0) return;
  if (int(app->audio_buf_.size()) < frames) app->audio_buf_.resize(frames);
  app->console_.audio.render(app->audio_buf_.data(), frames);
  SDL_PutAudioStreamData(stream, app->audio_buf_.data(), frames * int(sizeof(float)));
}

int App::player_for(SDL_JoystickID id) const {
  for (int i = 0; i < kPlayers; ++i)
    if (pads_[i] && SDL_GetGamepadID(pads_[i]) == id) return i;
  return -1;
}

void App::on_gamepad_button(SDL_JoystickID id, int button, bool down) {
  int p = player_for(id);
  int b = button_for(SDL_GamepadButton(button));
  if (p >= 0 && b >= 0) console_.input.set_pad_button(p, b, down);
}

void App::on_gamepad_axis(SDL_JoystickID id, int axis, int value) {
  int p = player_for(id);
  if (p < 0 || (axis != SDL_GAMEPAD_AXIS_LEFTX && axis != SDL_GAMEPAD_AXIS_LEFTY)) return;
  int dir = value < -kStickDeadzone ? -1 : (value > kStickDeadzone ? 1 : 0);
  int a = axis == SDL_GAMEPAD_AXIS_LEFTX ? 0 : 1;
  if (stick_[p][a] == dir) return;
  stick_[p][a] = dir;
  // The stick and d-pad share buttons; only release what the stick pressed.
  int neg = a == 0 ? input::kLeft : input::kUp, pos = a == 0 ? input::kRight : input::kDown;
  console_.input.set_pad_button(p, neg, dir < 0);
  console_.input.set_pad_button(p, pos, dir > 0);
}

void App::on_key(const SDL_KeyboardEvent& k, bool down) {
  if (down && !k.repeat) {
    bool alt = k.mod & SDL_KMOD_ALT, ctrl = k.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
    if (k.scancode == SDL_SCANCODE_F11 || (alt && k.scancode == SDL_SCANCODE_RETURN)) {
      toggle_fullscreen();
      return;
    }
    if (k.scancode == SDL_SCANCODE_F9) {
      crt_on_ = !crt_on_;
      save_settings();
      return;
    }
    if (ctrl && k.scancode == SDL_SCANCODE_Q) {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      return;
    }
  }
  console_.input.set_key(input::Key(k.scancode), down);
}

bool App::handle(SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_QUIT:
      return false;
    case SDL_EVENT_KEY_DOWN:
      on_key(e.key, true);
      break;
    case SDL_EVENT_KEY_UP:
      on_key(e.key, false);
      break;
    case SDL_EVENT_TEXT_INPUT:
      console_.input.add_text(e.text.text);
      break;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      SDL_ConvertEventToRenderCoordinates(renderer_, &e);
      if (e.type != SDL_EVENT_MOUSE_MOTION) {
        int bit = e.button.button == SDL_BUTTON_LEFT ? 0 : (e.button.button == SDL_BUTTON_RIGHT ? 1 : 2);
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_buttons_ |= uint8_t(1 << bit);
        else mouse_buttons_ &= uint8_t(~(1 << bit));
      }
      float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
      float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
      console_.input.set_mouse(int(std::floor(x)), int(std::floor(y)), mouse_buttons_);
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
      console_.input.add_wheel(int(e.wheel.y > 0 ? 1 : (e.wheel.y < 0 ? -1 : 0)));
      break;
    case SDL_EVENT_GAMEPAD_ADDED:
      for (auto& slot : pads_) {
        if (!slot) {
          slot = SDL_OpenGamepad(e.gdevice.which);
          break;
        }
      }
      break;
    case SDL_EVENT_GAMEPAD_REMOVED: {
      int p = player_for(e.gdevice.which);
      if (p >= 0) {
        SDL_CloseGamepad(pads_[p]);
        pads_[p] = nullptr;
        for (int b = 0; b < input::kButtonCount; ++b) console_.input.set_pad_button(p, b, false);
      }
      break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      on_gamepad_button(e.gbutton.which, e.gbutton.button, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      on_gamepad_axis(e.gaxis.which, e.gaxis.axis, e.gaxis.value);
      break;
    case SDL_EVENT_DROP_FILE:
      if (e.drop.data) load_cart(e.drop.data, true);
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      console_.input.release_all();
      mouse_buttons_ = 0;
      save_persist();
      break;
    default:
      break;
  }
  return true;
}

bool App::iterate() {
  uint64_t now = SDL_GetTicksNS();
  accum_ns_ += now - last_ns_;
  last_ns_ = now;
  accum_ns_ = std::min<uint64_t>(accum_ns_, kFrameNs * kMaxCatchUpFrames);
  while (accum_ns_ >= kFrameNs) {
    studio_.tick();
    accum_ns_ -= kFrameNs;
  }
  // The editors draw their own cursor; games get the system one.
  if (studio_.playing() != cursor_visible_) {
    cursor_visible_ = studio_.playing();
    if (cursor_visible_) SDL_ShowCursor();
    else SDL_HideCursor();
  }
  draw();
  return true;
}

void App::draw() {
  // Integer scaling keeps pixels perfectly square, but on screens too small
  // for 2x (phones) a 1x picture would be tiny, so fill the space instead.
  int ow = 0, oh = 0;
  SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
  bool small = ow < kScreenW * 2 || oh < kScreenH * 2;
  if (small != small_output_) {
    small_output_ = small;
    SDL_SetRenderLogicalPresentation(renderer_, kScreenW, kScreenH,
                                     small ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
  }
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  SDL_FRect dst{0, 0, float(kScreenW), float(kScreenH)};
  if (crt_on_) {
    crt_.apply(studio_.display(), crt_buf_.data());
    SDL_UpdateTexture(crt_tex_, nullptr, crt_buf_.data(), Crt::kW * 4);
    SDL_RenderTexture(renderer_, crt_tex_, nullptr, &dst);
  } else {
    SDL_UpdateTexture(screen_tex_, nullptr, studio_.display(), kScreenW * 4);
    SDL_RenderTexture(renderer_, screen_tex_, nullptr, &dst);
  }
  SDL_RenderPresent(renderer_);
}

}  // namespace glim::platform
