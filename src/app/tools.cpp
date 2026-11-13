#include "app/tools.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "cart/cart.hpp"
#include "cart/gif.hpp"
#include "cart/image.hpp"
#include "editors/studio.hpp"
#include "platform/crt.hpp"
#include "runtime/console.hpp"

namespace glim::tools {

namespace {

struct Press {
  int button = -1;
  int from = 0, to = 0;
};

bool parse_press(const std::string& spec, Press& out) {
  auto at = spec.find('@'), dash = spec.find('-', at);
  if (at == std::string::npos || dash == std::string::npos) return false;
  out.button = input::button_from_name(spec.substr(0, at));
  out.from = std::atoi(spec.c_str() + at + 1);
  out.to = std::atoi(spec.c_str() + dash + 1);
  return out.button >= 0;
}

}  // namespace

namespace {
constexpr int kLabelFrames = 180;

// Carts without a label get one from a short headless run, so exported PNGs
// show the game instead of a placeholder.
void capture_label(cart::Cart& c) {
  auto console = std::make_unique<Console>();
  Console& con = *console;
  con.run(c, 1);
  std::vector<float> sink(audio::kSamplesPerFrame);
  for (int f = 0; f < kLabelFrames && con.state() == Console::State::kRunning; ++f) {
    con.tick();
    con.audio.render(sink.data(), int(sink.size()));
  }
  if (con.state() != Console::State::kRunning) return;
  c.label.resize(addr::kScreenSize);
  for (uint32_t i = 0; i < addr::kScreenSize; ++i) {
    uint8_t px = con.mem.peek(addr::kScreen + i) & 31;
    c.label[i] = con.mem.peek(addr::kScreenPal + px) & 31;
  }
}
}  // namespace

bool export_cart(const std::string& in, const std::string& out) {
  cart::Cart c;
  auto r = cart::load_file(in, c);
  if (r.ok && c.label.empty() && out.ends_with(".png")) capture_label(c);
  if (r.ok) r = cart::save_file(out, c);
  if (!r.ok) {
    std::fprintf(stderr, "export failed: %s\n", r.error.c_str());
    return false;
  }
  return true;
}

bool run_headless(const Headless& opts) {
  cart::Cart c;
  auto r = cart::load_file(opts.cart, c);
  if (!r.ok) {
    std::fprintf(stderr, "%s: %s\n", opts.cart.c_str(), r.error.c_str());
    return false;
  }
  std::vector<Press> presses;
  for (const auto& spec : opts.presses) {
    Press p;
    if (!parse_press(spec, p)) {
      std::fprintf(stderr, "bad --press spec: %s\n", spec.c_str());
      return false;
    }
    presses.push_back(p);
  }

  // Console holds several hundred KiB inline; keep it off the stack, which is
  // small on some platforms' main-callback threads.
  auto console = std::make_unique<Console>();
  Console& con = *console;
  con.on_log = [](std::string_view s) { std::printf("%.*s\n", int(s.size()), s.data()); };
  con.run(c, 1);
  std::vector<float> sink(audio::kSamplesPerFrame);
  cart::GifWriter gif(kScreenW, kScreenH, 2);
  for (int f = 0; f < opts.frames && con.state() == Console::State::kRunning; ++f) {
    for (const auto& p : presses) con.input.set_pad_button(0, p.button, f >= p.from && f < p.to);
    con.tick();
    // Pull audio too, so sound code paths run exactly as they would live.
    con.audio.render(sink.data(), int(sink.size()));
    // GIF delays are in centiseconds; alternating 3 and 4 averages 30 fps.
    if (!opts.record.empty() && f >= opts.record_from && (f - opts.record_from) % 2 == 0)
      gif.add_frame(con.display(), gif.frames() % 3 == 2 ? 4 : 3);
  }
  if (!opts.record.empty() && !cart::write_file(opts.record, gif.finish())) {
    std::fprintf(stderr, "can't write %s\n", opts.record.c_str());
    return false;
  }

  if (!opts.screenshot.empty()) {
    const uint32_t* px = con.display();
    int w = kScreenW, h = kScreenH;
    std::vector<uint32_t> filtered;
    if (opts.crt) {
      filtered.resize(size_t(platform::Crt::kW) * platform::Crt::kH);
      platform::Crt().apply(px, filtered.data());
      px = filtered.data();
      w = platform::Crt::kW;
      h = platform::Crt::kH;
    }
    cart::Image img(w, h);
    for (int i = 0; i < w * h; ++i) {
      img.rgba[i * 4 + 0] = uint8_t(px[i] >> 16);
      img.rgba[i * 4 + 1] = uint8_t(px[i] >> 8);
      img.rgba[i * 4 + 2] = uint8_t(px[i]);
      img.rgba[i * 4 + 3] = 255;
    }
    if (!cart::write_file(opts.screenshot, cart::encode_png(img))) {
      std::fprintf(stderr, "can't write %s\n", opts.screenshot.c_str());
      return false;
    }
  }

  if (con.state() == Console::State::kError) {
    const auto& e = con.error();
    std::fprintf(stderr, "%s:%d: %s\n%s\n", opts.cart.c_str(), e.line, e.message.c_str(), e.traceback.c_str());
    return false;
  }
  std::printf("%s: ran %llu frames, cpu %.1f%%\n", opts.cart.c_str(), (unsigned long long)con.frames(),
              con.cpu_usage() * 100.0);
  return true;
}

}  // namespace glim::tools

namespace glim::tools {

namespace {

// Splits "ctrl+shift+z" into modifier keys and the main key.
std::vector<input::Key> parse_chord(const std::string& chord) {
  std::vector<input::Key> keys;
  size_t start = 0;
  while (start <= chord.size()) {
    size_t plus = chord.find('+', start);
    if (plus == start && plus + 1 == chord.size()) plus = std::string::npos;  // the "+" key itself
    std::string part = chord.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
    if (part == "ctrl") part = "lctrl";
    if (part == "shift") part = "lshift";
    if (part == "alt") part = "lalt";
    if (part == "enter") part = "return";
    if (part == "esc") part = "escape";
    if (input::Key k = input::key_from_name(part)) keys.push_back(k);
    if (plus == std::string::npos) break;
    start = plus + 1;
  }
  return keys;
}

}  // namespace

bool run_studio_script(const std::string& script_path, const std::string& cart_path) {
  std::vector<uint8_t> bytes;
  if (!cart::read_file(script_path, bytes)) {
    std::fprintf(stderr, "can't read %s\n", script_path.c_str());
    return false;
  }
  std::string script(bytes.begin(), bytes.end());

  auto console = std::make_unique<Console>();
  editors::Studio studio(*console, {});
  cart::Cart c;
  if (!cart_path.empty()) {
    auto r = cart::load_file(cart_path, c);
    if (!r.ok) {
      std::fprintf(stderr, "%s: %s\n", cart_path.c_str(), r.error.c_str());
      return false;
    }
  }
  studio.open(c, cart_path);

  auto& in = console->input;
  int mx = 0, my = 0;
  uint8_t buttons = 0;
  std::unique_ptr<cart::GifWriter> gif;
  std::string gif_path;
  int frame = 0;
  std::vector<float> sink(audio::kSamplesPerFrame);

  auto step = [&](int n) {
    for (int i = 0; i < n; ++i) {
      in.set_mouse(mx, my, buttons);
      studio.tick();
      console->audio.render(sink.data(), int(sink.size()));
      if (gif && frame % 2 == 0) gif->add_frame(studio.display(), gif->frames() % 3 == 2 ? 4 : 3);
      ++frame;
    }
  };
  auto glide = [&](int tx, int ty, int frames) {
    int sx = mx, sy = my;
    for (int i = 1; i <= frames; ++i) {
      mx = sx + (tx - sx) * i / frames;
      my = sy + (ty - sy) * i / frames;
      step(1);
    }
  };

  size_t pos = 0;
  int lineno = 0;
  while (pos < script.size()) {
    size_t nl = script.find('\n', pos);
    std::string line = script.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? script.size() : nl + 1;
    ++lineno;
    if (line.empty() || line[0] == '#') continue;
    auto sp = line.find(' ');
    std::string cmd = line.substr(0, sp), arg = sp == std::string::npos ? "" : line.substr(sp + 1);
    int a = 0, b = 0, cc = 0, d = 0, e = 0;
    int nargs = std::sscanf(arg.c_str(), "%d %d %d %d %d", &a, &b, &cc, &d, &e);

    if (cmd == "wait") {
      step(nargs > 0 ? a : 1);
    } else if (cmd == "mouse") {
      glide(a, b, nargs >= 3 ? cc : 1);
    } else if (cmd == "click" || cmd == "rclick" || cmd == "dclick") {
      int bit = cmd == "rclick" ? 2 : 1;
      if (nargs >= 2) glide(a, b, nargs >= 3 ? cc : 1);
      for (int k = 0; k < (cmd == "dclick" ? 2 : 1); ++k) {
        buttons |= uint8_t(bit);
        step(1);
        buttons &= uint8_t(~bit);
        step(1);
      }
    } else if (cmd == "drag") {
      glide(a, b, 1);
      buttons |= 1;
      step(1);
      glide(cc, d, nargs >= 5 ? e : 10);
      buttons &= uint8_t(~1);
      step(1);
    } else if (cmd == "key") {
      auto keys = parse_chord(arg);
      for (auto k : keys) in.set_key(k, true);
      step(1);
      for (auto k : keys) in.set_key(k, false);
      step(1);
    } else if (cmd == "hold" || cmd == "release") {
      for (auto k : parse_chord(arg)) in.set_key(k, cmd == "hold");
      step(1);
    } else if (cmd == "type") {
      // One character per frame, like a quick typist.
      for (char ch : arg) {
        if (ch == '|') {
          auto k = input::key_from_name("return");
          in.set_key(k, true);
          step(1);
          in.set_key(k, false);
          step(1);
          continue;
        }
        in.add_text(std::string(1, ch));
        step(2);
      }
    } else if (cmd == "shot") {
      cart::Image img(kScreenW, kScreenH);
      const uint32_t* px = studio.display();
      for (int i = 0; i < kScreenW * kScreenH; ++i) {
        img.rgba[i * 4 + 0] = uint8_t(px[i] >> 16);
        img.rgba[i * 4 + 1] = uint8_t(px[i] >> 8);
        img.rgba[i * 4 + 2] = uint8_t(px[i]);
        img.rgba[i * 4 + 3] = 255;
      }
      cart::write_file(arg, cart::encode_png(img));
    } else if (cmd == "record") {
      gif = std::make_unique<cart::GifWriter>(kScreenW, kScreenH, 2);
      gif_path = arg;
    } else if (cmd == "stoprecord") {
      if (gif) cart::write_file(gif_path, gif->finish());
      gif.reset();
    } else if (cmd == "save") {
      auto r = cart::save_file(arg, studio.cart());
      if (!r.ok) std::fprintf(stderr, "save failed: %s\n", r.error.c_str());
    } else {
      std::fprintf(stderr, "%s:%d: unknown command '%s'\n", script_path.c_str(), lineno, cmd.c_str());
      return false;
    }
  }
  if (gif) cart::write_file(gif_path, gif->finish());
  return true;
}

}  // namespace glim::tools
