#pragma once
#include "SkeletalAnimation.hpp"
#include <array>

namespace ilmeee {
enum class LocomotionState { Idle = 0, Walk = 1, Run = 2 };
const char *LocomotionName(LocomotionState state);
bool IsAnimationControllerPath(const std::string &path);
struct AnimationController {
  // Clip paths are relative to this controller asset's directory.
  std::string idle, walk, run;
  float walkSpeed = 2, runSpeed = 5;
  float idleThreshold = 0.05f, transition = 0.2f;
  bool inPlace = true;
  std::string rootBone = "センター";
};
bool ValidateController(const AnimationController &value, std::string &error);
bool LoadAnimationController(const std::string &path, AnimationController &out, std::string &error);
bool SaveAnimationController(const std::string &path, const AnimationController &value, std::string &error);
bool LoadControllerClips(const std::string &path, const AnimationController &value,
                         std::array<AnimationClip, 3> &out, std::string &error);

// CPU-only evaluation shared by editor preview and both game players.
class LocomotionPlayer {
public:
  void Reset();
  AnimationClip Update(const std::vector<SkeletonBone> &bones,
      const AnimationController &config, const std::array<AnimationClip, 3> &clips,
      float horizontalSpeed, float dt, float playbackSpeed = 1);
  LocomotionState State() const { return state_; }
  float Speed() const { return speed_; }
  float Blend() const { return blend_; }
private:
  LocomotionState state_ = LocomotionState::Idle;
  bool initialized_ = false;
  float phase_ = 0, speed_ = 0, blend_ = 1;
  std::vector<BoneTransform> pose_, from_;
};
struct ControllerRuntime {
  std::string loadedPath;
  AnimationController config;
  std::array<AnimationClip, 3> clips;
  LocomotionPlayer player;
  glm::vec3 previousPosition{0};
  bool hasPosition = false;
  float sampleTime = 0, sampleDistance = 0, measuredSpeed = 0;
};
} // namespace ilmeee
