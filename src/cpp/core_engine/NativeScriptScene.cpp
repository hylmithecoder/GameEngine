#include "../../../include/core_engine/NativeScriptScene.hpp"
#include "../../../include/core_engine/MediaSystem.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
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
using script::ObjectKind;
using script::Prop;
uint32_t Kinds(const SceneRenderer::Mesh3D &mesh) {
  uint32_t kinds = 0;
  if (mesh.isLight)
    kinds |= static_cast<uint32_t>(ObjectKind::Light);
  if (mesh.isCamera)
    kinds |= static_cast<uint32_t>(ObjectKind::Camera);
  // A disabled component still counts, so scripts can enable it again.
  if (mesh.isAudioEmitter || mesh.audio.enabled || !mesh.audio.clip.empty())
    kinds |= static_cast<uint32_t>(ObjectKind::Audio);
  if (mesh.video.enabled || !mesh.video.clip.empty())
    kinds |= static_cast<uint32_t>(ObjectKind::Video);
  return kinds;
}
ObjectKind KindOf(Prop prop) {
  if (prop <= Prop::AudioPlaying)
    return ObjectKind::Audio;
  if (prop <= Prop::VideoPlaying)
    return ObjectKind::Video;
  if (prop <= Prop::LightType)
    return ObjectKind::Light;
  return ObjectKind::Camera;
}
// The mesh owning `id` when it carries the component `prop` belongs to.
SceneRenderer::Mesh3D *Owner(void *ctx, uint64_t id, Prop prop) {
  const int index = Find(ctx, id);
  if (index < 0)
    return nullptr;
  auto &mesh =
      static_cast<NativeScriptSceneContext *>(ctx)->renderer->meshes3d[index];
  return Kinds(mesh) & static_cast<uint32_t>(KindOf(prop)) ? &mesh : nullptr;
}
float *FloatField(SceneRenderer::Mesh3D &mesh, Prop prop) {
  switch (prop) {
  case Prop::AudioVolume:
    return &mesh.audio.volume;
  case Prop::AudioMinDistance:
    return &mesh.audio.minDistance;
  case Prop::AudioMaxDistance:
    return &mesh.audio.maxDistance;
  case Prop::VideoVolume:
    return &mesh.video.volume;
  case Prop::LightIntensity:
    return &mesh.lightIntensity;
  case Prop::LightRange:
    return &mesh.lightRange;
  case Prop::LightSpotAngle:
    return &mesh.lightSpotAngle;
  case Prop::CameraFov:
    return &mesh.camFov;
  case Prop::CameraOrthoSize:
    return &mesh.camOrthoSize;
  case Prop::CameraNear:
    return &mesh.camNear;
  case Prop::CameraFar:
    return &mesh.camFar;
  default:
    return nullptr;
  }
}
bool *BoolField(SceneRenderer::Mesh3D &mesh, Prop prop) {
  switch (prop) {
  case Prop::AudioEnabled:
    return &mesh.audio.enabled;
  case Prop::AudioLoop:
    return &mesh.audio.loop;
  case Prop::AudioPlayOnStart:
    return &mesh.audio.playOnStart;
  case Prop::AudioSpatial:
    return &mesh.audio.spatial;
  case Prop::VideoEnabled:
    return &mesh.video.enabled;
  case Prop::VideoLoop:
    return &mesh.video.loop;
  case Prop::VideoPlayOnStart:
    return &mesh.video.playOnStart;
  case Prop::VideoUnlit:
    return &mesh.video.unlit;
  default:
    return nullptr;
  }
}
std::string *StringField(SceneRenderer::Mesh3D &mesh, Prop prop) {
  if (prop == Prop::AudioClip)
    return &mesh.audio.clip;
  if (prop == Prop::VideoClip)
    return &mesh.video.clip;
  return nullptr;
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
  host.kinds = [](void *ctx, uint64_t id) -> uint32_t {
    const int index = Find(ctx, id);
    return index < 0 ? 0
                     : Kinds(static_cast<NativeScriptSceneContext *>(ctx)
                                 ->renderer->meshes3d[index]);
  };
  host.getFloat = [](void *ctx, uint64_t id, Prop prop, float *out) {
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh)
      return false;
    if (prop == Prop::LightType) {
      *out = static_cast<float>(mesh->lightType);
      return true;
    }
    const float *field = FloatField(*mesh, prop);
    if (field)
      *out = *field;
    return field != nullptr;
  };
  host.setFloat = [](void *ctx, uint64_t id, Prop prop, float value) {
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh || !std::isfinite(value))
      return false;
    if (prop == Prop::LightType) {
      if (value < 0 || value > 2)
        return false;
      mesh->lightType = static_cast<int>(value);
      return true;
    }
    float *field = FloatField(*mesh, prop);
    if (!field)
      return false;
    if (prop == Prop::CameraFov)
      value = std::clamp(value, 1.0f, 179.0f);
    else if (prop == Prop::CameraNear)
      value = std::max(value, 0.001f);
    else if (prop == Prop::LightSpotAngle)
      value = std::clamp(value, 0.0f, 179.0f);
    else
      value = std::max(value, 0.0f);
    *field = value;
    return true;
  };
  host.getBool = [](void *ctx, uint64_t id, Prop prop, bool *out) {
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh)
      return false;
    const auto *context = static_cast<NativeScriptSceneContext *>(ctx);
    const auto index =
        static_cast<size_t>(mesh - context->renderer->meshes3d.data());
    if (prop == Prop::AudioPlaying || prop == Prop::VideoPlaying) {
      *out = context->media && (prop == Prop::AudioPlaying
                                    ? context->media->IsAudioPlaying(
                                          *context->renderer, index)
                                    : context->media->IsVideoPlaying(
                                          *context->renderer, index));
      return true;
    }
    if (prop == Prop::CameraOrthographic) {
      *out = mesh->camProjection == 1;
      return true;
    }
    const bool *field = BoolField(*mesh, prop);
    if (field)
      *out = *field;
    return field != nullptr;
  };
  host.setBool = [](void *ctx, uint64_t id, Prop prop, bool value) {
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh)
      return false;
    if (prop == Prop::CameraOrthographic) {
      mesh->camProjection = value ? 1 : 0;
      return true;
    }
    bool *field = BoolField(*mesh, prop);
    if (field)
      *field = value;
    return field != nullptr;
  };
  host.getVec3 = [](void *ctx, uint64_t id, Prop prop, script::Vec3 *out) {
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh || prop != Prop::LightColor)
      return false;
    *out = ToScript(mesh->lightColor);
    return true;
  };
  host.setVec3 = [](void *ctx, uint64_t id, Prop prop,
                    const script::Vec3 *value) {
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh || prop != Prop::LightColor || !Finite(*value))
      return false;
    mesh->lightColor = glm::clamp(ToEngine(*value), 0.0f, 1.0f);
    return true;
  };
  host.getString = [](void *ctx, uint64_t id, Prop prop) -> const char * {
    auto *mesh = Owner(ctx, id, prop);
    const std::string *field = mesh ? StringField(*mesh, prop) : nullptr;
    return field ? field->c_str() : "";
  };
  host.setString = [](void *ctx, uint64_t id, Prop prop, const char *value) {
    auto *mesh = Owner(ctx, id, prop);
    std::string *field = mesh ? StringField(*mesh, prop) : nullptr;
    if (!field || !value)
      return false;
    *field = value;
    return true;
  };
  host.command = [](void *ctx, uint64_t id, script::Command command) {
    auto *context = static_cast<NativeScriptSceneContext *>(ctx);
    const bool audio = command == script::Command::AudioPlay ||
                       command == script::Command::AudioStop;
    auto *mesh = Owner(ctx, id, audio ? Prop::AudioEnabled : Prop::VideoEnabled);
    if (!mesh || !context->media)
      return false;
    const auto index =
        static_cast<size_t>(mesh - context->renderer->meshes3d.data());
    switch (command) {
    case script::Command::AudioPlay:
      return context->media->PlayAudio(*context->renderer, index);
    case script::Command::AudioStop:
      context->media->StopAudio(index);
      return true;
    case script::Command::VideoPlay:
      return context->media->PlayVideo(*context->renderer, index);
    case script::Command::VideoStop:
      context->media->StopVideo(index);
      return true;
    }
    return false;
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
