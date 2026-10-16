#pragma once
#include <cstdint>
#include <string>

namespace ilmeee::script {
inline constexpr uint32_t kApiVersion = 5;
struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3 operator+(Vec3 other) const {
    return {x + other.x, y + other.y, z + other.z};
  }
  Vec3 operator-(Vec3 other) const {
    return {x - other.x, y - other.y, z - other.z};
  }
  Vec3 operator*(float value) const {
    return {x * value, y * value, z * value};
  }
};
struct Transform {
  Vec3 position, rotation, scale{1, 1, 1};
};
// What an object carries; one object can be several (a video screen with
// an audio source). Bit values are part of the script ABI.
enum class ObjectKind : uint32_t {
  Light = 1u << 0,
  Camera = 1u << 1,
  Audio = 1u << 2,
  Video = 1u << 3,
  Ui = 1u << 4,     // element of the game Canvas (API 3)
  Dialog = 1u << 5, // Dialog component (API 3)
  WorldText = 1u << 6, // world-space Text (API 4)
  UiText = 1u << 7,
  UiImage = 1u << 8,
  UiButton = 1u << 9,
};
// Component data reached through HostApi::get*/set*. Append only: the
// values are part of the script ABI.
enum class Prop : uint32_t {
  AudioEnabled,     // bool
  AudioClip,        // string, project-relative path
  AudioVolume,      // float
  AudioLoop,        // bool
  AudioPlayOnStart, // bool
  AudioSpatial,     // bool
  AudioMinDistance, // float
  AudioMaxDistance, // float
  AudioPlaying,     // bool, read-only
  VideoEnabled,     // bool
  VideoClip,        // string, project-relative path
  VideoVolume,      // float
  VideoLoop,        // bool
  VideoPlayOnStart, // bool
  VideoUnlit,       // bool
  VideoPlaying,     // bool, read-only
  LightColor,       // Vec3, 0..1
  LightIntensity,   // float
  LightRange,       // float
  LightSpotAngle,   // float, degrees
  LightType,        // float: 0 directional, 1 point, 2 spot
  CameraOrthographic, // bool
  CameraFov,          // float, degrees
  CameraOrthoSize,    // float
  CameraNear,         // float
  CameraFar,          // float
  // API 3: Canvas UI elements.
  UiText,            // string
  UiImage,           // string, project-relative path
  UiAction,          // string, button action
  UiVisible,         // bool
  UiPosition,        // Vec3: x, y in reference pixels from the anchor
  UiSize,            // Vec3: width, height in reference pixels
  UiFontSize,        // float
  UiColor,           // Vec3, 0..1
  UiAlpha,           // float, 0..1
  UiBackground,      // Vec3, 0..1
  UiBackgroundAlpha, // float, 0..1
  UiClicked,         // bool, read-only: pressed since the last Update
  // API 3: Dialog component.
  DialogEnabled,   // bool
  DialogScript,    // string, inline dialog text
  DialogTalking,   // bool, read-only
  DialogLineIndex, // float, read-only: current line, -1 when not talking
  DialogRange,     // float
  DialogSpeed,     // float, characters per second (0 = instant)
  DialogSpeaker,   // string, read-only: speaker of the current line
  DialogLine,      // string, read-only: full text of the current line
  // API 4: world-space Text, independent of Canvas properties.
  TextContent,    // string
  TextSize,       // float, line height in world units
  TextColor,      // Vec3
  TextAlpha,      // float
  TextVisible,    // bool
  TextAlignment,  // float: 0 left, 1 center, 2 right
};
enum class Command : uint32_t {
  AudioPlay,
  AudioStop,
  VideoPlay,
  VideoStop,
  DialogStart, // API 3
  DialogNext,
  DialogStop,
};
// Only the editor/player calls these callbacks, on its scene thread.
struct HostApi {
  void *context = nullptr;
  bool (*valid)(void *, uint64_t) = nullptr;
  bool (*getTransform)(void *, uint64_t, Transform *) = nullptr;
  bool (*setTransform)(void *, uint64_t, const Transform *) = nullptr;
  uint64_t (*find)(void *, const char *) = nullptr;
  const char *(*name)(void *, uint64_t) = nullptr;
  bool (*keyDown)(void *, const char *) = nullptr;
  void (*log)(void *, const char *) = nullptr;
  // API 2: typed components.
  uint32_t (*kinds)(void *, uint64_t) = nullptr;
  bool (*getFloat)(void *, uint64_t, Prop, float *) = nullptr;
  bool (*setFloat)(void *, uint64_t, Prop, float) = nullptr;
  bool (*getBool)(void *, uint64_t, Prop, bool *) = nullptr;
  bool (*setBool)(void *, uint64_t, Prop, bool) = nullptr;
  bool (*getVec3)(void *, uint64_t, Prop, Vec3 *) = nullptr;
  bool (*setVec3)(void *, uint64_t, Prop, const Vec3 *) = nullptr;
  // The returned text stays valid until the property changes.
  const char *(*getString)(void *, uint64_t, Prop) = nullptr;
  bool (*setString)(void *, uint64_t, Prop, const char *) = nullptr;
  bool (*command)(void *, uint64_t, Command) = nullptr;
  // API 3: true only on the frame the key went down.
  bool (*keyPressed)(void *, const char *) = nullptr;
};
class GameObject {
public:
  GameObject() = default;
  GameObject(uint64_t id, const HostApi *host) : id_(id), host_(host) {}
  uint64_t Id() const { return id_; }
  bool IsValid() const {
    return host_ && host_->valid && host_->valid(host_->context, id_);
  }
  std::string Name() const {
    if (!IsValid() || !host_->name)
      return {};
    const char *value = host_->name(host_->context, id_);
    return value ? value : "";
  }
  Transform GetTransform() const {
    Transform value;
    if (host_ && host_->getTransform)
      host_->getTransform(host_->context, id_, &value);
    return value;
  }
  bool SetTransform(const Transform &value) const {
    return host_ && host_->setTransform &&
           host_->setTransform(host_->context, id_, &value);
  }
  Vec3 GetPosition() const { return GetTransform().position; }
  bool SetPosition(Vec3 position) const {
    auto value = GetTransform();
    value.position = position;
    return SetTransform(value);
  }
  bool Translate(Vec3 delta) const {
    return SetPosition(GetPosition() + delta);
  }
  GameObject Find(const std::string &name) const {
    return {host_ && host_->find ? host_->find(host_->context, name.c_str())
                                 : 0,
            host_};
  }
  bool Is(ObjectKind kind) const {
    return host_ && host_->kinds &&
           (host_->kinds(host_->context, id_) & static_cast<uint32_t>(kind));
  }
  // Typed view, e.g. target.Get().As<AudioGameObject>(). Check IsValid():
  // it is false when the object lacks that component.
  template <class T> T As() const { return T(*this); }

protected:
  float GetFloat(Prop prop) const {
    float value = 0;
    if (host_ && host_->getFloat)
      host_->getFloat(host_->context, id_, prop, &value);
    return value;
  }
  bool SetFloat(Prop prop, float value) const {
    return host_ && host_->setFloat &&
           host_->setFloat(host_->context, id_, prop, value);
  }
  bool GetBool(Prop prop) const {
    bool value = false;
    if (host_ && host_->getBool)
      host_->getBool(host_->context, id_, prop, &value);
    return value;
  }
  bool SetBool(Prop prop, bool value) const {
    return host_ && host_->setBool &&
           host_->setBool(host_->context, id_, prop, value);
  }
  Vec3 GetVec3(Prop prop) const {
    Vec3 value;
    if (host_ && host_->getVec3)
      host_->getVec3(host_->context, id_, prop, &value);
    return value;
  }
  bool SetVec3(Prop prop, Vec3 value) const {
    return host_ && host_->setVec3 &&
           host_->setVec3(host_->context, id_, prop, &value);
  }
  std::string GetString(Prop prop) const {
    const char *value =
        host_ && host_->getString ? host_->getString(host_->context, id_, prop)
                                  : nullptr;
    return value ? value : "";
  }
  bool SetString(Prop prop, const std::string &value) const {
    return host_ && host_->setString &&
           host_->setString(host_->context, id_, prop, value.c_str());
  }
  bool Run(Command command) const {
    return host_ && host_->command &&
           host_->command(host_->context, id_, command);
  }

private:
  uint64_t id_ = 0;
  const HostApi *host_ = nullptr;
};
class GameObjectRef {
public:
  uint64_t id = 0; // serialized identity, never a mesh index or raw pointer
  GameObject Get() const { return {id, host_}; }
  bool IsValid() const { return Get().IsValid(); }
  void Bind(const HostApi *host) { host_ = host; }

private:
  const HostApi *host_ = nullptr;
};
// Inspector field that resolves to a typed object: ObjectRef<AudioGameObject>.
template <class T> class ObjectRef : public GameObjectRef {
public:
  T Get() const { return GameObjectRef::Get().template As<T>(); }
  bool IsValid() const { return Get().IsValid(); }
};
} // namespace ilmeee::script
using GameObjectRef = ilmeee::script::GameObjectRef;
