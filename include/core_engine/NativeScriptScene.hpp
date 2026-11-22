#pragma once
#include "IlmeeeScene.hpp"
#include "MovementScript.hpp"
#include "NativeScripts.hpp"
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
class SceneRenderer;
namespace ilmeee {
class MediaSystem;
class DialogSystem;
class PhysicsSystem;
struct GameUI;
struct NativeScriptSceneContext {
  SceneRenderer *renderer = nullptr;
  const MovementInput *input = nullptr;
  MediaSystem *media = nullptr; // Audio/Video Play and Stop
  std::function<void(const std::string &)> log;
  GameUI *ui = nullptr;            // Canvas elements (UiGameObject)
  DialogSystem *dialog = nullptr;  // DialogGameObject commands
  PhysicsSystem *physics = nullptr; // PhysicsGameObject + trigger/collision events
  uint32_t previousKeys = 0;       // for IsKeyPressed; see ScriptSceneEndFrame

  // --- Instantiate / Destroy (API 8) ---
  NativeScripts *scripts = nullptr;    // runs the scripts of spawned copies
  MovementSystem *movement = nullptr;  // follows renumbered meshes
  std::filesystem::path projectRoot;   // prefab paths are relative to this
  // Editor: snapshot the scene at ScriptSceneBeginPlay so
  // ScriptSceneRestore can undo spawns and destroys on Stop.
  bool recordForRestore = false;
  // After meshes were removed (old -> new index, -1 removed), e.g. to fix
  // the editor's selection.
  std::function<void(const std::vector<int> &)> onMeshesRemoved;

  // Runtime state, reset by ScriptSceneBeginPlay.
  std::vector<uint64_t> pendingDestroy; // removed by ScriptSceneEndFrame
  std::unordered_map<std::string, std::shared_ptr<const IlmeeeScene>> prefabs;
  std::unordered_set<std::string> failedPrefabs; // warned once per Play
  std::vector<uint64_t> playOrder;               // objectIds at Play start
  IlmeeeScene playSnapshot;                      // recordForRestore only
  size_t spawnedCount = 0;                       // Instantiate calls that worked
};
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context);
// Call when Play starts, before NativeScripts::Start: clears the runtime
// state above and (recordForRestore) snapshots the scene.
void ScriptSceneBeginPlay(NativeScriptSceneContext &context);
// Dispatches OnTriggerEnter/Stay/Exit and OnCollisionEnter/Stay/Exit from
// the latest PhysicsSystem contacts to active scripts.
void ScriptSceneDispatchPhysics(NativeScriptSceneContext &context,
                                NativeScripts &scripts);
// Call once per frame after the scripts' Update so IsKeyPressed sees edges.
// Also removes the objects scripts destroyed this frame.
void ScriptSceneEndFrame(NativeScriptSceneContext &context);
// Removes objects destroyed during this frame now (EndFrame calls this).
void ScriptSceneFlushDestroyed(NativeScriptSceneContext &context);
// Editor, after Play stopped (scripts already stopped): removes everything
// spawned during Play, brings back destroyed scene objects, and restores
// the original object order. Returns true if the scene structure changed.
bool ScriptSceneRestore(NativeScriptSceneContext &context);
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer, GameUI *ui = nullptr);
} // namespace ilmeee
