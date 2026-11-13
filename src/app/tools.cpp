#include "app/tools.hpp"

#include <cstdio>
#include <cstdlib>

#include "cart/cart.hpp"
#include "cart/image.hpp"
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

bool export_cart(const std::string& in, const std::string& out) {
  cart::Cart c;
  auto r = cart::load_file(in, c);
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

  Console con;
  con.on_log = [](std::string_view s) { std::printf("%.*s\n", int(s.size()), s.data()); };
  con.run(c, 1);
  std::vector<float> sink(audio::kSamplesPerFrame);
  for (int f = 0; f < opts.frames && con.state() == Console::State::kRunning; ++f) {
    for (const auto& p : presses) con.input.set_pad_button(0, p.button, f >= p.from && f < p.to);
    con.tick();
    // Pull audio too, so sound code paths run exactly as they would live.
    con.audio.render(sink.data(), int(sink.size()));
  }

  if (!opts.screenshot.empty()) {
    cart::Image img(kScreenW, kScreenH);
    const uint32_t* px = con.display();
    for (int i = 0; i < kScreenW * kScreenH; ++i) {
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
  std::printf("%s: ran %llu frames, cpu %.0f%%\n", opts.cart.c_str(), (unsigned long long)con.frames(),
              con.cpu_usage() * 100.0);
  return true;
}

}  // namespace glim::tools
