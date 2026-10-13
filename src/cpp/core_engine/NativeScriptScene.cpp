#include "../../../include/core_engine/NativeScriptScene.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <cmath>
#include <cstring>

namespace ilmeee {
namespace {
int Find(void *opaque, uint64_t id) {
  const auto *renderer =
      static_cast<NativeScriptSceneContext *>(opaque)->renderer;
  if (!renderer || !id)
    return -1;
  for (size_t i = 0; i < renderer->meshes3d.size(); ++i)
    if (renderer->meshes3d[i].objectId == id)
      return static_cast<int>(i);
  return -1;
}
script::Vec3 ToScript(glm::vec3 value) { return {value.x, value.y, value.z}; }
glm::vec3 ToEngine(script::Vec3 value) { return {value.x, value.y, value.z}; }
bool Finite(script::Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
} // namespace
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context) {
  script::HostApi host;
  host.context = &context;
  host.valid = [](void *ctx, uint64_t id) { return Find(ctx, id) >= 0; };
  host.getTransform = [](void *ctx, uint64_t id, script::Transform *out) {
    const int index = Find(ctx, id);
    if (index < 0)
      return false;
    const auto *renderer =
        static_cast<NativeScriptSceneContext *>(ctx)->renderer;
    *out = {ToScript(renderer->GetMesh3DPosition(index)),
            ToScript(renderer->GetMesh3DRotation(index)),
            ToScript(renderer->GetMesh3DScale(index))};
    return true;
  };
  host.setTransform = [](void *ctx, uint64_t id,
                         const script::Transform *value) {
    const int index = Find(ctx, id);
    if (index < 0 || !Finite(value->position) || !Finite(value->rotation) ||
        !Finite(value->scale))
      return false;
    auto *renderer = static_cast<NativeScriptSceneContext *>(ctx)->renderer;
    renderer->SetMesh3DTransform(index, ToEngine(value->position),
                                 ToEngine(value->rotation),
                                 ToEngine(value->scale));
    return true;
  };
  host.find = [](void *ctx, const char *name) -> uint64_t {
    const auto *renderer =
        static_cast<NativeScriptSceneContext *>(ctx)->renderer;
    if (renderer)
      for (const auto &mesh : renderer->meshes3d)
        if (mesh.displayName == name)
          return mesh.objectId;
    return 0;
  };
  host.name = [](void *ctx, uint64_t id) -> const char * {
    const int index = Find(ctx, id);
    const auto *renderer =
        static_cast<NativeScriptSceneContext *>(ctx)->renderer;
    return index < 0 ? "" : renderer->meshes3d[index].displayName.c_str();
  };
  host.keyDown = [](void *ctx, const char *key) {
    const auto *input = static_cast<NativeScriptSceneContext *>(ctx)->input;
    if (!input)
      return false;
    if (std::strcmp(key, "W") == 0)
      return input->forward;
    if (std::strcmp(key, "S") == 0)
      return input->backward;
    if (std::strcmp(key, "A") == 0)
      return input->left;
    if (std::strcmp(key, "D") == 0)
      return input->right;
    if (std::strcmp(key, "Space") == 0)
      return input->jump;
    return false;
  };
  host.log = [](void *ctx, const char *message) {
    const auto &log = static_cast<NativeScriptSceneContext *>(ctx)->log;
    if (log)
      log(std::string("[Script] ") + message);
  };
  return host;
}
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer) {
  std::vector<ScriptOwner> owners;
  for (auto &mesh : renderer.meshes3d)
    owners.push_back({mesh.objectId, &mesh.scripts});
  return owners;
}
} // namespace ilmeee
