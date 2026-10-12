// AudioEngine + VideoDecoder without a window: WAV/OGG decoding, the PCM
// ring, voices draining through SDL's dummy audio driver, and (when given a
// file) video frames, sound track and looping.
//
//   cmake --build build --target TestMedia
//   SDL_AUDIODRIVER=dummy ./build/bin/TestMedia [video.mp4] [clip.ogg]

#include "../include/core_engine/AudioEngine.hpp"
#include "../include/core_engine/MediaComponents.hpp"
#include "../include/core_engine/VideoDecoder.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace ilmeee;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                          \
  } while (0)

// 16-bit mono PCM WAV with a 440 Hz tone.
static void WriteWav(const fs::path &p, int rate, float seconds) {
  const uint32_t n = (uint32_t)(rate * seconds);
  std::ofstream f(p, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write((const char *)&v, 4); };
  auto u16 = [&](uint16_t v) { f.write((const char *)&v, 2); };
  f.write("RIFF", 4);
  u32(36 + n * 2);
  f.write("WAVEfmt ", 8);
  u32(16);
  u16(1); // PCM
  u16(1); // mono
  u32((uint32_t)rate);
  u32((uint32_t)rate * 2);
  u16(2);
  u16(16);
  f.write("data", 4);
  u32(n * 2);
  for (uint32_t i = 0; i < n; ++i)
    u16((uint16_t)(int16_t)(12000 * std::sin(6.2831853 * 440.0 * i / rate)));
}

int main(int argc, char **argv) {
  const fs::path dir = fs::temp_directory_path() / "ilmeee-media-test";
  fs::create_directories(dir);

  // 1. Extension checks.
  CHECK(IsAudioClipPath("a/b/Rain.OGG"));
  CHECK(IsAudioClipPath("x.wav"));
  CHECK(!IsAudioClipPath("x.mp4"));
  CHECK(IsVideoClipPath("intro.MP4"));
  CHECK(!IsVideoClipPath("intro.png"));

  // 2. PCM ring keeps order and reports what is left.
  {
    PcmRing ring;
    float in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ring.Push(in, 4);
    float out[8] = {};
    CHECK(ring.Pull(out, 3) == 3);
    CHECK(out[0] == 1 && out[5] == 6);
    CHECK(ring.Frames() == 1);
    CHECK(ring.Pull(out, 3) == 1 && out[0] == 7);
    CHECK(!ring.Finished());
  }

  AudioEngine engine;

  // 3. WAV resampled to 48 kHz stereo.
  {
    const fs::path wav = dir / "tone.wav";
    WriteWav(wav, 22050, 0.5f);
    std::string err;
    auto clip = engine.LoadClip(wav.string(), &err);
    CHECK(clip != nullptr);
    if (clip) {
      const double frames = (double)clip->Frames();
      CHECK(std::fabs(frames - 24000.0) < 24000.0 * 0.02);
      float peak = 0.0f;
      for (float s : clip->samples)
        peak = std::max(peak, std::fabs(s));
      CHECK(peak > 0.3f && peak < 0.4f); // 12000 / 32768
    } else {
      std::fprintf(stderr, "  wav: %s\n", err.c_str());
    }
    CHECK(engine.LoadClip(wav.string()) == clip); // cached

    // 4. A voice drains through the (dummy) device in real time.
    if (clip && engine.Ensure()) {
      const VoiceId v = engine.PlayClip(clip, false);
      CHECK(v != 0);
      const auto t0 = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - t0 <
             std::chrono::milliseconds(300)) {
        engine.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      const double played = engine.VoicePlayedSeconds(v);
      CHECK(played > 0.1 && played < 0.5);
      // The 0.5 s clip finishes and the voice is retired.
      const auto t1 = std::chrono::steady_clock::now();
      while (engine.IsVoiceActive(v) &&
             std::chrono::steady_clock::now() - t1 < std::chrono::seconds(2)) {
        engine.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      CHECK(!engine.IsVoiceActive(v));
    } else {
      std::fprintf(stderr, "  (no audio device; run with "
                           "SDL_AUDIODRIVER=dummy for voice checks)\n");
    }
  }

  // 5. Missing / bogus files fail cleanly.
  {
    std::string err;
    CHECK(engine.LoadClip((dir / "nope.wav").string(), &err) == nullptr);
    CHECK(!err.empty());
  }

  // 6. Optional: a video file.
  if (argc > 1 && VideoDecoder::Available()) {
    for (bool loop : {false, true}) {
      VideoDecoder dec;
      std::string err;
      CHECK(dec.Open(argv[1], loop, &err));
      VideoFrame f;
      int frames = 0;
      double lastPts = -1.0, maxPts = 0.0;
      bool monotonic = true;
      // Simulated 4 s of playback at 60 fps.
      for (int step = 0; step < 240; ++step) {
        const double clock = step / 60.0;
        for (int tries = 0; tries < 50; ++tries) {
          if (dec.TakeFrame(clock, f, frames == 0)) {
            ++frames;
            monotonic &= f.pts > lastPts;
            lastPts = f.pts;
            maxPts = std::max(maxPts, f.pts);
            break;
          }
          if (dec.Finished())
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (auto ring = dec.Audio()) {
          float sink[4096];
          ring->Pull(sink, 800); // consume like the mixer would
        }
      }
      CHECK(frames > 30);
      CHECK(monotonic);
      CHECK(f.width > 0 && f.width <= kMaxVideoWidth);
      CHECK(f.height > 0 && f.height <= kMaxVideoHeight);
      CHECK(f.rgba.size() == (size_t)f.width * f.height * 4);
      std::printf("  video loop=%d: %d frames, %dx%d, last pts %.2f, audio %s\n",
                  loop, frames, f.width, f.height, maxPts,
                  dec.Audio() ? "yes" : "no");
      if (loop)
        CHECK(maxPts > 3.0); // kept going past a short clip's end
      else
        CHECK(dec.Finished() || maxPts > 3.0);
    }
  }

  // 7. Optional: an Ogg Vorbis clip.
  if (argc > 2) {
    std::string err;
    auto clip = engine.LoadClip(argv[2], &err);
    CHECK(clip && clip->Frames() > 0);
    if (!clip)
      std::fprintf(stderr, "  ogg: %s\n", err.c_str());
  }

  fs::remove_all(dir);
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
