#pragma once
// Typed views over GameObject for objects carrying an engine component.
// Each keeps the GameObject transform API; IsValid() is also false when the
// object lacks the component. Get one with gameObject.As<AudioGameObject>()
// or expose ObjectRef<AudioGameObject> as an Inspector field.
#include "GameObject.hpp"

namespace ilmeee::script {
class AudioGameObject : public GameObject {
public:
  AudioGameObject() = default;
  explicit AudioGameObject(const GameObject &object) : GameObject(object) {}
  bool IsValid() const {
    return GameObject::IsValid() && Is(ObjectKind::Audio);
  }
  // Play restarts the clip from the beginning. Play mode only.
  bool Play() const { return Run(Command::AudioPlay); }
  bool Stop() const { return Run(Command::AudioStop); }
  bool IsPlaying() const { return GetBool(Prop::AudioPlaying); }
  bool IsEnabled() const { return GetBool(Prop::AudioEnabled); }
  bool SetEnabled(bool value) const {
    return SetBool(Prop::AudioEnabled, value);
  }
  // Takes effect on the next Play().
  std::string GetClip() const { return GetString(Prop::AudioClip); }
  bool SetClip(const std::string &path) const {
    return SetString(Prop::AudioClip, path);
  }
  float GetVolume() const { return GetFloat(Prop::AudioVolume); }
  bool SetVolume(float value) const {
    return SetFloat(Prop::AudioVolume, value);
  }
  // Takes effect on the next Play().
  bool GetLoop() const { return GetBool(Prop::AudioLoop); }
  bool SetLoop(bool value) const { return SetBool(Prop::AudioLoop, value); }
  bool GetSpatial() const { return GetBool(Prop::AudioSpatial); }
  bool SetSpatial(bool value) const {
    return SetBool(Prop::AudioSpatial, value);
  }
  float GetMinDistance() const { return GetFloat(Prop::AudioMinDistance); }
  bool SetMinDistance(float value) const {
    return SetFloat(Prop::AudioMinDistance, value);
  }
  float GetMaxDistance() const { return GetFloat(Prop::AudioMaxDistance); }
  bool SetMaxDistance(float value) const {
    return SetFloat(Prop::AudioMaxDistance, value);
  }
};

class VideoPlayerGameObject : public GameObject {
public:
  VideoPlayerGameObject() = default;
  explicit VideoPlayerGameObject(const GameObject &object)
      : GameObject(object) {}
  bool IsValid() const {
    return GameObject::IsValid() && Is(ObjectKind::Video);
  }
  // Play restarts the video from the beginning; Stop blanks the screen.
  // Play mode only.
  bool Play() const { return Run(Command::VideoPlay); }
  bool Stop() const { return Run(Command::VideoStop); }
  bool IsPlaying() const { return GetBool(Prop::VideoPlaying); }
  bool IsEnabled() const { return GetBool(Prop::VideoEnabled); }
  bool SetEnabled(bool value) const {
    return SetBool(Prop::VideoEnabled, value);
  }
  // Changing clip or loop while playing reloads the video.
  std::string GetClip() const { return GetString(Prop::VideoClip); }
  bool SetClip(const std::string &path) const {
    return SetString(Prop::VideoClip, path);
  }
  bool GetLoop() const { return GetBool(Prop::VideoLoop); }
  bool SetLoop(bool value) const { return SetBool(Prop::VideoLoop, value); }
  float GetVolume() const { return GetFloat(Prop::VideoVolume); }
  bool SetVolume(float value) const {
    return SetFloat(Prop::VideoVolume, value);
  }
  bool GetUnlit() const { return GetBool(Prop::VideoUnlit); }
  bool SetUnlit(bool value) const { return SetBool(Prop::VideoUnlit, value); }
};

class LightGameObject : public GameObject {
public:
  enum class Type { Directional = 0, Point = 1, Spot = 2 };
  LightGameObject() = default;
  explicit LightGameObject(const GameObject &object) : GameObject(object) {}
  bool IsValid() const {
    return GameObject::IsValid() && Is(ObjectKind::Light);
  }
  Vec3 GetColor() const { return GetVec3(Prop::LightColor); }
  bool SetColor(Vec3 rgb) const { return SetVec3(Prop::LightColor, rgb); }
  float GetIntensity() const { return GetFloat(Prop::LightIntensity); }
  bool SetIntensity(float value) const {
    return SetFloat(Prop::LightIntensity, value);
  }
  float GetRange() const { return GetFloat(Prop::LightRange); }
  bool SetRange(float value) const { return SetFloat(Prop::LightRange, value); }
  float GetSpotAngle() const { return GetFloat(Prop::LightSpotAngle); }
  bool SetSpotAngle(float degrees) const {
    return SetFloat(Prop::LightSpotAngle, degrees);
  }
  Type GetType() const {
    return static_cast<Type>(static_cast<int>(GetFloat(Prop::LightType)));
  }
  bool SetType(Type type) const {
    return SetFloat(Prop::LightType, static_cast<float>(type));
  }
};

class CameraGameObject : public GameObject {
public:
  CameraGameObject() = default;
  explicit CameraGameObject(const GameObject &object) : GameObject(object) {}
  bool IsValid() const {
    return GameObject::IsValid() && Is(ObjectKind::Camera);
  }
  bool IsOrthographic() const { return GetBool(Prop::CameraOrthographic); }
  bool SetOrthographic(bool value) const {
    return SetBool(Prop::CameraOrthographic, value);
  }
  float GetFov() const { return GetFloat(Prop::CameraFov); }
  bool SetFov(float degrees) const {
    return SetFloat(Prop::CameraFov, degrees);
  }
  float GetOrthoSize() const { return GetFloat(Prop::CameraOrthoSize); }
  bool SetOrthoSize(float value) const {
    return SetFloat(Prop::CameraOrthoSize, value);
  }
  float GetNear() const { return GetFloat(Prop::CameraNear); }
  bool SetNear(float value) const { return SetFloat(Prop::CameraNear, value); }
  float GetFar() const { return GetFloat(Prop::CameraFar); }
  bool SetFar(float value) const { return SetFloat(Prop::CameraFar, value); }
};
} // namespace ilmeee::script

// Short names for project scripts only; engine code has its own GameObject.
#ifdef ILMEEE_SCRIPT_BUILD
using Vec3 = ilmeee::script::Vec3;
using Transform = ilmeee::script::Transform;
using GameObject = ilmeee::script::GameObject;
using AudioGameObject = ilmeee::script::AudioGameObject;
using VideoPlayerGameObject = ilmeee::script::VideoPlayerGameObject;
using LightGameObject = ilmeee::script::LightGameObject;
using CameraGameObject = ilmeee::script::CameraGameObject;
template <class T> using ObjectRef = ilmeee::script::ObjectRef<T>;
#endif
