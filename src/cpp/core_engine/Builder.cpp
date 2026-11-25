#include "../../include/core_engine/Builder.hpp"
#include <unordered_map>

namespace Ilmeee {

void Builder::Init(SceneRenderer *renderer) { renderer_ = renderer; }

void Builder::Play() {
  if (!renderer_)
    return;

  // Snapshot current transforms so we can restore on Stop
  snapshot_.clear();
  for (const auto &mesh : renderer_->meshes3d) {
    MeshSnapshot snap;
    snap.objectId = mesh.objectId;
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
  // Objects spawned during Play have no snapshot (the editor removes them);
  // destroyed ones are brought back by ScriptSceneRestore.
  if (renderer_) {
    std::unordered_map<uint64_t, const MeshSnapshot *> byId;
    for (const auto &snap : snapshot_)
      byId[snap.objectId] = &snap;
    for (auto &mesh : renderer_->meshes3d) {
      const auto it = byId.find(mesh.objectId);
      if (it == byId.end())
        continue;
      const MeshSnapshot &snap = *it->second;
      mesh.userPosition = snap.position;
      mesh.userRotation = snap.rotation;
      mesh.userScale = snap.scale;
      mesh.worldText = snap.worldText;
      mesh.hasPhysics = snap.hasPhysics;
      mesh.useGravity = snap.useGravity;
      mesh.isKinematic = snap.isKinematic;
      mesh.mass = snap.mass;
      mesh.drag = snap.drag;
      mesh.gravityY = snap.gravityY;
      mesh.velocity = snap.velocity;
      mesh.collider = snap.collider;
    }
  }
  snapshot_.clear();

  state_ = PlayState::Stopped;
  Debug::Log("Game stopped (scene restored)", Debug::LogLevel::SUCCESS);
}

} // namespace Ilmeee
