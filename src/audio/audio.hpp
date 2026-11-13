#pragma once

#include <array>
#include <cstdint>
#include <mutex>

#include "core/memory.hpp"

namespace glim::audio {

inline constexpr int kSampleRate = 48000;
inline constexpr int kSamplesPerFrame = kSampleRate / kFps;
// A step lasts speed * this many samples (7.5 ms per speed unit).
inline constexpr int kSamplesPerSpeedUnit = 360;

enum Wave : uint8_t {
  kSquare, kPulse, kTriangle, kSaw, kNoise, kOrgan, kSine, kSoftSquare,
  kCustom0,  // 8..15 map to the custom wavetables in RAM
};

enum Effect : uint8_t {
  kNone, kSlide, kVibrato, kDrop, kFadeIn, kFadeOut, kArpFast, kArpSlow,
};

struct Step {
  uint8_t pitch = 0;   // semitones above C2, 0-63
  uint8_t wave = 0;    // 0-15
  uint8_t volume = 0;  // 0-7, 0 is a rest
  uint8_t effect = 0;  // 0-7

  static Step decode(uint16_t v) {
    return {uint8_t(v & 63), uint8_t((v >> 6) & 15), uint8_t((v >> 10) & 7), uint8_t((v >> 13) & 7)};
  }
  uint16_t encode() const {
    return uint16_t((pitch & 63) | ((wave & 15) << 6) | ((volume & 7) << 10) | ((effect & 7) << 13));
  }
};

struct Sfx {
  uint8_t speed = 16;
  uint8_t loop_start = 0;
  uint8_t loop_end = 0;  // loops while loop_end > loop_start
  uint8_t flags = 0;
  std::array<Step, kSfxSteps> steps{};
};

Sfx read_sfx(const Memory& mem, int n);
void write_sfx(Memory& mem, int n, const Sfx& sfx);
double pitch_to_hz(double pitch);

struct ChannelStatus {
  int sfx = -1;
  int step = 0;
};

// Real-time synthesizer for the four sound channels. The game thread issues
// commands and the audio device thread calls render(); a mutex guards both.
// Sound data is snapshotted from RAM once per frame (sync) so the audio thread
// never reads console memory directly.
class Audio {
 public:
  Audio();

  void sync(const Memory& mem);
  void reset();

  // n >= 0 plays; n == -1 stops; n == -2 releases a looping sfx so it plays out.
  void sfx(int n, int channel = -1, int offset = 0, int length = kSfxSteps);
  void music(int pattern, int fade_ms = 0, uint8_t channel_mask = 0x0F);
  // Sustained live note for instrument-style playback; pitch < 0 stops it.
  void note(int channel, int pitch, int wave, int volume, int effect = 0);

  void render(float* out, int frames);

  ChannelStatus status(int channel);
  int music_pattern();
  void set_master_volume(float v);

 private:
  struct Channel {
    int sfx = -1;
    int step = 0;
    int end = kSfxSteps;
    int pos = 0;  // samples into the current step
    bool releasing = false;
    bool live = false;
    Step live_step{};
    bool music = false;
    double phase = 0.0;
    double noise_phase = 0.0;
    uint32_t lfsr = 0x7A5Fu;
    float noise_value = 0.f;
    double prev_pitch = 0.0;
    float amp = 0.f;
    double tail_pitch = 0.0;  // keeps the release tail on the last heard tone
    int tail_wave = 0;
    uint64_t clock = 0;  // samples since the channel started, for vibrato/arpeggio
  };

  void start_sfx(Channel& ch, int n, int offset, int length, bool music);
  void start_pattern(int pattern);
  float channel_sample(Channel& ch);
  float oscillator(Channel& ch, int wave, double hz);
  int pick_channel() const;
  void tick_music();

  std::mutex mu_;
  std::array<Sfx, kSfxCount> sfx_{};
  std::array<std::array<int8_t, kWaveSamples>, kWavetables> waves_{};
  std::array<uint8_t, addr::kMusicSize> patterns_{};
  std::array<Channel, kChannels> ch_{};

  int music_pattern_ = -1;
  int music_pos_ = 0;
  int music_len_ = 0;
  uint8_t music_mask_ = 0;
  float music_gain_ = 1.f;
  float music_gain_step_ = 0.f;
  bool music_stopping_ = false;
  float master_ = 1.f;
};

}  // namespace glim::audio
