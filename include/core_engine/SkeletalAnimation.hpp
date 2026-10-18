#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <array>

namespace ilmeee {

struct AnimationComponent {
  bool enabled = false;
  std::string clip;
  float speed = 1.0f;
  bool loop = true;
  std::string controller; // optional locomotion asset; takes precedence over clip
};

struct BoneTransform {
  glm::vec3 translation{0};
  glm::quat rotation{1, 0, 0, 0};
  glm::vec3 scale{1};
};
struct BoneKeyframe {
  float time = 0;
  BoneTransform value;
  // Incoming VMD Bezier curves: X, Y, Z translation, then rotation.
  std::array<glm::vec4, 4> curves{{{0,0,1,1}, {0,0,1,1}, {0,0,1,1}, {0,0,1,1}}};
};
struct BoneTrack {
  std::string bone;
  std::vector<BoneKeyframe> keys;
};
struct AnimationClip {
  std::string name = "New Animation";
  float duration = 3.0f;
  float fps = 30.0f;
  std::vector<BoneTrack> tracks;
};
struct SkeletonBone {
  std::string name;
  int parent = -1;
  glm::vec3 bindPosition{0}; // model-space, right-handed
};

bool ValidateAnimation(const AnimationClip &clip, std::string &error);
bool LoadAnimationClip(const std::string &path, AnimationClip &out,
                       std::string &error);
bool LoadVmdAnimation(const std::string &path, AnimationClip &out,
                      std::string &error);
bool IsAnimationPath(const std::string &path);
bool SaveAnimationClip(const std::string &path, const AnimationClip &clip,
                       std::string &error);
BoneTransform SampleBone(const BoneTrack &track, float time);
void SetBoneKey(AnimationClip &clip, const std::string &bone,
                const BoneKeyframe &key);
bool DeleteBoneKey(AnimationClip &clip, const std::string &bone, float time);
float AnimationTime(float time, float duration, bool loop);
// Handles parents stored after their children; rejects cycles and bad links.
bool EvaluateSkeleton(const std::vector<SkeletonBone> &bones,
                      const AnimationClip &clip, float time,
                      std::vector<glm::mat4> &skinMatrices,
                      std::vector<glm::vec3> &positions);
glm::vec3 SkinPosition(const glm::vec3 &position, const glm::ivec4 &joints,
                       const glm::vec4 &weights,
                       const std::vector<glm::mat4> &matrices);
glm::vec3 SkinNormal(const glm::vec3 &normal, const glm::ivec4 &joints,
                     const glm::vec4 &weights,
                     const std::vector<glm::mat4> &matrices);
// A small editable loop, using standard PMX/Japanese and common English names.
AnimationClip MakeBreathingClip(const std::vector<SkeletonBone> &bones,
                                float modelHeight);
} // namespace ilmeee
