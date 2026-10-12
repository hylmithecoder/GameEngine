#pragma once
// Background video decoder for Video Player components. A worker thread
// demuxes with FFmpeg, converts frames to RGBA (scaled down to fit
// kMaxVideoWidth x kMaxVideoHeight) into a short queue, and pushes the
// sound track as float stereo 48 kHz into a PcmRing for AudioEngine.
// The main thread takes whichever frame is due for its playback clock.
//
// Backends: FFmpeg (ILMEEE_HAS_FFMPEG, desktop) or MediaCodec (Android). With
// neither, Open() fails with a message and the screen keeps its material.

#include "AudioEngine.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ilmeee {

constexpr int kMaxVideoWidth = 1280;
constexpr int kMaxVideoHeight = 720;

struct VideoFrame {
  double pts = 0.0; // seconds on the decoder's continuous (looping) timeline
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
};

class VideoDecoder {
public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;

  static bool Available(); // compiled with FFmpeg

  bool Open(const std::string &path, bool loop, std::string *error);
  void Close();

  // The sound track, or nullptr when the file has none.
  std::shared_ptr<PcmRing> Audio() const { return audio_; }

  // Moves the newest frame with pts <= clock into `out`, dropping older
  // ones. `first`: return the first decoded frame whatever its pts (poster).
  bool TakeFrame(double clock, VideoFrame &out, bool first = false);
  // End of a non-looping file reached and every frame handed out.
  bool Finished() const;

private:
  void Run(bool loop);

  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<PcmRing> audio_;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<VideoFrame> frames_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> eof_{false};
};

} // namespace ilmeee
