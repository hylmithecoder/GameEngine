#pragma once
#include "MovementScript.hpp"
#include "NativeScripts.hpp"
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
};
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context);
// Dispatches OnTriggerEnter/Stay/Exit and OnCollisionEnter/Stay/Exit from
// the latest PhysicsSystem contacts to active scripts.
void ScriptSceneDispatchPhysics(NativeScriptSceneContext &context,
                                NativeScripts &scripts);
// Call once per frame after the scripts' Update so IsKeyPressed sees edges.
void ScriptSceneEndFrame(NativeScriptSceneContext &context);
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer, GameUI *ui = nullptr);
} // namespace ilmeee
