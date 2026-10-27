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
    snap.worldText = mesh.worldText;
    snap.position = mesh.userPosition;
    snap.rotation = mesh.userRotation;
    snap.scale = mesh.userScale;
    snap.hasPhysics = mesh.hasPhysics;
    snap.useGravity = mesh.useGravity;
    snap.isKinematic = mesh.isKinematic;
    snap.mass = mesh.mass;
    snap.drag = mesh.drag;
    snap.gravityY = mesh.gravityY;
    snap.velocity = mesh.velocity;
    snap.collider = mesh.collider;
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
      renderer_->meshes3d[i].worldText = snapshot_[i].worldText;
      renderer_->meshes3d[i].hasPhysics = snapshot_[i].hasPhysics;
      renderer_->meshes3d[i].useGravity = snapshot_[i].useGravity;
      renderer_->meshes3d[i].isKinematic = snapshot_[i].isKinematic;
      renderer_->meshes3d[i].mass = snapshot_[i].mass;
      renderer_->meshes3d[i].drag = snapshot_[i].drag;
      renderer_->meshes3d[i].gravityY = snapshot_[i].gravityY;
      renderer_->meshes3d[i].velocity = snapshot_[i].velocity;
      renderer_->meshes3d[i].collider = snapshot_[i].collider;
    }
  }
  snapshot_.clear();

  state_ = PlayState::Stopped;
  Debug::Log("Game stopped (scene restored)", Debug::LogLevel::SUCCESS);
}

} // namespace Ilmeee
