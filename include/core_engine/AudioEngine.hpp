#pragma once
// Small game-audio mixer on top of SDL3. One private playback device; each
// voice is an SDL_AudioStream bound to it (SDL mixes them). Voices are fed a
// little ahead of the device (~100 ms) from the main thread, which is where
// per-voice gain and panning are applied — so a moving 3D source updates
// with that much latency, and no audio callback/thread of our own is needed.
//
// Everything is decoded to interleaved float stereo at 48 kHz:
//   .wav  SDL_LoadWAV            (all platforms)
//   .ogg  stb_vorbis             (all platforms)
//   other FFmpeg                 (only where ILMEEE_HAS_FFMPEG is defined)

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

namespace ilmeee {

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

struct AudioClip {
  std::vector<float> samples; // interleaved stereo
  size_t Frames() const { return samples.size() / kAudioChannels; }
};

// Thread-safe stereo FIFO: a video decoder thread pushes, the mixer pulls.
class PcmRing {
public:
  void Push(const float *interleaved, size_t frames);
  size_t Pull(float *out, size_t maxFrames);
  size_t Frames() const;
  void Clear();
  // Producer has no more data (end of stream).
  void SetFinished(bool f);
  bool Finished() const;

private:
  mutable std::mutex mutex_;
  std::vector<float> data_; // unread samples start at head_
  size_t head_ = 0;
  bool finished_ = false;
};

using VoiceId = int;

// Per-ear processing of a voice. Changes are ramped over the next chunk so a
// moving source never clicks.
struct VoiceSpatial {
  float gainL = 1.0f, gainR = 1.0f;
  // Interaural time difference: the far ear hears the sound this many
  // samples later (fractional; up to kMaxEarDelay).
  float delayL = 0.0f, delayR = 0.0f;
  // One-pole low-pass coefficient per ear (1 = off): head shadow on the far
  // ear, duller sound from behind.
  float lowpassL = 1.0f, lowpassR = 1.0f;
  bool mono = false; // downmix first: a 3D source is a point
};
constexpr int kMaxEarDelay = 48; // samples (1 ms at 48 kHz)

// DSP state of one voice: ear delay lines (mono input) + low-pass filters.
struct SpatialState {
  VoiceSpatial current;
  bool started = false; // first chunk takes the target without ramping
  float history[64] = {};
  unsigned writePos = 0; // wraps; only ever used masked
  float lpL = 0.0f, lpR = 0.0f;
};

// Applies `target` to n interleaved stereo frames in place, ramping from the
// state's current settings. Mono voices get delays + filters, stereo voices
// only gains.
void Spatialize(SpatialState &state, const VoiceSpatial &target,
                float *frames, size_t n);

class AudioEngine {
public:
  AudioEngine() = default;
  ~AudioEngine();
  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;

  // Opens the device on first use; false if there is no audio.
  bool Ensure();

  // Decodes (and caches) a whole file. nullptr + error on failure.
  std::shared_ptr<const AudioClip> LoadClip(const std::string &path,
                                            std::string *error = nullptr);
  void ClearClipCache() { clips_.clear(); }

  VoiceId PlayClip(std::shared_ptr<const AudioClip> clip, bool loop);
  VoiceId PlayStream(std::shared_ptr<PcmRing> ring);
  void StopVoice(VoiceId id);
  void StopAll();
  bool IsVoiceActive(VoiceId id) const;
  // Applied to everything pushed from now on (ramped in).
  void SetVoiceSpatial(VoiceId id, const VoiceSpatial &spatial);
  // Plain stereo volume (2D voices).
  void SetVoiceGains(VoiceId id, float left, float right, bool mono);
  // Seconds of this voice the device has actually played.
  double VoicePlayedSeconds(VoiceId id) const;

  void SetPaused(bool paused);
  // Keep every voice's queue topped up. Call once per frame.
  void Update();

private:
  struct Voice {
    SDL_AudioStream *stream = nullptr;
    std::shared_ptr<const AudioClip> clip;
    std::shared_ptr<PcmRing> ring;
    size_t cursor = 0;
    bool loop = false;
    bool done = false; // source exhausted; removed once drained
    VoiceSpatial target;
    SpatialState dsp;
    uint64_t framesPushed = 0;
  };

  void DestroyVoice(Voice &v);
  size_t QueuedFrames(const Voice &v) const;

  uint32_t device_ = 0;
  bool initTried_ = false;
  bool paused_ = false;
  VoiceId nextId_ = 1;
  std::unordered_map<VoiceId, Voice> voices_;
  std::unordered_map<std::string, std::shared_ptr<const AudioClip>> clips_;
  std::vector<float> scratch_;
};

} // namespace ilmeee
