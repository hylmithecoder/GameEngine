#include "../../../include/core_engine/AudioEngine.hpp"
#include "../../../include/core_engine/MediaComponents.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#define STB_VORBIS_HEADER_ONLY
#include <stb/stb_vorbis.c>

#if defined(__ANDROID__) && !defined(ILMEEE_HAS_FFMPEG)
#include "../../../include/core_engine/AndroidMedia.hpp"
#endif

#ifdef ILMEEE_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace ilmeee {

namespace {

std::string LowerExt(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext;
}

const SDL_AudioSpec kMixSpec = {SDL_AUDIO_F32, kAudioChannels, kAudioRate};

// Any SDL-describable PCM → the mixer's float stereo 48 kHz.
bool ConvertToMix(const SDL_AudioSpec &src, const void *data, int bytes,
                  AudioClip &out, std::string *error) {
  Uint8 *dst = nullptr;
  int dstLen = 0;
  if (!SDL_ConvertAudioSamples(&src, (const Uint8 *)data, bytes, &kMixSpec,
                               &dst, &dstLen)) {
    if (error)
      *error = SDL_GetError();
    return false;
  }
  out.samples.assign((const float *)dst,
                     (const float *)dst + dstLen / sizeof(float));
  SDL_free(dst);
  return true;
}

bool DecodeWav(const std::string &path, AudioClip &out, std::string *error) {
  SDL_AudioSpec spec;
  Uint8 *buf = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) {
    if (error)
      *error = SDL_GetError();
    return false;
  }
  const bool ok = ConvertToMix(spec, buf, (int)len, out, error);
  SDL_free(buf);
  return ok;
}

bool DecodeOgg(const std::string &path, AudioClip &out, std::string *error) {
  int channels = 0, rate = 0;
  short *pcm = nullptr;
  const int frames =
      stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &pcm);
  if (frames <= 0 || !pcm || channels <= 0) {
    if (error)
      *error = "not a readable Ogg Vorbis file";
    std::free(pcm);
    return false;
  }
  const SDL_AudioSpec spec = {SDL_AUDIO_S16, channels, rate};
  const bool ok = ConvertToMix(spec, pcm, frames * channels * 2, out, error);
  std::free(pcm);
  return ok;
}

#ifdef ILMEEE_HAS_FFMPEG
bool DecodeFFmpeg(const std::string &path, AudioClip &out,
                  std::string *error) {
  AVFormatContext *fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
    if (error)
      *error = "cannot open file";
    return false;
  }
  bool ok = false;
  AVCodecContext *dec = nullptr;
  SwrContext *swr = nullptr;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  do {
    if (avformat_find_stream_info(fmt, nullptr) < 0)
      break;
    const AVCodec *codec = nullptr;
    const int si =
        av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (si < 0 || !codec) {
      if (error)
        *error = "no audio stream";
      break;
    }
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[si]->codecpar);
    if (avcodec_open2(dec, codec, nullptr) < 0)
      break;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kAudioRate,
                            &dec->ch_layout, dec->sample_fmt,
                            dec->sample_rate, 0, nullptr) < 0 ||
        swr_init(swr) < 0)
      break;
    std::vector<float> buf;
    auto drain = [&]() {
      while (avcodec_receive_frame(dec, frame) == 0) {
        const int maxOut =
            swr_get_out_samples(swr, frame->nb_samples) + 256;
        buf.resize((size_t)maxOut * kAudioChannels);
        uint8_t *outPtr = (uint8_t *)buf.data();
        const int n = swr_convert(swr, &outPtr, maxOut,
                                  (const uint8_t **)frame->extended_data,
                                  frame->nb_samples);
        if (n > 0)
          out.samples.insert(out.samples.end(), buf.begin(),
                             buf.begin() + (size_t)n * kAudioChannels);
      }
    };
    while (av_read_frame(fmt, pkt) >= 0) {
      if (pkt->stream_index == si && avcodec_send_packet(dec, pkt) >= 0)
        drain();
      av_packet_unref(pkt);
    }
    avcodec_send_packet(dec, nullptr);
    drain();
    ok = !out.samples.empty();
    if (!ok && error)
      *error = "decoded no samples";
  } while (false);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  swr_free(&swr);
  avcodec_free_context(&dec);
  avformat_close_input(&fmt);
  return ok;
}
#endif

} // namespace

bool IsAudioClipPath(const std::string &path) {
  static const char *kExt[] = {".wav", ".ogg", ".mp3", ".flac",
                               ".m4a", ".aac", ".opus"};
  const std::string ext = LowerExt(path);
  return std::any_of(std::begin(kExt), std::end(kExt),
                     [&](const char *e) { return ext == e; });
}

bool IsVideoClipPath(const std::string &path) {
  static const char *kExt[] = {".mp4", ".mkv", ".webm", ".mov", ".avi"};
  const std::string ext = LowerExt(path);
  return std::any_of(std::begin(kExt), std::end(kExt),
                     [&](const char *e) { return ext == e; });
}

// ---------------------------------------------------------------------------
// PcmRing
// ---------------------------------------------------------------------------

void PcmRing::Push(const float *interleaved, size_t frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.insert(data_.end(), interleaved, interleaved + frames * kAudioChannels);
}

size_t PcmRing::Pull(float *out, size_t maxFrames) {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t avail = (data_.size() - head_) / kAudioChannels;
  const size_t n = std::min(avail, maxFrames);
  std::copy_n(data_.begin() + (std::ptrdiff_t)head_, n * kAudioChannels, out);
  head_ += n * kAudioChannels;
  // Compact once the consumed prefix dominates.
  if (head_ > 65536 && head_ * 2 > data_.size()) {
    data_.erase(data_.begin(), data_.begin() + (std::ptrdiff_t)head_);
    head_ = 0;
  }
  return n;
}

size_t PcmRing::Frames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return (data_.size() - head_) / kAudioChannels;
}

void PcmRing::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.clear();
  head_ = 0;
  finished_ = false;
}

void PcmRing::SetFinished(bool f) {
  std::lock_guard<std::mutex> lock(mutex_);
  finished_ = f;
}

bool PcmRing::Finished() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return finished_;
}

// ---------------------------------------------------------------------------
// AudioEngine
// ---------------------------------------------------------------------------

AudioEngine::~AudioEngine() {
  StopAll();
  if (device_ != 0) {
    SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
}

bool AudioEngine::Ensure() {
  if (device_ != 0)
    return true;
  if (initTried_)
    return false;
  initTried_ = true;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_Log("[Audio] SDL audio init failed: %s", SDL_GetError());
    return false;
  }
  device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kMixSpec);
  if (device_ == 0) {
    SDL_Log("[Audio] no playback device: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }
  SDL_ResumeAudioDevice(device_);
  return true;
}

std::shared_ptr<const AudioClip>
AudioEngine::LoadClip(const std::string &path, std::string *error) {
  auto it = clips_.find(path);
  if (it != clips_.end())
    return it->second;
  auto clip = std::make_shared<AudioClip>();
  const std::string ext = LowerExt(path);
  bool ok = false;
  if (ext == ".wav")
    ok = DecodeWav(path, *clip, error);
  else if (ext == ".ogg")
    ok = DecodeOgg(path, *clip, error);
  else {
#if defined(ILMEEE_HAS_FFMPEG)
    ok = DecodeFFmpeg(path, *clip, error);
#elif defined(__ANDROID__)
    ok = android_media::DecodeAudioFile(path, clip->samples, error);
#else
    if (error)
      *error = ext + " needs FFmpeg, which this build lacks (use .wav/.ogg)";
#endif
  }
  if (!ok)
    return nullptr;
  clips_[path] = clip;
  return clip;
}

VoiceId AudioEngine::PlayClip(std::shared_ptr<const AudioClip> clip,
                              bool loop) {
  if (!clip || clip->Frames() == 0 || !Ensure())
    return 0;
  SDL_AudioStream *s = SDL_CreateAudioStream(&kMixSpec, &kMixSpec);
  if (!s || !SDL_BindAudioStream(device_, s)) {
    if (s)
      SDL_DestroyAudioStream(s);
    return 0;
  }
  Voice v;
  v.stream = s;
  v.clip = std::move(clip);
  v.loop = loop;
  const VoiceId id = nextId_++;
  voices_.emplace(id, std::move(v));
  return id;
}

VoiceId AudioEngine::PlayStream(std::shared_ptr<PcmRing> ring) {
  if (!ring || !Ensure())
    return 0;
  SDL_AudioStream *s = SDL_CreateAudioStream(&kMixSpec, &kMixSpec);
  if (!s || !SDL_BindAudioStream(device_, s)) {
    if (s)
      SDL_DestroyAudioStream(s);
    return 0;
  }
  Voice v;
  v.stream = s;
  v.ring = std::move(ring);
  const VoiceId id = nextId_++;
  voices_.emplace(id, std::move(v));
  return id;
}

void AudioEngine::DestroyVoice(Voice &v) {
  if (v.stream)
    SDL_DestroyAudioStream(v.stream);
  v.stream = nullptr;
}

void AudioEngine::StopVoice(VoiceId id) {
  auto it = voices_.find(id);
  if (it == voices_.end())
    return;
  DestroyVoice(it->second);
  voices_.erase(it);
}

void AudioEngine::StopAll() {
  for (auto &[id, v] : voices_)
    DestroyVoice(v);
  voices_.clear();
}

bool AudioEngine::IsVoiceActive(VoiceId id) const {
  return voices_.count(id) != 0;
}

void AudioEngine::SetVoiceGains(VoiceId id, float left, float right,
                                bool mono) {
  auto it = voices_.find(id);
  if (it == voices_.end())
    return;
  it->second.gainL = left;
  it->second.gainR = right;
  it->second.mono = mono;
}

size_t AudioEngine::QueuedFrames(const Voice &v) const {
  const int bytes = SDL_GetAudioStreamQueued(v.stream);
  return bytes > 0 ? (size_t)bytes / (sizeof(float) * kAudioChannels) : 0;
}

double AudioEngine::VoicePlayedSeconds(VoiceId id) const {
  auto it = voices_.find(id);
  if (it == voices_.end())
    return 0.0;
  const Voice &v = it->second;
  const uint64_t queued = QueuedFrames(v);
  const uint64_t played = v.framesPushed > queued ? v.framesPushed - queued : 0;
  return (double)played / kAudioRate;
}

void AudioEngine::SetPaused(bool paused) {
  if (paused == paused_ || device_ == 0)
    return;
  paused_ = paused;
  if (paused)
    SDL_PauseAudioDevice(device_);
  else
    SDL_ResumeAudioDevice(device_);
}

void AudioEngine::Update() {
  constexpr size_t kLeadFrames = kAudioRate / 10; // keep ~100 ms queued
  constexpr size_t kChunk = 1024;
  for (auto it = voices_.begin(); it != voices_.end();) {
    Voice &v = it->second;
    while (!v.done && QueuedFrames(v) < kLeadFrames) {
      scratch_.resize(kChunk * kAudioChannels);
      size_t n = 0;
      if (v.clip) {
        const size_t total = v.clip->Frames();
        while (n < kChunk) {
          if (v.cursor >= total) {
            if (!v.loop) {
              v.done = true;
              break;
            }
            v.cursor = 0;
          }
          const size_t take = std::min(kChunk - n, total - v.cursor);
          std::copy_n(v.clip->samples.data() + v.cursor * kAudioChannels,
                      take * kAudioChannels,
                      scratch_.data() + n * kAudioChannels);
          n += take;
          v.cursor += take;
        }
      } else if (v.ring) {
        n = v.ring->Pull(scratch_.data(), kChunk);
        if (n == 0) {
          if (v.ring->Finished())
            v.done = true;
          break; // underrun: wait for the decoder
        }
      }
      if (n == 0)
        break;
      for (size_t f = 0; f < n; ++f) {
        float &l = scratch_[f * 2], &r = scratch_[f * 2 + 1];
        if (v.mono)
          l = r = 0.5f * (l + r);
        l *= v.gainL;
        r *= v.gainR;
      }
      SDL_PutAudioStreamData(v.stream, scratch_.data(),
                             (int)(n * kAudioChannels * sizeof(float)));
      v.framesPushed += n;
    }
    if (v.done && QueuedFrames(v) == 0) {
      DestroyVoice(v);
      it = voices_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace ilmeee
