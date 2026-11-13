#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/audio.hpp"

using namespace glim;
using namespace glim::audio;

namespace {

Sfx tone(int pitch, int wave, int volume, int speed = 4) {
  Sfx s;
  s.speed = uint8_t(speed);
  for (auto& st : s.steps) st = Step{uint8_t(pitch), uint8_t(wave), uint8_t(volume), 0};
  return s;
}

std::vector<float> render(Audio& a, int n, int chunk = kSamplesPerFrame) {
  std::vector<float> out(n);
  for (int i = 0; i < n; i += chunk) a.render(out.data() + i, std::min(chunk, n - i));
  return out;
}

int rising_zero_crossings(const std::vector<float>& v, int from, int to) {
  int n = 0;
  for (int i = from + 1; i < to; ++i) n += v[i - 1] < 0.f && v[i] >= 0.f;
  return n;
}

}  // namespace

TEST_CASE("step encoding round-trips every field") {
  Step s{63, 15, 7, 7};
  CHECK(Step::decode(s.encode()).pitch == 63);
  CHECK(Step::decode(s.encode()).wave == 15);
  CHECK(Step::decode(s.encode()).volume == 7);
  CHECK(Step::decode(s.encode()).effect == 7);
  Step t{33, 2, 5, 3};
  auto u = Step::decode(t.encode());
  CHECK(u.pitch == 33);
  CHECK(u.wave == 2);
  CHECK(u.volume == 5);
  CHECK(u.effect == 3);
}

TEST_CASE("pitch 33 is concert A") { CHECK(pitch_to_hz(33) == doctest::Approx(440.0).epsilon(1e-6)); }

TEST_CASE("sine at pitch 33 oscillates at 440 Hz") {
  Memory mem;
  write_sfx(mem, 0, tone(33, kSine, 7, 255));
  Audio a;
  a.sync(mem);
  a.sfx(0, 0);
  auto out = render(a, kSampleRate);
  // Skip the first 1/10 s while the amplitude ramp settles.
  int crossings = rising_zero_crossings(out, kSampleRate / 10, kSampleRate);
  CHECK(crossings == doctest::Approx(440 * 0.9).epsilon(0.01));
}

TEST_CASE("rendering is independent of buffer size") {
  Memory mem;
  Sfx s = tone(20, kSquare, 6, 2);
  for (int i = 0; i < kSfxSteps; ++i) {
    s.steps[i].pitch = uint8_t(10 + i);
    s.steps[i].wave = uint8_t(i % 8);
    s.steps[i].effect = uint8_t(i % 8);
  }
  write_sfx(mem, 3, s);

  Audio a, b;
  a.sync(mem);
  b.sync(mem);
  a.sfx(3, 1);
  b.sfx(3, 1);
  auto x = render(a, 30000, kSamplesPerFrame);
  auto y = render(b, 30000, 37);
  CHECK(x == y);
}

TEST_CASE("notes start on the exact sample their step begins") {
  Memory mem;
  Sfx s = tone(30, kSquare, 7, 1);
  s.steps[0].volume = 0;
  write_sfx(mem, 0, s);
  Audio a;
  a.sync(mem);
  a.sfx(0, 0);
  auto out = render(a, kSamplesPerSpeedUnit * 3);
  for (int i = 0; i < kSamplesPerSpeedUnit; ++i) REQUIRE(out[i] == 0.f);
  CHECK(std::abs(out[kSamplesPerSpeedUnit]) > 0.f);
}

TEST_CASE("volume changes between steps do not click") {
  Memory mem;
  Sfx s = tone(24, kSine, 7, 1);
  for (int i = 0; i < kSfxSteps; ++i) s.steps[i].volume = uint8_t(i % 2 ? 7 : 1);
  write_sfx(mem, 0, s);
  Audio a;
  a.sync(mem);
  a.sfx(0, 0);
  auto out = render(a, kSamplesPerSpeedUnit * kSfxSteps + 2000);
  float worst = 0.f;
  for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - out[i - 1]));
  // A full-scale 260 Hz sine moves ~0.01 per sample at this gain; a click would be ~0.3.
  CHECK(worst < 0.03f);
}

TEST_CASE("sfx stops after its last step and loops until released") {
  Memory mem;
  write_sfx(mem, 0, tone(24, kTriangle, 5, 1));
  Sfx looping = tone(24, kTriangle, 5, 1);
  looping.loop_start = 4;
  looping.loop_end = 8;
  write_sfx(mem, 1, looping);
  Audio a;
  a.sync(mem);
  a.sfx(0, 0);
  a.sfx(1, 1);
  render(a, kSamplesPerSpeedUnit * kSfxSteps + 10);
  CHECK(a.status(0).sfx == -1);
  CHECK(a.status(1).sfx == 1);
  CHECK(a.status(1).step >= 4);
  CHECK(a.status(1).step < 8);

  a.sfx(-2, 1);
  render(a, kSamplesPerSpeedUnit * kSfxSteps + 10);
  CHECK(a.status(1).sfx == -1);
}

TEST_CASE("music advances patterns and honours the stop flag") {
  Memory mem;
  write_sfx(mem, 0, tone(24, kSquare, 4, 1));
  write_sfx(mem, 1, tone(28, kSquare, 4, 1));
  // Pattern 0 plays sfx 0, pattern 1 plays sfx 1 and stops; other channels disabled.
  mem.poke(addr::kMusic + 0, 0);
  mem.poke(addr::kMusic + 1, 0x40);
  mem.poke(addr::kMusic + 2, 0x40);
  mem.poke(addr::kMusic + 3, 0x40);
  mem.poke(addr::kMusic + 4, 1);
  mem.poke(addr::kMusic + 5, 0x40);
  mem.poke(addr::kMusic + 6, 0x40 | 0x80);
  mem.poke(addr::kMusic + 7, 0x40);
  Audio a;
  a.sync(mem);
  a.music(0);
  CHECK(a.music_pattern() == 0);
  int pattern_len = kSamplesPerSpeedUnit * kSfxSteps;
  render(a, pattern_len + 1);
  CHECK(a.music_pattern() == 1);
  CHECK(a.status(0).sfx == 1);
  render(a, pattern_len);
  CHECK(a.music_pattern() == -1);
}

TEST_CASE("sfx avoids channels reserved for music") {
  Memory mem;
  write_sfx(mem, 0, tone(24, kSquare, 4, 8));
  for (int i = 0; i < 4; ++i) mem.poke(addr::kMusic + i, 0);
  Audio a;
  a.sync(mem);
  a.music(0, 0, 0b0011);
  a.sfx(0);
  CHECK(a.status(2).sfx == 0);
  a.sfx(0);
  CHECK(a.status(3).sfx == 0);
}

TEST_CASE("custom wavetables are played back") {
  Memory mem;
  for (int i = 0; i < kWaveSamples; ++i) mem.poke(addr::kWave + i, uint8_t(i < 16 ? 100 : -100));
  write_sfx(mem, 0, tone(24, kCustom0, 7, 255));
  Audio a;
  a.sync(mem);
  a.sfx(0, 0);
  auto out = render(a, 9600);
  float peak = 0.f;
  for (float v : out) peak = std::max(peak, std::abs(v));
  CHECK(peak > 0.15f);
}
