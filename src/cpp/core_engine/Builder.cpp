#include "../../include/core_engine/Builder.hpp"

namespace Ilmeee {

void Builder::Init(SceneRenderer *renderer) { renderer_ = renderer; }

void Builder::Play() {
  if (!renderer_)
    return;

  // Snapshot current transforms so we can restore on Stop
  snapshot_.clear();
  for (const auto &mesh : renderer_->meshes3d) {
    MeshSnapshot snap;
    snap.position = mesh.userPosition;
    snap.rotation = mesh.userRotation;
    snap.scale = mesh.userScale;
    snapshot_.push_back(snap);
  }

  state_ = PlayState::Playing;
  Debug::Log("Game started (Play mode)", Debug::LogLevel::SUCCESS);
}

void Builder::Pause() {
  if (state_ != PlayState::Playing)
    return;
  state_ = PlayState::Paused;
  Debug::Log("Game paused", Debug::LogLevel::INFO);
}

void Builder::Resume() {
  if (state_ != PlayState::Paused)
    return;
  state_ = PlayState::Playing;
  Debug::Log("Game resumed", Debug::LogLevel::INFO);
}

void Builder::Stop() {
  if (state_ == PlayState::Stopped)
    return;

  // Restore transforms from snapshot
  if (renderer_ && snapshot_.size() == renderer_->meshes3d.size()) {
    for (size_t i = 0; i < snapshot_.size(); i++) {
      renderer_->meshes3d[i].userPosition = snapshot_[i].position;
      renderer_->meshes3d[i].userRotation = snapshot_[i].rotation;
      renderer_->meshes3d[i].userScale = snapshot_[i].scale;
    }
  }
  snapshot_.clear();

  state_ = PlayState::Stopped;
  Debug::Log("Game stopped (scene restored)", Debug::LogLevel::SUCCESS);
}

} // namespace Ilmeee
