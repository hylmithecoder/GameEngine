#pragma once
#include "MovementScript.hpp"
#include "NativeScripts.hpp"
class SceneRenderer;
namespace ilmeee {
class MediaSystem;
class DialogSystem;
struct GameUI;
struct NativeScriptSceneContext {
  SceneRenderer *renderer = nullptr;
  const MovementInput *input = nullptr;
  MediaSystem *media = nullptr; // Audio/Video Play and Stop
  std::function<void(const std::string &)> log;
  GameUI *ui = nullptr;            // Canvas elements (UiGameObject)
  DialogSystem *dialog = nullptr;  // DialogGameObject commands
  uint32_t previousKeys = 0;       // for IsKeyPressed; see ScriptSceneEndFrame
};
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context);
// Call once per frame after the scripts' Update so IsKeyPressed sees edges.
void ScriptSceneEndFrame(NativeScriptSceneContext &context);
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer);
} // namespace ilmeee
