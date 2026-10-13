#pragma once
#include "MovementScript.hpp"
#include "NativeScripts.hpp"
class SceneRenderer;
namespace ilmeee {
struct NativeScriptSceneContext {
  SceneRenderer *renderer = nullptr;
  const MovementInput *input = nullptr;
  std::function<void(const std::string &)> log;
};
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context);
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer);
} // namespace ilmeee
