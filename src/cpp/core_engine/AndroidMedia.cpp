#if defined(__ANDROID__)

#include "../../../include/core_engine/AndroidMedia.hpp"
#include "../../../include/core_engine/AudioEngine.hpp"
#include <SDL3/SDL.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ilmeee::android_media {

namespace {
constexpr int64_t kTimeoutUs = 10000;
// MediaFormat "pcm-encoding" values (android.media.AudioFormat).
constexpr int32_t kEncodingPcmFloat = 4;
} // namespace

// ---------------------------------------------------------------------------
// Extractor
// ---------------------------------------------------------------------------

Extractor::~Extractor() {
  if (ex_)
    AMediaExtractor_delete(ex_);
  if (fd_ >= 0)
    close(fd_);
}

bool Extractor::Open(const std::string &path, std::string *error) {
  fd_ = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  struct stat st {};
  if (fd_ < 0 || fstat(fd_, &st) != 0) {
    if (error)
      *error = "cannot open file";
    return false;
  }
  ex_ = AMediaExtractor_new();
  if (!ex_ || AMediaExtractor_setDataSourceFd(ex_, fd_, 0, st.st_size) !=
                  AMEDIA_OK) {
    if (error)
      *error = "unsupported container";
    return false;
  }
  return true;
}

int Extractor::FindTrack(const char *prefix, AMediaFormat **format,
                         std::string *mime) {
  const size_t n = AMediaExtractor_getTrackCount(ex_);
  const std::string want(prefix);
  for (size_t i = 0; i < n; ++i) {
    AMediaFormat *f = AMediaExtractor_getTrackFormat(ex_, i);
    const char *m = nullptr;
    if (f && AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &m) && m &&
        std::string(m).rfind(want, 0) == 0) {
      if (mime)
        *mime = m;
      *format = f;
      return (int)i;
    }
    if (f)
      AMediaFormat_delete(f);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// PcmConverter
// ---------------------------------------------------------------------------

PcmConverter::~PcmConverter() {
  if (stream_)
    SDL_DestroyAudioStream(stream_);
}

void PcmConverter::SetFormat(int sampleRate, int channels, bool isFloat) {
  if (stream_ && sampleRate == rate_ && channels == channels_ &&
      isFloat == isFloat_)
    return;
  if (stream_)
    SDL_DestroyAudioStream(stream_);
  rate_ = sampleRate;
  channels_ = channels;
  isFloat_ = isFloat;
  const SDL_AudioSpec src = {isFloat ? SDL_AUDIO_F32 : SDL_AUDIO_S16,
                             channels, sampleRate};
  const SDL_AudioSpec dst = {SDL_AUDIO_F32, kAudioChannels, kAudioRate};
  stream_ = SDL_CreateAudioStream(&src, &dst);
}

void PcmConverter::Drain(std::vector<float> &out) {
  for (;;) {
    const int avail = SDL_GetAudioStreamAvailable(stream_);
    if (avail <= 0)
      return;
    const size_t at = out.size();
    out.resize(at + (size_t)avail / sizeof(float));
    const int got = SDL_GetAudioStreamData(stream_, out.data() + at, avail);
    out.resize(at + (got > 0 ? (size_t)got / sizeof(float) : 0));
    if (got <= 0)
      return;
  }
}

void PcmConverter::Convert(const uint8_t *data, size_t bytes,
                           std::vector<float> &out) {
  if (!stream_ || !data || bytes == 0)
    return;
  SDL_PutAudioStreamData(stream_, data, (int)bytes);
  Drain(out);
}

void PcmConverter::Flush(std::vector<float> &out) {
  if (!stream_)
    return;
  SDL_FlushAudioStream(stream_);
  Drain(out);
}

void PcmFormatOf(AMediaFormat *format, int &rate, int &channels,
                 bool &isFloat) {
  int32_t r = 44100, c = 2, enc = 2;
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &r);
  AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &c);
  AMediaFormat_getInt32(format, "pcm-encoding", &enc);
  rate = r > 0 ? r : 44100;
  channels = c > 0 ? c : 2;
  isFloat = enc == kEncodingPcmFloat;
}

// ---------------------------------------------------------------------------
// Whole-file audio decode
// ---------------------------------------------------------------------------

bool DecodeAudioFile(const std::string &path, std::vector<float> &out,
                     std::string *error) {
  Extractor ex;
  if (!ex.Open(path, error))
    return false;
  AMediaFormat *format = nullptr;
  std::string mime;
  const int track = ex.FindTrack("audio/", &format, &mime);
  if (track < 0) {
    if (error)
      *error = "no audio track";
    return false;
  }
  AMediaExtractor_selectTrack(ex.get(), (size_t)track);
  AMediaCodec *codec = AMediaCodec_createDecoderByType(mime.c_str());
  if (!codec ||
      AMediaCodec_configure(codec, format, nullptr, nullptr, 0) != AMEDIA_OK ||
      AMediaCodec_start(codec) != AMEDIA_OK) {
    if (error)
      *error = "no decoder for " + mime;
    if (codec)
      AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    return false;
  }
  int rate, channels;
  bool isFloat;
  PcmFormatOf(format, rate, channels, isFloat);
  AMediaFormat_delete(format);
  PcmConverter conv;
  conv.SetFormat(rate, channels, isFloat);

  bool inputDone = false, outputDone = false;
  while (!outputDone) {
    if (!inputDone) {
      const ssize_t in = AMediaCodec_dequeueInputBuffer(codec, kTimeoutUs);
      if (in >= 0) {
        size_t cap = 0;
        uint8_t *buf = AMediaCodec_getInputBuffer(codec, (size_t)in, &cap);
        const ssize_t n = AMediaExtractor_readSampleData(ex.get(), buf, cap);
        if (n < 0) {
          AMediaCodec_queueInputBuffer(codec, (size_t)in, 0, 0, 0,
                                       AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          inputDone = true;
        } else {
          AMediaCodec_queueInputBuffer(
              codec, (size_t)in, 0, (size_t)n,
              (uint64_t)AMediaExtractor_getSampleTime(ex.get()), 0);
          AMediaExtractor_advance(ex.get());
        }
      }
    }
    AMediaCodecBufferInfo info;
    const ssize_t o = AMediaCodec_dequeueOutputBuffer(codec, &info, kTimeoutUs);
    if (o >= 0) {
      size_t size = 0;
      uint8_t *buf = AMediaCodec_getOutputBuffer(codec, (size_t)o, &size);
      if (buf && info.size > 0)
        conv.Convert(buf + info.offset, (size_t)info.size, out);
      AMediaCodec_releaseOutputBuffer(codec, (size_t)o, false);
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
        outputDone = true;
    } else if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      AMediaFormat *f = AMediaCodec_getOutputFormat(codec);
      PcmFormatOf(f, rate, channels, isFloat);
      AMediaFormat_delete(f);
      conv.SetFormat(rate, channels, isFloat);
    }
  }
  conv.Flush(out);
  AMediaCodec_stop(codec);
  AMediaCodec_delete(codec);
  if (out.empty() && error)
    *error = "decoded no samples";
  return !out.empty();
}

} // namespace ilmeee::android_media

#endif
