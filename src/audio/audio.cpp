#include "audio/audio.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace glim::audio {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kBaseHz = 65.40639132514966;  // C2
constexpr float kAmpSmoothing = 0.02f;         // ~1 ms, removes clicks on volume jumps
constexpr int kArpFastPeriod = kSamplesPerFrame;
constexpr int kArpSlowPeriod = kSamplesPerFrame * 2;
constexpr double kVibratoHz = 6.0;
constexpr double kVibratoDepth = 0.35;  // semitones
constexpr float kMixGain = 0.3f;

constexpr uint8_t kPatternDisabled = 0x40;
constexpr uint8_t kPatternFlag = 0x80;

// Band-limits the discontinuity of square and saw waves (polynomial BLEP).
double poly_blep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0;
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return t * t + t + t + 1.0;
  }
  return 0.0;
}

double triangle(double p) { return 1.0 - 4.0 * std::abs(p - 0.5); }

}  // namespace

double pitch_to_hz(double pitch) { return kBaseHz * std::exp2(pitch / 12.0); }

Sfx read_sfx(const Memory& mem, int n) {
  Sfx s;
  uint32_t a = addr::kSfx + uint32_t(n) * kSfxBytes;
  s.speed = mem.peek(a);
  s.loop_start = mem.peek(a + 1);
  s.loop_end = mem.peek(a + 2);
  s.flags = mem.peek(a + 3);
  for (int i = 0; i < kSfxSteps; ++i) s.steps[i] = Step::decode(mem.peek16(a + 4 + i * 2));
  return s;
}

void write_sfx(Memory& mem, int n, const Sfx& s) {
  uint32_t a = addr::kSfx + uint32_t(n) * kSfxBytes;
  mem.poke(a, s.speed);
  mem.poke(a + 1, s.loop_start);
  mem.poke(a + 2, s.loop_end);
  mem.poke(a + 3, s.flags);
  for (int i = 0; i < kSfxSteps; ++i) mem.poke16(a + 4 + i * 2, s.steps[i].encode());
}

Audio::Audio() { reset(); }

void Audio::reset() {
  std::lock_guard lock(mu_);
  for (auto& c : ch_) c = Channel{};
  music_pattern_ = -1;
  music_mask_ = 0;
  music_gain_ = 1.f;
  music_gain_step_ = 0.f;
  music_stopping_ = false;
}

void Audio::sync(const Memory& mem) {
  std::array<Sfx, kSfxCount> sfx;
  for (int i = 0; i < kSfxCount; ++i) sfx[i] = read_sfx(mem, i);
  std::lock_guard lock(mu_);
  sfx_ = sfx;
  for (int t = 0; t < kWavetables; ++t)
    for (int i = 0; i < kWaveSamples; ++i)
      waves_[t][i] = int8_t(mem.peek(addr::kWave + t * kWaveSamples + i));
  auto pat = mem.region(addr::kMusic, addr::kMusicSize);
  std::copy(pat.begin(), pat.end(), patterns_.begin());
}

void Audio::set_master_volume(float v) {
  std::lock_guard lock(mu_);
  master_ = std::clamp(v, 0.f, 1.f);
}

int Audio::pick_channel() const {
  for (int i = 0; i < kChannels; ++i)
    if (ch_[i].sfx < 0 && !ch_[i].live && !((music_mask_ >> i) & 1)) return i;
  for (int i = kChannels - 1; i >= 0; --i)
    if (!((music_mask_ >> i) & 1)) return i;
  return kChannels - 1;
}

void Audio::start_sfx(Channel& ch, int n, int offset, int length, bool music) {
  ch.sfx = n;
  ch.step = std::clamp(offset, 0, kSfxSteps - 1);
  ch.end = std::clamp(ch.step + length, ch.step + 1, kSfxSteps);
  ch.pos = 0;
  ch.releasing = false;
  ch.live = false;
  ch.music = music;
  ch.clock = 0;
  ch.prev_pitch = sfx_[n].steps[ch.step].pitch;
}

void Audio::sfx(int n, int channel, int offset, int length) {
  std::lock_guard lock(mu_);
  if (n == -1 || n == -2) {
    for (int i = 0; i < kChannels; ++i) {
      if (channel >= 0 && i != channel) continue;
      if (channel < 0 && ch_[i].music) continue;
      if (n == -1) {
        ch_[i].sfx = -1;
        ch_[i].live = false;
      } else {
        ch_[i].releasing = true;
      }
    }
    return;
  }
  if (n < 0 || n >= kSfxCount) return;
  int c = (channel >= 0 && channel < kChannels) ? channel : pick_channel();
  start_sfx(ch_[c], n, offset, length, false);
}

void Audio::note(int channel, int pitch, int wave, int volume, int effect) {
  std::lock_guard lock(mu_);
  if (channel < 0 || channel >= kChannels) return;
  auto& ch = ch_[channel];
  if (pitch < 0) {
    ch.live = false;
    return;
  }
  ch.sfx = -1;
  ch.music = false;
  if (!ch.live) ch.clock = 0;
  ch.live = true;
  ch.live_step = Step{uint8_t(std::clamp(pitch, 0, 63)), uint8_t(wave & 15), uint8_t(std::clamp(volume, 0, 7)),
                      uint8_t(effect & 7)};
}

void Audio::start_pattern(int pattern) {
  music_pattern_ = pattern;
  music_pos_ = 0;
  int len = 0, fallback = 0;
  for (int i = 0; i < kChannels; ++i) {
    uint8_t b = patterns_[pattern * kChannels + i];
    if ((b & kPatternDisabled) || !((music_mask_ >> i) & 1)) continue;
    int n = b & 63;
    const Sfx& s = sfx_[n];
    int dur = std::max<int>(1, s.speed) * kSamplesPerSpeedUnit * kSfxSteps;
    fallback = std::max(fallback, dur);
    if (!len && s.loop_end <= s.loop_start) len = dur;
    start_sfx(ch_[i], n, 0, kSfxSteps, true);
  }
  music_len_ = len ? len : fallback;
  if (!music_len_) music_pattern_ = -1;
}

void Audio::music(int pattern, int fade_ms, uint8_t channel_mask) {
  std::lock_guard lock(mu_);
  int fade_samples = std::max(0, fade_ms) * kSampleRate / 1000;
  if (pattern < 0) {
    if (fade_samples > 0 && music_pattern_ >= 0) {
      music_stopping_ = true;
      music_gain_step_ = -music_gain_ / float(fade_samples);
      return;
    }
    for (int i = 0; i < kChannels; ++i)
      if (ch_[i].music) ch_[i].sfx = -1;
    music_pattern_ = -1;
    music_mask_ = 0;
    return;
  }
  if (pattern >= kPatterns) return;
  for (auto& c : ch_)
    if (c.music) c.sfx = -1;
  music_mask_ = channel_mask & 0x0F;
  music_stopping_ = false;
  music_gain_ = fade_samples > 0 ? 0.f : 1.f;
  music_gain_step_ = fade_samples > 0 ? 1.f / float(fade_samples) : 0.f;
  start_pattern(pattern);
}

void Audio::tick_music() {
  if (music_pattern_ < 0) return;
  if (music_gain_step_ != 0.f) {
    music_gain_ += music_gain_step_;
    if (music_gain_ >= 1.f) {
      music_gain_ = 1.f;
      music_gain_step_ = 0.f;
    } else if (music_gain_ <= 0.f) {
      music_gain_ = 0.f;
      music_gain_step_ = 0.f;
      if (music_stopping_) {
        for (auto& c : ch_)
          if (c.music) c.sfx = -1;
        music_pattern_ = -1;
        music_mask_ = 0;
        music_stopping_ = false;
        return;
      }
    }
  }
  if (++music_pos_ < music_len_) return;

  const uint8_t* p = &patterns_[music_pattern_ * kChannels];
  int next = music_pattern_ + 1;
  if (p[2] & kPatternFlag) {
    next = -1;
  } else if (p[1] & kPatternFlag) {
    next = 0;
    for (int i = music_pattern_; i >= 0; --i) {
      if (patterns_[i * kChannels] & kPatternFlag) {
        next = i;
        break;
      }
    }
  }
  bool empty = true;
  if (next >= 0 && next < kPatterns)
    for (int i = 0; i < kChannels; ++i) empty &= (patterns_[next * kChannels + i] & kPatternDisabled) != 0;
  if (next < 0 || next >= kPatterns || empty) {
    for (auto& c : ch_)
      if (c.music) c.sfx = -1;
    music_pattern_ = -1;
    music_mask_ = 0;
    return;
  }
  start_pattern(next);
}

float Audio::oscillator(Channel& ch, int wave, double hz) {
  double dt = std::min(hz / kSampleRate, 0.5);
  double p = ch.phase;
  double v = 0.0;
  switch (wave) {
    case kSquare:
      v = (p < 0.5 ? 1.0 : -1.0) + poly_blep(p, dt) - poly_blep(std::fmod(p + 0.5, 1.0), dt);
      break;
    case kPulse:
      v = (p < 0.25 ? 1.0 : -1.0) + poly_blep(p, dt) - poly_blep(std::fmod(p + 0.75, 1.0), dt);
      break;
    case kTriangle:
      v = triangle(p);
      break;
    case kSaw:
      v = (2.0 * p - 1.0) - poly_blep(p, dt);
      break;
    case kNoise: {
      // Pitched noise: the LFSR is clocked faster for higher notes.
      ch.noise_phase += hz * 16.0 / kSampleRate;
      while (ch.noise_phase >= 1.0) {
        ch.noise_phase -= 1.0;
        uint32_t bit = (ch.lfsr ^ (ch.lfsr >> 1)) & 1u;
        ch.lfsr = (ch.lfsr >> 1) | (bit << 14);
        ch.noise_value = (ch.lfsr & 1u) ? 1.f : -1.f;
      }
      v = ch.noise_value * 0.7;
      break;
    }
    case kOrgan:
      v = triangle(p) * 0.6 + triangle(std::fmod(p * 2.0, 1.0)) * 0.4;
      break;
    case kSine:
      v = std::sin(kTwoPi * p);
      break;
    case kSoftSquare:
      v = std::clamp(std::sin(kTwoPi * p) * 3.0, -1.0, 1.0) * 0.9;
      break;
    default: {
      const auto& t = waves_[(wave - kCustom0) & 7];
      double fi = p * kWaveSamples;
      int i0 = int(fi) % kWaveSamples, i1 = (i0 + 1) % kWaveSamples;
      double f = fi - std::floor(fi);
      v = (t[i0] * (1.0 - f) + t[i1] * f) / 128.0;
      break;
    }
  }
  ch.phase += hz / kSampleRate;
  ch.phase -= std::floor(ch.phase);
  return float(v);
}

float Audio::channel_sample(Channel& ch) {
  bool active = ch.live || ch.sfx >= 0;
  if (!active && ch.amp < 1e-4f) return 0.f;

  Step st{};
  double pitch = ch.tail_pitch;
  float target = 0.f;
  int wave = ch.tail_wave;

  if (ch.live) {
    st = ch.live_step;
  } else if (ch.sfx >= 0) {
    st = sfx_[ch.sfx].steps[ch.step];
  }

  if (active) {
    const Sfx* s = ch.sfx >= 0 ? &sfx_[ch.sfx] : nullptr;
    int step_len = s ? std::max<int>(1, s->speed) * kSamplesPerSpeedUnit : kSamplesPerSpeedUnit * 16;
    double t = ch.live ? 0.0 : double(ch.pos) / step_len;
    pitch = st.pitch;
    float vol = st.volume / 7.f;
    switch (st.effect) {
      case kSlide:
        pitch = ch.prev_pitch + (st.pitch - ch.prev_pitch) * t;
        break;
      case kVibrato:
        pitch += kVibratoDepth * std::sin(kTwoPi * kVibratoHz * double(ch.clock) / kSampleRate);
        break;
      case kDrop:
        pitch = st.pitch - 36.0 * t;  // three octaves down over the step
        break;
      case kFadeIn:
        vol *= float(t);
        break;
      case kFadeOut:
        vol *= float(1.0 - t);
        break;
      case kArpFast:
      case kArpSlow:
        if (s) {
          int period = st.effect == kArpFast ? kArpFastPeriod : kArpSlowPeriod;
          int base = ch.step & ~3;
          int idx = int((ch.clock / period) % 4);
          pitch = s->steps[std::min(base + idx, kSfxSteps - 1)].pitch;
        }
        break;
      default:
        break;
    }
    target = vol;
    wave = st.wave;
    if (st.volume > 0) {
      ch.tail_pitch = pitch;
      ch.tail_wave = wave;
    }
  }

  ch.amp += (target - ch.amp) * kAmpSmoothing;
  float v = oscillator(ch, wave, pitch_to_hz(pitch)) * ch.amp;
  if (!active) return v;

  ++ch.clock;
  if (ch.live) return v;

  const Sfx& s = sfx_[ch.sfx];
  int step_len = std::max<int>(1, s.speed) * kSamplesPerSpeedUnit;
  if (++ch.pos >= step_len) {
    ch.pos = 0;
    if (st.volume > 0) ch.prev_pitch = st.pitch;
    ++ch.step;
    bool loops = s.loop_end > s.loop_start && !ch.releasing;
    if (loops && ch.step >= s.loop_end) ch.step = s.loop_start;
    if (ch.step >= ch.end) ch.sfx = -1;
  }
  return v;
}

void Audio::render(float* out, int frames) {
  std::lock_guard lock(mu_);
  for (int i = 0; i < frames; ++i) {
    tick_music();
    float mix = 0.f;
    for (int c = 0; c < kChannels; ++c) {
      float v = channel_sample(ch_[c]);
      if (ch_[c].music) v *= music_gain_;
      mix += v;
    }
    out[i] = std::clamp(mix * kMixGain * master_, -1.f, 1.f);
  }
}

ChannelStatus Audio::status(int channel) {
  std::lock_guard lock(mu_);
  if (channel < 0 || channel >= kChannels) return {};
  const auto& c = ch_[channel];
  return {c.sfx, c.sfx >= 0 ? c.step : 0};
}

int Audio::music_pattern() {
  std::lock_guard lock(mu_);
  return music_pattern_;
}

}  // namespace glim::audio
