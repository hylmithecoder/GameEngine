#pragma once
#include "MovementScript.hpp"
#include "NativeScripts.hpp"
class SceneRenderer;
namespace ilmeee {
class MediaSystem;
struct NativeScriptSceneContext {
  SceneRenderer *renderer = nullptr;
  const MovementInput *input = nullptr;
  MediaSystem *media = nullptr; // Audio/Video Play and Stop
  std::function<void(const std::string &)> log;
};
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context);
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer);
} // namespace ilmeee
