#include "../../../include/core_engine/NativeScriptScene.hpp"
#include "../../../include/core_engine/DialogSystem.hpp"
#include "../../../include/core_engine/GameUI.hpp"
#include "../../../include/core_engine/MediaSystem.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

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
UiElement *FindUi(void *opaque, uint64_t id) {
  auto *ui = static_cast<NativeScriptSceneContext *>(opaque)->ui;
  return ui ? FindUiElement(*ui, id) : nullptr;
}
// IsKeyDown / IsKeyPressed names, as bits so the previous frame fits in a word.
uint32_t KeyBit(const char *key) {
  static const char *const names[] = {"W", "S", "A", "D", "Space", "E", "Enter"};
  for (uint32_t i = 0; i < std::size(names); ++i)
    if (std::strcmp(key, names[i]) == 0)
      return 1u << i;
  return std::strcmp(key, "Interact") == 0 ? 1u << 5 : 0u;
}
uint32_t KeyBits(const MovementInput *input) {
  if (!input)
    return 0;
  return (input->forward ? 1u : 0u) | (input->backward ? 2u : 0u) |
         (input->left ? 4u : 0u) | (input->right ? 8u : 0u) |
         (input->jump ? 16u : 0u) | (input->interact ? 32u : 0u) |
         (input->submit ? 64u : 0u);
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
  if (mesh.isWorldText) kinds |= static_cast<uint32_t>(ObjectKind::WorldText);
  if (mesh.isLight)
    kinds |= static_cast<uint32_t>(ObjectKind::Light);
  if (mesh.isCamera)
    kinds |= static_cast<uint32_t>(ObjectKind::Camera);
  // A disabled component still counts, so scripts can enable it again.
  if (mesh.isAudioEmitter || mesh.audio.enabled || !mesh.audio.clip.empty())
    kinds |= static_cast<uint32_t>(ObjectKind::Audio);
  if (mesh.video.enabled || !mesh.video.clip.empty())
    kinds |= static_cast<uint32_t>(ObjectKind::Video);
  if (mesh.dialog.enabled || !mesh.dialog.lines.empty() ||
      !mesh.dialog.file.empty())
    kinds |= static_cast<uint32_t>(ObjectKind::Dialog);
  return kinds;
}
ObjectKind KindOf(Prop prop) {
  if (prop <= Prop::AudioPlaying)
    return ObjectKind::Audio;
  if (prop <= Prop::VideoPlaying)
    return ObjectKind::Video;
  if (prop <= Prop::LightType)
    return ObjectKind::Light;
  if (prop <= Prop::CameraFar)
    return ObjectKind::Camera;
  if (prop <= Prop::UiClicked)
    return ObjectKind::Ui;
  if (prop <= Prop::DialogLine) return ObjectKind::Dialog;
  return ObjectKind::WorldText;
}
bool IsUi(Prop prop) { return KindOf(prop) == ObjectKind::Ui; }
// The Dialog line/speaker, only while `mesh` owns the open dialog.
const DialogLine *OpenLine(void *ctx, const SceneRenderer::Mesh3D &mesh) {
  const auto *dialog = static_cast<NativeScriptSceneContext *>(ctx)->dialog;
  return dialog && dialog->Owner() == mesh.objectId ? dialog->CurrentLine()
                                                    : nullptr;
}
glm::vec3 Rgb(const ImVec4 &color) { return {color.x, color.y, color.z}; }
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
  case Prop::DialogRange:
    return &mesh.dialog.range;
  case Prop::DialogSpeed:
    return &mesh.dialog.charsPerSecond;
  case Prop::TextSize:
    return &mesh.worldText.size;
  case Prop::TextAlpha:
    return &mesh.worldText.color.a;
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
  case Prop::DialogEnabled:
    return &mesh.dialog.enabled;
  case Prop::TextVisible:
    return &mesh.worldText.visible;
  default:
    return nullptr;
  }
}
std::string *StringField(SceneRenderer::Mesh3D &mesh, Prop prop) {
  if (prop == Prop::AudioClip)
    return &mesh.audio.clip;
  if (prop == Prop::VideoClip)
    return &mesh.video.clip;
  if (prop == Prop::DialogScript)
    return &mesh.dialog.lines;
  if (prop == Prop::TextContent)
    return &mesh.worldText.text;
  return nullptr;
}
std::string *UiStringField(UiElement &e, Prop prop) {
  switch (prop) {
  case Prop::UiText:
    return &e.text;
  case Prop::UiImage:
    return &e.imagePath;
  case Prop::UiAction:
    return &e.action;
  default:
    return nullptr;
  }
}
} // namespace
script::HostApi ScriptSceneHost(NativeScriptSceneContext &context) {
  script::HostApi host;
  host.context = &context;
  host.valid = [](void *ctx, uint64_t id) {
    return Find(ctx, id) >= 0 || FindUi(ctx, id) != nullptr;
  };
  host.getTransform = [](void *ctx, uint64_t id, script::Transform *out) {
    const int index = Find(ctx, id);
    if (index < 0) {
      // UI elements: position is x, y from the anchor in reference pixels.
      const UiElement *e = FindUi(ctx, id);
      if (e)
        *out = {{e->x, e->y, 0}, {}, {1, 1, 1}};
      return e != nullptr;
    }
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
    if (!Finite(value->position) || !Finite(value->rotation) ||
        !Finite(value->scale))
      return false;
    if (index < 0) {
      UiElement *e = FindUi(ctx, id);
      if (e) {
        e->x = value->position.x;
        e->y = value->position.y;
      }
      return e != nullptr;
    }
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
    if (const auto *ui = static_cast<NativeScriptSceneContext *>(ctx)->ui)
      for (const auto &element : ui->elements)
        if (element.id == name)
          return element.objectId;
    return 0;
  };
  host.name = [](void *ctx, uint64_t id) -> const char * {
    const int index = Find(ctx, id);
    const auto *renderer =
        static_cast<NativeScriptSceneContext *>(ctx)->renderer;
    if (index >= 0)
      return renderer->meshes3d[index].displayName.c_str();
    const UiElement *e = FindUi(ctx, id);
    return e ? e->id.c_str() : "";
  };
  host.keyDown = [](void *ctx, const char *key) {
    const auto *input = static_cast<NativeScriptSceneContext *>(ctx)->input;
    return key && (KeyBits(input) & KeyBit(key)) != 0;
  };
  host.keyPressed = [](void *ctx, const char *key) {
    const auto *context = static_cast<NativeScriptSceneContext *>(ctx);
    const uint32_t bit = key ? KeyBit(key) : 0u;
    return (KeyBits(context->input) & bit) && !(context->previousKeys & bit);
  };
  host.log = [](void *ctx, const char *message) {
    const auto &log = static_cast<NativeScriptSceneContext *>(ctx)->log;
    if (log)
      log(std::string("[Script] ") + message);
  };
  host.kinds = [](void *ctx, uint64_t id) -> uint32_t {
    const int index = Find(ctx, id);
    if (index < 0) {
      const auto *element = FindUi(ctx, id);
      if (!element) return 0u;
      uint32_t kinds = static_cast<uint32_t>(ObjectKind::Ui);
      if (element->kind == UiKind::Text) kinds |= static_cast<uint32_t>(ObjectKind::UiText);
      if (element->kind == UiKind::Image) kinds |= static_cast<uint32_t>(ObjectKind::UiImage);
      if (element->kind == UiKind::Button) kinds |= static_cast<uint32_t>(ObjectKind::UiButton);
      return kinds;
    }
    return Kinds(static_cast<NativeScriptSceneContext *>(ctx)
                                 ->renderer->meshes3d[index]);
  };
  host.getFloat = [](void *ctx, uint64_t id, Prop prop, float *out) {
    if (IsUi(prop)) {
      const UiElement *e = FindUi(ctx, id);
      if (!e)
        return false;
      if (prop == Prop::UiFontSize)
        *out = e->fontSize;
      else if (prop == Prop::UiAlpha)
        *out = e->color.w;
      else if (prop == Prop::UiBackgroundAlpha)
        *out = e->background.w;
      else
        return false;
      return true;
    }
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh)
      return false;
    if (prop == Prop::DialogLineIndex) {
      const auto *dialog = static_cast<NativeScriptSceneContext *>(ctx)->dialog;
      *out = dialog && dialog->Owner() == mesh->objectId
                 ? static_cast<float>(dialog->LineIndex())
                 : -1.0f;
      return true;
    }
    if (prop == Prop::LightType) {
      *out = static_cast<float>(mesh->lightType);
      return true;
    }
    if (prop == Prop::TextAlignment) {
      *out = static_cast<float>(mesh->worldText.alignment);
      return true;
    }
    const float *field = FloatField(*mesh, prop);
    if (field)
      *out = *field;
    return field != nullptr;
  };
  host.setFloat = [](void *ctx, uint64_t id, Prop prop, float value) {
    if (!std::isfinite(value))
      return false;
    if (IsUi(prop)) {
      UiElement *e = FindUi(ctx, id);
      if (!e)
        return false;
      if (prop == Prop::UiFontSize)
        e->fontSize = std::clamp(value, 1.0f, 512.0f);
      else if (prop == Prop::UiAlpha)
        e->color.w = std::clamp(value, 0.0f, 1.0f);
      else if (prop == Prop::UiBackgroundAlpha)
        e->background.w = std::clamp(value, 0.0f, 1.0f);
      else
        return false;
      return true;
    }
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh)
      return false;
    if (prop == Prop::LightType) {
      if (value < 0 || value > 2)
        return false;
      mesh->lightType = static_cast<int>(value);
      return true;
    }
    float *field = FloatField(*mesh, prop);
    if (prop == Prop::TextAlignment) {
      if (value < 0 || value > 2 || std::floor(value) != value) return false;
      mesh->worldText.alignment = static_cast<int>(value);
      return true;
    }
    if (!field)
      return false;
    if (prop == Prop::TextSize)
      value = std::clamp(value, 0.001f, 100.0f);
    else if (prop == Prop::TextAlpha)
      value = std::clamp(value, 0.0f, 1.0f);
    else if (prop == Prop::CameraFov)
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
    const auto *context = static_cast<NativeScriptSceneContext *>(ctx);
    if (IsUi(prop)) {
      const UiElement *e = FindUi(ctx, id);
      if (!e)
        return false;
      if (prop == Prop::UiVisible) {
        *out = e->visible;
      } else if (prop == Prop::UiClicked) {
        const auto *input = context->input;
        *out = input && std::find(input->uiPressed.begin(),
                                  input->uiPressed.end(),
                                  id) != input->uiPressed.end();
      } else {
        return false;
      }
      return true;
    }
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh)
      return false;
    if (prop == Prop::DialogTalking) {
      *out = context->dialog && context->dialog->Owner() == mesh->objectId;
      return true;
    }
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
    if (prop == Prop::UiVisible) {
      UiElement *e = FindUi(ctx, id);
      if (e)
        e->visible = value;
      return e != nullptr;
    }
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
    if (IsUi(prop)) {
      const UiElement *e = FindUi(ctx, id);
      if (!e)
        return false;
      if (prop == Prop::UiPosition)
        *out = {e->x, e->y, 0};
      else if (prop == Prop::UiSize)
        *out = {e->width, e->height, 0};
      else if (prop == Prop::UiColor)
        *out = ToScript(Rgb(e->color));
      else if (prop == Prop::UiBackground)
        *out = ToScript(Rgb(e->background));
      else
        return false;
      return true;
    }
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh || (prop != Prop::LightColor && prop != Prop::TextColor))
      return false;
    *out = ToScript(prop == Prop::TextColor ? glm::vec3(mesh->worldText.color) : mesh->lightColor);
    return true;
  };
  host.setVec3 = [](void *ctx, uint64_t id, Prop prop,
                    const script::Vec3 *value) {
    if (IsUi(prop)) {
      UiElement *e = FindUi(ctx, id);
      if (!e || !Finite(*value))
        return false;
      const glm::vec3 rgb = glm::clamp(ToEngine(*value), 0.0f, 1.0f);
      if (prop == Prop::UiPosition) {
        e->x = value->x;
        e->y = value->y;
      } else if (prop == Prop::UiSize) {
        e->width = std::clamp(value->x, 1.0f, 8192.0f);
        e->height = std::clamp(value->y, 1.0f, 8192.0f);
      } else if (prop == Prop::UiColor) {
        e->color = ImVec4(rgb.x, rgb.y, rgb.z, e->color.w);
      } else if (prop == Prop::UiBackground) {
        e->background = ImVec4(rgb.x, rgb.y, rgb.z, e->background.w);
      } else {
        return false;
      }
      return true;
    }
    auto *mesh = Owner(ctx, id, prop);
    if (!mesh || (prop != Prop::LightColor && prop != Prop::TextColor) || !Finite(*value))
      return false;
    const auto color = glm::clamp(ToEngine(*value), 0.0f, 1.0f);
    if (prop == Prop::TextColor) mesh->worldText.color = glm::vec4(color, mesh->worldText.color.a);
    else mesh->lightColor = color;
    return true;
  };
  host.getString = [](void *ctx, uint64_t id, Prop prop) -> const char * {
    if (IsUi(prop)) {
      UiElement *e = FindUi(ctx, id);
      const std::string *field = e ? UiStringField(*e, prop) : nullptr;
      return field ? field->c_str() : "";
    }
    auto *mesh = Owner(ctx, id, prop);
    if (mesh && (prop == Prop::DialogSpeaker || prop == Prop::DialogLine)) {
      const DialogLine *line = OpenLine(ctx, *mesh);
      return !line ? ""
             : prop == Prop::DialogSpeaker ? line->speaker.c_str()
                                           : line->text.c_str();
    }
    const std::string *field = mesh ? StringField(*mesh, prop) : nullptr;
    return field ? field->c_str() : "";
  };
  host.setString = [](void *ctx, uint64_t id, Prop prop, const char *value) {
    if (IsUi(prop)) {
      UiElement *e = FindUi(ctx, id);
      std::string *field = e ? UiStringField(*e, prop) : nullptr;
      if (!field || !value)
        return false;
      *field = value;
      return true;
    }
    auto *mesh = Owner(ctx, id, prop);
    std::string *field = mesh ? StringField(*mesh, prop) : nullptr;
    if (!field || !value)
      return false;
    *field = value;
    return true;
  };
  host.command = [](void *ctx, uint64_t id, script::Command command) {
    auto *context = static_cast<NativeScriptSceneContext *>(ctx);
    if (command >= script::Command::DialogStart) {
      auto *mesh = Owner(ctx, id, Prop::DialogEnabled);
      auto *dialog = context->dialog;
      if (!mesh || !dialog || !context->ui)
        return false;
      auto &renderer = *context->renderer;
      if (command == script::Command::DialogStart)
        return dialog->Begin(
            renderer, *context->ui,
            static_cast<size_t>(mesh - renderer.meshes3d.data()));
      if (dialog->Owner() != mesh->objectId)
        return false;
      if (command == script::Command::DialogNext)
        return dialog->Next(renderer, *context->ui);
      dialog->End(renderer, *context->ui);
      return true;
    }
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
    default:
      break;
    }
    return false;
  };
  return host;
}
void ScriptSceneEndFrame(NativeScriptSceneContext &context) {
  context.previousKeys = KeyBits(context.input);
}
std::vector<ScriptOwner> ScriptSceneOwners(SceneRenderer &renderer, GameUI *ui) {
  std::vector<ScriptOwner> owners;
  for (auto &mesh : renderer.meshes3d)
    owners.push_back({mesh.objectId, &mesh.scripts});
  if (ui) for (auto &element : ui->elements)
    owners.push_back({element.objectId, &element.scripts});
  return owners;
}
} // namespace ilmeee
