#pragma once
// Android media decoding through the platform's MediaCodec (NDK, libmediandk)
// — hardware decoders, so the Android player needs no FFmpeg. Used by
// AudioEngine (whole-file decode of .mp3/.m4a/.aac/.flac/...) and by the
// Android VideoDecoder. Only compiled into the Android player.

#if defined(__ANDROID__)

#include <cstdint>
#include <string>
#include <vector>

struct AMediaExtractor;
struct AMediaFormat;
struct SDL_AudioStream;

namespace ilmeee::android_media {

// AMediaExtractor over a plain file (game files are extracted from the APK
// to internal storage, so everything is a real path).
class Extractor {
public:
  ~Extractor();
  bool Open(const std::string &path, std::string *error);
  AMediaExtractor *get() const { return ex_; }
  // First track whose MIME starts with `prefix` ("video/", "audio/"); -1 if
  // none. `format` (owned by the caller, AMediaFormat_delete) and `mime`
  // are filled on success.
  int FindTrack(const char *prefix, AMediaFormat **format, std::string *mime);

private:
  AMediaExtractor *ex_ = nullptr;
  int fd_ = -1;
};

// Decoder PCM (16-bit or float, any rate/channels) → float stereo 48 kHz,
// resampled continuously across chunks by an unbound SDL_AudioStream.
class PcmConverter {
public:
  ~PcmConverter();
  void SetFormat(int sampleRate, int channels, bool isFloat);
  // Appends converted samples (interleaved stereo) to `out`.
  void Convert(const uint8_t *data, size_t bytes, std::vector<float> &out);
  void Flush(std::vector<float> &out); // end of stream: drain the resampler
  bool Ready() const { return stream_ != nullptr; }

private:
  void Drain(std::vector<float> &out);
  SDL_AudioStream *stream_ = nullptr;
  int rate_ = 0, channels_ = 0;
  bool isFloat_ = false;
};

// Reads the PCM layout from a decoder output format.
void PcmFormatOf(AMediaFormat *format, int &rate, int &channels,
                 bool &isFloat);

bool DecodeAudioFile(const std::string &path, std::vector<float> &out,
                     std::string *error);

} // namespace ilmeee::android_media

#endif
