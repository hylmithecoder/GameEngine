#pragma once
// Plays the Audio Source and Video Player components of a scene. Shared by
// the editor (Play mode) and IlmeeePlayer; both call Update() once per
// frame, before the scene is rendered, with the current play state.
//
//  Stopped  editor only: video screens show their first frame (poster),
//           nothing is heard except an explicit Preview().
//  Playing  sources with Play On Start begin when entering this state;
//           3D sources are mixed relative to the game camera.
//  Paused   everything holds (device paused, video clocks frozen).
//
// Slots are keyed by mesh index. The scene structure is locked while
// playing, and in the editor a renumbered slot just reloads its clip.

#include "AudioEngine.hpp"
#include "VideoDecoder.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SceneRenderer;

namespace ilmeee {

class MediaSystem {
public:
  enum class State { Stopped, Playing, Paused };

  MediaSystem() = default;
  ~MediaSystem() = default;

  // Relative clip paths resolve against this (project or game folder).
  void SetProjectRoot(const std::filesystem::path &root) { root_ = root; }
  std::string ResolvePath(const std::string &clip) const;

  void Update(SceneRenderer &renderer, State state, float deltaTime);
  // Releases decoders, voices and the renderer textures it created. Call
  // before the renderer goes away.
  void Shutdown(SceneRenderer &renderer);

  // Editor audition of a clip in 2D while stopped.
  bool Preview(const std::string &clip, std::string *error);
  void StopPreview();
  bool IsPreviewing() const;

  // Last load/playback error for a clip ("" = fine / not tried).
  std::string ErrorFor(const std::string &clip) const;
  static bool VideoSupported() { return VideoDecoder::Available(); }

private:
  struct VideoSlot {
    std::string clip;
    bool loop = false;
    bool playing = false; // false: poster mode
    bool failed = false;
    std::unique_ptr<VideoDecoder> decoder;
    int texture = 0;
    int texW = 0, texH = 0;
    VoiceId voice = 0;
    double clock = 0.0;
    bool shown = false;
    double lastFramePts = 0.0; // newest frame handed to the screen
    bool warnedSlow = false;
  };

  void Start(SceneRenderer &renderer);
  void StopPlayback(SceneRenderer &renderer);
  void ResetVideo(SceneRenderer &renderer, VideoSlot &slot);
  void UpdateVideos(SceneRenderer &renderer, bool advance, float dt);
  void UpdateAudioGains(SceneRenderer &renderer);
  void SetError(const std::string &clip, const std::string &message);

  std::filesystem::path root_;
  AudioEngine audio_;
  State state_ = State::Stopped;
  std::vector<VideoSlot> videos_;
  std::vector<VoiceId> sources_; // per mesh while playing, 0 = none
  VoiceId preview_ = 0;
  std::unordered_map<std::string, std::string> errors_;
};

} // namespace ilmeee
