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
  // left/right gains applied to everything pushed from now on. `mono`
  // downmixes the voice first (3D sources are panned as a point).
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
    float gainL = 1.0f, gainR = 1.0f;
    bool mono = false;
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
