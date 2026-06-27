#pragma once
#include "../core_engine/Debugger.hpp"
#include "../core_engine/SceneRenderer.hpp"
#include <string>

namespace Ilmeee {

// Play/Pause/Stop controller for the editor. On Play the current
// scene state is snapshot-serialised to JSON; on Stop it is restored
// so the user gets a clean hot-reload back to the editor state.
class Builder {
public:
  enum class PlayState { Stopped, Playing, Paused };

  Builder() = default;
  ~Builder() = default;

  // Must be called once with a valid SceneRenderer pointer.
  void Init(SceneRenderer *renderer);

  // Enter play mode. Snapshots the current scene.
  void Play();

  // Pause play mode (freezes physics / script updates).
  void Pause();

  // Resume from paused state.
  void Resume();

  // Stop play mode. Restores scene from snapshot.
  void Stop();

  PlayState GetState() const { return state_; }
  bool IsPlaying() const { return state_ == PlayState::Playing; }
  bool IsPaused() const { return state_ == PlayState::Paused; }
  bool IsStopped() const { return state_ == PlayState::Stopped; }

private:
  SceneRenderer *renderer_ = nullptr;
  PlayState state_ = PlayState::Stopped;

  // Snapshot storage: serialised mesh transforms before entering play.
  struct MeshSnapshot {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
  };
  std::vector<MeshSnapshot> snapshot_;
};

} // namespace Ilmeee