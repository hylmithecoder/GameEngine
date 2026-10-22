#pragma once
// Spring bones — cheap secondary motion for hair, breasts, skirts, ribbons
// and tails (VRM SpringBone / MMD-style jiggle), simulated per bone tail
// with Verlet integration on a fixed timestep.
//
// Runs between EvaluateSkeletonPose (animated frames) and BuildSkinMatrices:
// each chain bone is rotated so its tail follows a point that lags behind,
// springs back to the animated pose, and is pushed out of body colliders.
//
// Units are scale-free so one preset fits MMD (≈20 units tall) and metre
// rigs alike: stiffness is 1/s, gravity and wind are bone lengths per second,
// collider sizes are model units (scaled with the object).
#include "SkeletalAnimation.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace ilmeee {

struct SpringBoneChain {
  std::string rootBone; // the root and all its descendants swing
  float stiffness = 8.0f; // pull back to the animated pose, 1/s
  float drag = 0.4f;      // velocity lost per step, 0..1
  float gravity = 1.0f;   // bone lengths per second, downwards
  float hitRadius = 0.0f; // tail thickness against colliders, model units
  bool useColliders = true;
};

// Sphere (tailOffset == offset) or capsule, attached to a bone. Offsets are
// model-space vectors from the bone head in bind pose; they turn with the
// bone.
struct SpringCollider {
  std::string bone;
  glm::vec3 offset{0.0f};
  glm::vec3 tailOffset{0.0f};
  float radius = 0.5f;
};

struct SpringBoneComponent {
  bool enabled = false;
  glm::vec3 wind{0.0f}; // bone lengths per second, model-independent
  std::vector<SpringBoneChain> chains;
  std::vector<SpringCollider> colliders;
};

// Simulation state; owned by the runtime mesh, never serialised.
struct SpringBoneRuntime {
  struct Joint {
    int bone = -1;
    int chain = -1;
    glm::vec3 tailLocal{0.0f}; // tail offset in the bone frame
    glm::vec3 previous{0.0f}, current{0.0f}; // world-space tail
  };
  std::string signature; // rebuild joints when skeleton/chains change
  std::vector<int> order; // parents before children
  std::vector<Joint> joints;
  std::vector<int> jointOfBone;
  float accumulator = 0.0f;
  glm::mat4 previousModel{1.0f};
  bool initialized = false;
  void Reset() { *this = SpringBoneRuntime(); }
};

constexpr float kSpringBoneStep = 1.0f / 60.0f;

// Advances the simulation by `deltaSeconds` (fixed substeps, at most 4 per
// call) and rewrites `world` (model-space frames from EvaluateSkeletonPose)
// so chain bones follow their tails. `modelToWorld` is the object's full
// model matrix; moving the object makes the chains lag. deltaSeconds == 0
// re-applies the last state to a new pose without advancing time.
// Returns false (and leaves `world` untouched) if nothing is simulated.
bool ApplySpringBones(const std::vector<SkeletonBone> &bones,
                      const SpringBoneComponent &component,
                      SpringBoneRuntime &runtime, const glm::mat4 &modelToWorld,
                      float deltaSeconds, std::vector<glm::mat4> &world);

// Number of bones a component would simulate on this skeleton.
size_t CountSpringJoints(const std::vector<SkeletonBone> &bones,
                         const SpringBoneComponent &component);

// Builds chains and body colliders from common PMX (Japanese) and English
// bone names: hair, breasts, skirt, ribbons/ties, tails. A rig without any
// match gets an empty, disabled component.
SpringBoneComponent AutoSetupSpringBones(const std::vector<SkeletonBone> &bones);

// The auto-setup category of a bone by name: "hair", "breast", "skirt",
// "accessory", or "" for anything else.
const char *SpringBoneKind(const SkeletonBone &bone);

} // namespace ilmeee
