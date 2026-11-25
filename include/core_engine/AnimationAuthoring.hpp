#pragma once
// Animation authoring helpers for tools (MCP) and editors: humanoid bone
// roles across PMX / Mixamo / VRM / 3ds Max Biped names, bone grouping,
// keyframe edit operations and small procedural clips (idle, wave, nod ...).
//
// Pose space: a character faces +Z, +X is the character's left, +Y is up.
// Rest orientation of every bone is identity, so a key's rotation is relative
// to the parent's frame with these same axes at rest. Euler angles are
// degrees [x, y, z] as in glm::quat(radians(euler)).

#include "SkeletalAnimation.hpp"
#include "../../vendor/nlohmann/json.hpp"
#include <map>
#include <string>
#include <vector>

namespace ilmeee {

// Role ("head", "arm_l", ...) → bone index, for the roles this rig has.
std::map<std::string, int> HumanoidRoles(const std::vector<SkeletonBone> &bones);
// Every role name this module knows, in body order.
const std::vector<std::string> &HumanoidRoleNames();

// "body", "face", "arm_l", "hand_r", "leg_l", "ik", "hair", "breast",
// "skirt", "accessory", "helper" or "other".
std::string BoneGroup(const std::vector<SkeletonBone> &bones, int index);

// 左↔右, Left↔Right, L_↔R_, _L↔_R, " L "↔" R ". False if no side marker.
bool MirrorBoneName(const std::string &name, std::string &mirrored);

// Resolves "@role" through HumanoidRoles, otherwise an exact bone name
// (primary or alt). Empty skeleton = accept any plain name.
bool ResolveBone(const std::vector<SkeletonBone> &bones, const std::string &ref,
                 std::string &name, std::string &error);

glm::vec3 QuatToEulerDegrees(const glm::quat &q);
glm::quat EulerDegreesToQuat(const glm::vec3 &degrees);

// Applies edit operations (see docs/ScriptingAPI.md, MCP edit_animation):
// set_key, delete_key, clear_track, add_rotation, mirror, scale_time, shift,
// set_duration. Atomic batch: clip and log are unchanged on failure.
bool ApplyAnimationEdits(AnimationClip &clip, const nlohmann::json &ops,
                         const std::vector<SkeletonBone> &bones,
                         std::vector<std::string> &log, std::string &error);

// Procedural clips built on humanoid roles: "idle", "breathe", "wave",
// "nod", "shake_head", "sway", "bow", "talk", "happy_bounce",
// "look_around". intensity scales every amplitude (1 = default).
const std::vector<std::string> &GeneratedAnimationTypes();
bool GenerateAnimation(const std::string &type, const std::vector<SkeletonBone> &bones,
                       float duration, float intensity, AnimationClip &out,
                       std::string &error);

} // namespace ilmeee
