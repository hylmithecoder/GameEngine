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
// An element of the game Canvas (Text, Image, Button, Joystick). Its
// GetPosition/SetPosition/Translate move it in reference pixels from its
// anchor (z is ignored). Changes last until Stop, like transforms.
class UiGameObject : public GameObject {
public:
  UiGameObject() = default;
  explicit UiGameObject(const GameObject &object) : GameObject(object) {}
  bool IsValid() const { return GameObject::IsValid() && Is(ObjectKind::Ui); }
  std::string GetText() const { return GetString(Prop::UiText); }
  bool SetText(const std::string &text) const {
    return SetString(Prop::UiText, text);
  }
  // Image elements: path relative to the project, e.g. assets/ui/face.png.
  std::string GetImage() const { return GetString(Prop::UiImage); }
  bool SetImage(const std::string &path) const {
    return SetString(Prop::UiImage, path);
  }
  std::string GetAction() const { return GetString(Prop::UiAction); }
  bool SetAction(const std::string &action) const {
    return SetString(Prop::UiAction, action);
  }
  bool IsVisible() const { return GetBool(Prop::UiVisible); }
  bool SetVisible(bool value) const { return SetBool(Prop::UiVisible, value); }
  bool Show() const { return SetVisible(true); }
  bool Hide() const { return SetVisible(false); }
  // x = width, y = height (reference pixels, >= 1).
  Vec3 GetSize() const { return GetVec3(Prop::UiSize); }
  bool SetSize(Vec3 size) const { return SetVec3(Prop::UiSize, size); }
  float GetFontSize() const { return GetFloat(Prop::UiFontSize); }
  bool SetFontSize(float value) const {
    return SetFloat(Prop::UiFontSize, value);
  }
  // Text / knob colour and its alpha, RGB 0..1.
  Vec3 GetColor() const { return GetVec3(Prop::UiColor); }
  bool SetColor(Vec3 rgb) const { return SetVec3(Prop::UiColor, rgb); }
  float GetAlpha() const { return GetFloat(Prop::UiAlpha); }
  bool SetAlpha(float value) const { return SetFloat(Prop::UiAlpha, value); }
  Vec3 GetBackground() const { return GetVec3(Prop::UiBackground); }
  bool SetBackground(Vec3 rgb) const {
    return SetVec3(Prop::UiBackground, rgb);
  }
  float GetBackgroundAlpha() const {
    return GetFloat(Prop::UiBackgroundAlpha);
  }
  bool SetBackgroundAlpha(float value) const {
    return SetFloat(Prop::UiBackgroundAlpha, value);
  }
  // Clicked or tapped since the previous Update. Any visible element counts,
  // not only buttons.
  bool WasClicked() const { return GetBool(Prop::UiClicked); }
};

// An object with the engine's Dialog component (a character you talk to).
// The component shows its dialog box, types the lines into the text element
// and advances on E / Enter / a tap on the box; these let a script drive it.
class DialogGameObject : public GameObject {
public:
  DialogGameObject() = default;
  explicit DialogGameObject(const GameObject &object) : GameObject(object) {}
  bool IsValid() const {
    return GameObject::IsValid() && Is(ObjectKind::Dialog);
  }
  // Opens the dialog from its first line. False when another dialog is open,
  // the script has no lines, or the component is disabled. Play mode only.
  bool StartDialog() const { return Run(Command::DialogStart); }
  // Finishes the typing line, otherwise goes to the next line (or closes).
  bool Next() const { return Run(Command::DialogNext); }
  bool StopDialog() const { return Run(Command::DialogStop); }
  bool IsTalking() const { return GetBool(Prop::DialogTalking); }
  // 0-based line shown now, -1 when the dialog is closed.
  int GetLineIndex() const {
    return static_cast<int>(GetFloat(Prop::DialogLineIndex));
  }
  std::string GetSpeaker() const { return GetString(Prop::DialogSpeaker); }
  std::string GetLine() const { return GetString(Prop::DialogLine); }
  // Dialog text in the Inspector format ("Name: line", one per line). Used
  // from the next StartDialog(); a Dialog File set in the Inspector wins.
  std::string GetScript() const { return GetString(Prop::DialogScript); }
  bool SetScript(const std::string &text) const {
    return SetString(Prop::DialogScript, text);
  }
  bool IsEnabled() const { return GetBool(Prop::DialogEnabled); }
  bool SetEnabled(bool value) const {
    return SetBool(Prop::DialogEnabled, value);
  }
  float GetRange() const { return GetFloat(Prop::DialogRange); }
  bool SetRange(float value) const { return SetFloat(Prop::DialogRange, value); }
  float GetSpeed() const { return GetFloat(Prop::DialogSpeed); }
  bool SetSpeed(float charsPerSecond) const {
    return SetFloat(Prop::DialogSpeed, charsPerSecond);
  }
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
using UiGameObject = ilmeee::script::UiGameObject;
using DialogGameObject = ilmeee::script::DialogGameObject;
template <class T> using ObjectRef = ilmeee::script::ObjectRef<T>;
#endif
