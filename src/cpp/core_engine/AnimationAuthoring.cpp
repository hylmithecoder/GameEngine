#include "core_engine/AnimationAuthoring.hpp"
#include "core_engine/SpringBone.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <glm/gtc/quaternion.hpp>
#include <initializer_list>

namespace ilmeee {
namespace {
using json = nlohmann::json;

std::string Lower(std::string s) {
  for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool Has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }
bool HasAny(const std::string &s, std::initializer_list<const char *> parts) {
  for (const auto *p : parts) if (Has(s, p)) return true;
  return false;
}

struct RoleNames { const char *role; std::initializer_list<const char *> names; };
// Exact names, primary (PMX Japanese) first, then Mixamo, VRM, Biped, Unity.
const RoleNames kRoles[] = {
    {"center", {"センター", "Center", "center", "Hips", "mixamorig:Hips", "J_Bip_C_Hips", "Bip001", "Bip001 Pelvis"}},
    {"groove", {"グルーブ", "Groove"}},
    {"lower_body", {"下半身", "LowerBody", "Pelvis", "Bip001 Pelvis"}},
    {"upper_body", {"上半身", "UpperBody", "Spine", "mixamorig:Spine", "J_Bip_C_Spine", "Bip001 Spine"}},
    {"upper_body2", {"上半身2", "UpperBody2", "Chest", "Spine1", "mixamorig:Spine1", "J_Bip_C_Chest", "Bip001 Spine1"}},
    {"neck", {"首", "Neck", "neck", "mixamorig:Neck", "J_Bip_C_Neck", "Bip001 Neck"}},
    {"head", {"頭", "Head", "head", "mixamorig:Head", "J_Bip_C_Head", "Bip001 Head"}},
    {"eyes", {"両目", "Eyes"}},
    {"eye_l", {"左目", "LeftEye", "J_Adj_L_FaceEye", "Bdy_L_Eye"}},
    {"eye_r", {"右目", "RightEye", "J_Adj_R_FaceEye", "Bdy_R_Eye"}},
    {"shoulder_l", {"左肩", "LeftShoulder", "mixamorig:LeftShoulder", "J_Bip_L_Shoulder", "Bip001 L Clavicle"}},
    {"arm_l", {"左腕", "LeftArm", "mixamorig:LeftArm", "J_Bip_L_UpperArm", "Bip001 L UpperArm"}},
    {"elbow_l", {"左ひじ", "LeftForeArm", "mixamorig:LeftForeArm", "J_Bip_L_LowerArm", "Bip001 L Forearm"}},
    {"wrist_l", {"左手首", "LeftHand", "mixamorig:LeftHand", "J_Bip_L_Hand", "Bip001 L Hand"}},
    {"shoulder_r", {"右肩", "RightShoulder", "mixamorig:RightShoulder", "J_Bip_R_Shoulder", "Bip001 R Clavicle"}},
    {"arm_r", {"右腕", "RightArm", "mixamorig:RightArm", "J_Bip_R_UpperArm", "Bip001 R UpperArm"}},
    {"elbow_r", {"右ひじ", "RightForeArm", "mixamorig:RightForeArm", "J_Bip_R_LowerArm", "Bip001 R Forearm"}},
    {"wrist_r", {"右手首", "RightHand", "mixamorig:RightHand", "J_Bip_R_Hand", "Bip001 R Hand"}},
    {"leg_l", {"左足", "LeftUpLeg", "mixamorig:LeftUpLeg", "LeftUpperLeg", "J_Bip_L_UpperLeg", "Bip001 L Thigh"}},
    {"knee_l", {"左ひざ", "LeftLeg", "mixamorig:LeftLeg", "LeftLowerLeg", "J_Bip_L_LowerLeg", "Bip001 L Calf"}},
    {"ankle_l", {"左足首", "LeftFoot", "mixamorig:LeftFoot", "J_Bip_L_Foot", "Bip001 L Foot"}},
    {"leg_r", {"右足", "RightUpLeg", "mixamorig:RightUpLeg", "RightUpperLeg", "J_Bip_R_UpperLeg", "Bip001 R Thigh"}},
    {"knee_r", {"右ひざ", "RightLeg", "mixamorig:RightLeg", "RightLowerLeg", "J_Bip_R_LowerLeg", "Bip001 R Calf"}},
    {"ankle_r", {"右足首", "RightFoot", "mixamorig:RightFoot", "J_Bip_R_Foot", "Bip001 R Foot"}},
    {"leg_ik_l", {"左足ＩＫ", "左足IK", "LeftFootIK"}},
    {"leg_ik_r", {"右足ＩＫ", "右足IK", "RightFootIK"}},
};

int Side(const std::string &name) { // -1 right, 1 left, 0 none
  const std::string low = Lower(name);
  if (Has(name, "左") || low.rfind("left", 0) == 0 || Has(low, "_l_") ||
      low.rfind("l_", 0) == 0 || Has(low, " l ") || (low.size() > 2 && low.compare(low.size() - 2, 2, "_l") == 0))
    return 1;
  if (Has(name, "右") || low.rfind("right", 0) == 0 || Has(low, "_r_") ||
      low.rfind("r_", 0) == 0 || Has(low, " r ") || (low.size() > 2 && low.compare(low.size() - 2, 2, "_r") == 0))
    return -1;
  if (Has(low, "left")) return 1;
  if (Has(low, "right")) return -1;
  return 0;
}

std::string GroupOf(const std::string &name) {
  const std::string low = Lower(name);
  if (HasAny(name, {"ＩＫ", "IK"}) || Has(low, "ik")) return "ik";
  if (HasAny(name, {"捩", "補助", "錘", "锤", "ダミー", "親", "キャンセル", "調整", "先", "+"}) ||
      HasAny(low, {"twist", "dummy", "helper", "cancel", "_end", "nub"}) ||
      (!name.empty() && (name.back() == 'D' || name.back() == 'P' || name.back() == 'C') &&
       Side(name) && name.size() > 3 && static_cast<unsigned char>(name[0]) >= 0x80))
    return "helper";
  const int side = Side(name);
  const char *suffix = side > 0 ? "_l" : side < 0 ? "_r" : "";
  if (HasAny(name, {"目", "口", "眉", "舌", "歯", "まぶた"}) ||
      HasAny(low, {"eye", "mouth", "brow", "tongue", "jaw", "teeth"}))
    return "face";
  if (side && (HasAny(name, {"指"}) || HasAny(low, {"finger", "thumb", "index", "middle", "ring", "pinky", "little"})))
    return std::string("hand") + suffix;
  if (side && (HasAny(name, {"肩", "腕", "ひじ", "手"}) ||
               HasAny(low, {"shoulder", "clavicle", "arm", "elbow", "hand", "wrist"})))
    return std::string("arm") + suffix;
  if (side && (HasAny(name, {"足", "ひざ", "膝", "つま先", "尻"}) ||
               HasAny(low, {"leg", "thigh", "calf", "knee", "foot", "toe", "ankle"})))
    return std::string("leg") + suffix;
  if (HasAny(name, {"センター", "グルーブ", "腰", "下半身", "上半身", "首", "頭", "全ての親"}) ||
      HasAny(low, {"center", "hips", "pelvis", "spine", "chest", "neck", "head", "root"}))
    return "body";
  return "other";
}

std::array<glm::vec4, 4> Curves(const std::string &ease) {
  glm::vec4 c(0.4f, 0.0f, 0.6f, 1.0f); // smooth
  if (ease == "linear") c = {0.0f, 0.0f, 1.0f, 1.0f};
  else if (ease == "ease_in") c = {0.4f, 0.0f, 1.0f, 1.0f};
  else if (ease == "ease_out") c = {0.0f, 0.0f, 0.6f, 1.0f};
  return {c, c, c, c};
}

bool ReadVec3(const json &op, const char *key, glm::vec3 &out) {
  const auto it = op.find(key);
  if (it == op.end()) return false;
  if (!it->is_array() || it->size() != 3) throw std::runtime_error(std::string(key) + " must be [x, y, z]");
  for (int i = 0; i < 3; ++i) {
    if (!(*it)[i].is_number()) throw std::runtime_error(std::string(key) + " must be numbers");
    out[i] = (*it)[i].get<float>();
    if (!std::isfinite(out[i]) || std::abs(out[i]) > 100000) throw std::runtime_error(std::string(key) + " out of range");
  }
  return true;
}
float ReadFloat(const json &op, const char *key, float fallback, bool required = false) {
  const auto it = op.find(key);
  if (it == op.end()) {
    if (required) throw std::runtime_error(std::string("missing ") + key);
    return fallback;
  }
  if (!it->is_number() || !std::isfinite(it->get<double>()))
    throw std::runtime_error(std::string(key) + " must be a number");
  const float value = it->get<float>();
  if (!std::isfinite(value)) throw std::runtime_error(std::string(key) + " out of range");
  return value;
}
std::string ReadString(const json &op, const char *key, bool required = true) {
  const auto it = op.find(key);
  if (it == op.end()) {
    if (required) throw std::runtime_error(std::string("missing ") + key);
    return {};
  }
  if (!it->is_string()) throw std::runtime_error(std::string(key) + " must be a string");
  return it->get<std::string>();
}
std::string Fmt(float v) { char b[32]; std::snprintf(b, sizeof(b), "%.3g", v); return b; }
} // namespace

const std::vector<std::string> &HumanoidRoleNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> n;
    for (const auto &r : kRoles) n.push_back(r.role);
    return n;
  }();
  return names;
}

std::map<std::string, int> HumanoidRoles(const std::vector<SkeletonBone> &bones) {
  std::map<std::string, int> roles;
  for (const auto &r : kRoles)
    for (const auto *name : r.names) {
      int found = -1;
      for (size_t i = 0; i < bones.size() && found < 0; ++i)
        if (bones[i].name == name || bones[i].altName == name) found = int(i);
      if (found >= 0) { roles[r.role] = found; break; }
    }
  return roles;
}

std::string BoneGroup(const std::vector<SkeletonBone> &bones, int index) {
  if (index < 0 || index >= int(bones.size())) return "other";
  const auto &bone = bones[index];
  if (const char *kind = SpringBoneKind(bone); *kind) return kind;
  std::string group = GroupOf(bone.name);
  if ((group == "other" || group == "helper") && !bone.altName.empty()) {
    const std::string alt = GroupOf(bone.altName);
    if (alt != "other" && group == "other") group = alt;
  }
  return group;
}

bool MirrorBoneName(const std::string &name, std::string &out) {
  static const std::pair<const char *, const char *> swaps[] = {
      {"左", "右"}, {"Left", "Right"}, {"left", "right"}, {"LEFT", "RIGHT"}};
  for (const auto &[a, b] : swaps) {
    for (int dir = 0; dir < 2; ++dir) {
      const std::string from = dir ? b : a, to = dir ? a : b;
      if (const auto at = name.find(from); at != std::string::npos) {
        out = name; out.replace(at, from.size(), to); return true;
      }
    }
  }
  // Single-letter markers: L_x / x_L_x / x_L / "x L x".
  for (const auto &[from, to] : std::initializer_list<std::pair<char, char>>{{'L', 'R'}, {'R', 'L'}, {'l', 'r'}, {'r', 'l'}}) {
    const std::string text(1, from);
    if (name.rfind(text + "_", 0) == 0) { out = name; out[0] = to; return true; }
    for (const char sep : {'_', ' ', '.'}) {
      const std::string mid = std::string(1, sep) + from + sep;
      if (const auto at = name.find(mid); at != std::string::npos) { out = name; out[at + 1] = to; return true; }
      const std::string tail = std::string(1, sep) + from;
      if (name.size() > 2 && name.compare(name.size() - 2, 2, tail) == 0) { out = name; out.back() = to; return true; }
    }
  }
  return false;
}

bool ResolveBone(const std::vector<SkeletonBone> &bones, const std::string &ref,
                 std::string &name, std::string &error) {
  if (ref.empty()) { error = "empty bone name"; return false; }
  if (ref[0] == '@') {
    const auto roles = HumanoidRoles(bones);
    const auto it = roles.find(ref.substr(1));
    if (it == roles.end()) { error = "this rig has no role " + ref; return false; }
    name = bones[it->second].name; return true;
  }
  if (bones.empty()) { name = ref; return true; }
  for (const auto &b : bones)
    if (b.name == ref || b.altName == ref) { name = b.name; return true; }
  error = "unknown bone " + ref;
  return false;
}

glm::vec3 QuatToEulerDegrees(const glm::quat &q) { return glm::degrees(glm::eulerAngles(glm::normalize(q))); }
glm::quat EulerDegreesToQuat(const glm::vec3 &d) { return glm::normalize(glm::quat(glm::radians(d))); }

static bool ApplyAnimationEditsInPlace(AnimationClip &clip, const json &ops,
                         const std::vector<SkeletonBone> &bones,
                         std::vector<std::string> &log, std::string &error) {
  if (!ops.is_array()) { error = "ops must be an array"; return false; }
  if (ops.size() > 2000) { error = "at most 2000 ops per call"; return false; }
  size_t index = 0;
  try {
    for (const auto &op : ops) {
      ++index;
      if (!op.is_object()) throw std::runtime_error("op must be an object");
      const std::string kind = ReadString(op, "op");
      const auto bone = [&](bool required = true) {
        const std::string ref = ReadString(op, "bone", required);
        if (ref.empty()) {
          if (required) throw std::runtime_error("empty bone name");
          return std::string();
        }
        std::string name, why;
        if (!ResolveBone(bones, ref, name, why)) throw std::runtime_error(why);
        return name;
      };
      if (kind == "set_key") {
        const std::string name = bone();
        const float time = ReadFloat(op, "time", 0, true);
        if (time < 0 || time > 600) throw std::runtime_error("time must be 0..600 s");
        BoneKeyframe key;
        key.time = time;
        const auto track = std::find_if(clip.tracks.begin(), clip.tracks.end(),
                                        [&](const auto &t) { return t.bone == name; });
        if (track != clip.tracks.end()) key.value = SampleBone(*track, time);
        glm::vec3 v;
        if (ReadVec3(op, "rotation", v)) key.value.rotation = EulerDegreesToQuat(v);
        if (ReadVec3(op, "translation", v)) key.value.translation = v;
        if (ReadVec3(op, "scale", v)) key.value.scale = v;
        const auto ease = ReadString(op, "ease", false);
        if (!ease.empty() && ease != "linear" && ease != "smooth" &&
            ease != "ease_in" && ease != "ease_out") throw std::runtime_error("unknown ease " + ease);
        key.curves = Curves(ease);
        SetBoneKey(clip, name, key);
        if (time > clip.duration) clip.duration = time;
        log.push_back("set " + name + " @" + Fmt(time));
      } else if (kind == "delete_key") {
        const std::string name = bone();
        const float time = ReadFloat(op, "time", 0, true);
        if (time < 0 || time > 3600) throw std::runtime_error("time must be 0..3600 s");
        if (!DeleteBoneKey(clip, name, time)) throw std::runtime_error("no key on " + name + " at " + Fmt(time));
        log.push_back("deleted " + name + " @" + Fmt(time));
      } else if (kind == "clear_track") {
        const std::string name = bone();
        const auto before = clip.tracks.size();
        clip.tracks.erase(std::remove_if(clip.tracks.begin(), clip.tracks.end(),
                                         [&](const auto &t) { return t.bone == name; }), clip.tracks.end());
        log.push_back(before != clip.tracks.size() ? "cleared " + name : "no track " + name);
      } else if (kind == "add_rotation") {
        const std::string name = bone();
        glm::vec3 degrees(0);
        if (!ReadVec3(op, "rotation", degrees)) throw std::runtime_error("missing rotation");
        const float from = ReadFloat(op, "from", 0), to = ReadFloat(op, "to", 1e9f);
        if (from < 0 || to < from) throw std::runtime_error("invalid time range");
        const glm::quat offset = EulerDegreesToQuat(degrees);
        int changed = 0;
        for (auto &t : clip.tracks)
          if (t.bone == name)
            for (auto &k : t.keys)
              if (k.time >= from - 1e-4f && k.time <= to + 1e-4f) {
                k.value.rotation = glm::normalize(offset * k.value.rotation); ++changed;
              }
        if (!changed) throw std::runtime_error("no keys on " + name + " in range");
        log.push_back("rotated " + std::to_string(changed) + " keys of " + name);
      } else if (kind == "mirror") {
        const std::string from = ReadString(op, "from", false);
        const std::string only = bone(false);
        const int wanted = from == "right" ? -1 : 1;
        if (!from.empty() && from != "left" && from != "right") throw std::runtime_error("from must be left or right");
        std::vector<BoneTrack> copies;
        for (const auto &t : clip.tracks) {
          if (!only.empty() ? t.bone != only : Side(t.bone) != wanted) continue;
          std::string target;
          if (!MirrorBoneName(t.bone, target)) continue;
          if (!bones.empty()) {
            std::string resolved, why;
            if (!ResolveBone(bones, target, resolved, why)) continue;
            target = resolved;
          }
          BoneTrack copy{target, t.keys};
          // Reflect across the YZ plane (character's left ↔ right).
          for (auto &k : copy.keys) {
            k.value.translation.x = -k.value.translation.x;
            k.value.rotation = glm::quat(k.value.rotation.w, k.value.rotation.x,
                                         -k.value.rotation.y, -k.value.rotation.z);
          }
          copies.push_back(std::move(copy));
        }
        if (copies.empty()) throw std::runtime_error("nothing to mirror");
        for (auto &copy : copies) {
          clip.tracks.erase(std::remove_if(clip.tracks.begin(), clip.tracks.end(),
                                           [&](const auto &t) { return t.bone == copy.bone; }), clip.tracks.end());
          log.push_back("mirrored onto " + copy.bone);
          clip.tracks.push_back(std::move(copy));
        }
      } else if (kind == "scale_time") {
        const float factor = ReadFloat(op, "factor", 1, true);
        if (!(factor >= 0.01f && factor <= 100)) throw std::runtime_error("factor must be 0.01..100");
        for (auto &t : clip.tracks) for (auto &k : t.keys) k.time *= factor;
        clip.duration *= factor;
        log.push_back("scaled time x" + Fmt(factor));
      } else if (kind == "shift") {
        const float by = ReadFloat(op, "by", 0, true);
        const std::string only = bone(false);
        for (auto &t : clip.tracks) {
          if (!only.empty() && t.bone != only) continue;
          // Negative shifts can collapse several keys onto zero. Keep the
          // last key at each resulting time, as the VMD importer does.
          std::vector<BoneKeyframe> shifted;
          for (auto k : t.keys) {
            k.time = std::max(0.0f, k.time + by);
            if (!shifted.empty() && shifted.back().time == k.time) shifted.back() = k;
            else shifted.push_back(k);
          }
          t.keys = std::move(shifted);
        }
        float end = 0;
        for (const auto &t : clip.tracks) if (!t.keys.empty()) end = std::max(end, t.keys.back().time);
        clip.duration = std::max(clip.duration, end);
        log.push_back("shifted " + (only.empty() ? std::string("all") : only) + " by " + Fmt(by));
      } else if (kind == "set_duration") {
        const float duration = ReadFloat(op, "duration", clip.duration, true);
        if (!(duration > 0 && duration <= 600)) throw std::runtime_error("duration must be 0..600 s");
        clip.duration = duration;
        clip.fps = ReadFloat(op, "fps", clip.fps);
        log.push_back("duration " + Fmt(duration));
      } else {
        throw std::runtime_error("unknown op " + kind);
      }
    }
  } catch (const std::exception &e) {
    error = "op " + std::to_string(index) + ": " + e.what();
    return false;
  }
  return ValidateAnimation(clip, error);
}

bool ApplyAnimationEdits(AnimationClip &clip, const json &ops,
                         const std::vector<SkeletonBone> &bones,
                         std::vector<std::string> &log, std::string &error) {
  AnimationClip edited = clip;
  std::vector<std::string> pending;
  if (!ApplyAnimationEditsInPlace(edited, ops, bones, pending, error)) return false;
  clip = std::move(edited);
  log.insert(log.end(), pending.begin(), pending.end());
  return true;
}

// ---------------------------------------------------------------------------
// Procedural clips

const std::vector<std::string> &GeneratedAnimationTypes() {
  static const std::vector<std::string> types = {
      "idle", "breathe", "wave", "nod", "shake_head", "sway",
      "bow", "talk", "happy_bounce", "look_around"};
  return types;
}

namespace {
struct Builder {
  const std::vector<SkeletonBone> &bones;
  std::map<std::string, int> roles;
  AnimationClip &clip;
  float height = 1, k = 1; // model height, intensity
  bool Has(const char *role) const { return roles.count(role) != 0; }
  // Euler key (degrees, scaled by intensity) + optional translation in model heights.
  void Key(const char *role, float t, glm::vec3 deg, glm::vec3 move = glm::vec3(0)) {
    const auto it = roles.find(role);
    if (it == roles.end()) return;
    BoneKeyframe key;
    key.time = t;
    key.value.rotation = EulerDegreesToQuat(deg * k);
    key.value.translation = move * height * k;
    key.curves = Curves("smooth");
    SetBoneKey(clip, bones[it->second].name, key);
  }
  // Absolute rotation, not scaled by intensity (pose corrections).
  void Pose(const char *role, float t, glm::vec3 deg) {
    const auto it = roles.find(role);
    if (it == roles.end()) return;
    BoneKeyframe key;
    key.time = t;
    key.value.rotation = EulerDegreesToQuat(deg);
    key.curves = Curves("smooth");
    SetBoneKey(clip, bones[it->second].name, key);
  }
  // Degrees about Z that bring an upper arm from its bind angle to `target`
  // below horizontal (A/T-pose independent). side: 1 left, -1 right.
  float ArmDown(int side, float target) const {
    const auto arm = roles.find(side > 0 ? "arm_l" : "arm_r");
    const auto elbow = roles.find(side > 0 ? "elbow_l" : "elbow_r");
    if (arm == roles.end() || elbow == roles.end()) return 0;
    const glm::vec3 d = bones[elbow->second].bindPosition - bones[arm->second].bindPosition;
    const float bind = glm::degrees(std::atan2(d.y, std::abs(d.x))); // negative = below
    return side * (-target - bind);
  }
  // Relaxed arms held for the whole clip, plus an optional per-time offset.
  void RelaxedArms(float duration) {
    for (int side : {1, -1}) {
      const char *arm = side > 0 ? "arm_l" : "arm_r";
      Pose(arm, 0, {0, 0, ArmDown(side, 65)});
      Pose(arm, duration, {0, 0, ArmDown(side, 65)});
    }
  }
};
} // namespace

bool GenerateAnimation(const std::string &type, const std::vector<SkeletonBone> &bones,
                       float duration, float intensity, AnimationClip &target,
                       std::string &error) {
  if (std::find(GeneratedAnimationTypes().begin(), GeneratedAnimationTypes().end(), type) ==
      GeneratedAnimationTypes().end()) {
    error = "unknown type " + type; return false;
  }
  if (!std::isfinite(duration) || duration < 0 || duration > 60 ||
      (duration > 0 && duration < 0.2f) || !std::isfinite(intensity) || intensity < 0 || intensity > 3) {
    error = "duration must be 0 (default) or 0.2..60 s; intensity must be 0..3"; return false;
  }
  AnimationClip out;
  Builder b{bones, HumanoidRoles(bones), out};
  if (!b.Has("upper_body") && !b.Has("head")) { error = "rig has no upper_body/head bones"; return false; }
  float lo = 0, hi = 0;
  for (size_t i = 0; i < bones.size(); ++i) {
    lo = i ? std::min(lo, bones[i].bindPosition.y) : bones[i].bindPosition.y;
    hi = i ? std::max(hi, bones[i].bindPosition.y) : bones[i].bindPosition.y;
  }
  b.height = hi - lo > 1e-4f ? hi - lo : 1.0f;
  b.k = intensity;
  const std::map<std::string, float> defaults = {
      {"idle", 4}, {"breathe", 4}, {"wave", 2.4f}, {"nod", 1.6f}, {"shake_head", 1.6f},
      {"sway", 4}, {"bow", 2.5f}, {"talk", 3}, {"happy_bounce", 1.6f}, {"look_around", 5}};
  const float D = duration > 0 ? duration : defaults.at(type);
  out.name = type;
  out.duration = D;
  out.fps = 30;
  const auto at = [&](float f) { return f * D; }; // fraction of the clip
  const glm::vec3 o(0);

  if (type == "breathe" || type == "idle") {
    for (float f : {0.0f, 0.5f, 1.0f}) {
      const float in = f == 0.5f ? 1.0f : 0.0f;
      b.Key("upper_body", at(f), {-1.2f * in, 0, 0});
      b.Key("upper_body2", at(f), {-1.0f * in, 0, 0});
      b.Key("head", at(f), {0.8f * in, 0, 0});
      b.Key("shoulder_l", at(f), {0, 0, 1.5f * in});
      b.Key("shoulder_r", at(f), {0, 0, -1.5f * in});
    }
    if (type == "idle") {
      // Slow weight shift and a glance aside.
      for (float f : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float s = std::sin(f * 2 * glm::pi<float>());
        b.Key("center", at(f), o, {0.006f * s, 0, 0});
        b.Key("lower_body", at(f), {0, 0, -2.0f * s});
        b.Key("neck", at(f), {0, 6.0f * std::sin(f * 4 * glm::pi<float>()) * 0.5f, 1.5f * s});
      }
      b.RelaxedArms(D);
    }
  } else if (type == "wave") {
    // Right arm up, forearm upright, hand waving side to side.
    const float rest = b.ArmDown(-1, 65);
    const float raise = rest + (b.ArmDown(-1, -25) - rest) * b.k;
    b.Pose("arm_r", 0, {0, 0, rest});
    b.Pose("arm_l", 0, {0, 0, b.ArmDown(1, 65)});
    b.Pose("arm_l", D, {0, 0, b.ArmDown(1, 65)});
    b.Pose("arm_r", at(0.18f), {0, 0, raise});
    b.Pose("arm_r", at(0.82f), {0, 0, raise});
    b.Pose("arm_r", D, {0, 0, b.ArmDown(-1, 65)});
    b.Pose("elbow_r", 0, o);
    b.Pose("elbow_r", at(0.18f), {0, 0, -80 * b.k});
    for (int i = 0; i <= 4; ++i) {
      const float f = 0.22f + i * 0.13f;
      b.Pose("elbow_r", at(f), {0, 0, (-80 + (i % 2 ? 22.0f : -22.0f)) * b.k});
    }
    b.Pose("elbow_r", at(0.82f), {0, 0, -80 * b.k});
    b.Pose("elbow_r", D, o);
    b.Key("head", 0, o); b.Key("head", at(0.3f), {0, -8, 4}); b.Key("head", at(0.8f), {0, -8, 4}); b.Key("head", D, o);
    b.Key("upper_body", 0, o); b.Key("upper_body", at(0.3f), {0, 0, 3}); b.Key("upper_body", at(0.8f), {0, 0, 3}); b.Key("upper_body", D, o);
  } else if (type == "nod") {
    for (auto [f, x] : {std::pair{0.0f, 0.0f}, {0.2f, 14.0f}, {0.4f, 0.0f}, {0.6f, 12.0f}, {0.8f, 0.0f}, {1.0f, 0.0f}}) {
      b.Key("head", at(f), {x, 0, 0});
      b.Key("neck", at(f), {x * 0.4f, 0, 0});
    }
  } else if (type == "shake_head") {
    for (auto [f, y] : {std::pair{0.0f, 0.0f}, {0.15f, 18.0f}, {0.35f, -18.0f}, {0.55f, 16.0f}, {0.75f, -12.0f}, {1.0f, 0.0f}}) {
      b.Key("head", at(f), {0, y, 0});
      b.Key("neck", at(f), {0, y * 0.3f, 0});
    }
  } else if (type == "sway") {
    for (float f : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
      const float s = std::sin(f * 2 * glm::pi<float>());
      b.Key("center", at(f), o, {0.02f * s, 0, 0});
      b.Key("lower_body", at(f), {0, 0, -6 * s});
      b.Key("upper_body", at(f), {0, 0, 4 * s});
      b.Key("head", at(f), {0, 0, 3 * s});
    }
    b.RelaxedArms(D);
  } else if (type == "bow") {
    for (auto [f, x] : {std::pair{0.0f, 0.0f}, {0.35f, 35.0f}, {0.65f, 35.0f}, {1.0f, 0.0f}}) {
      b.Key("upper_body", at(f), {x, 0, 0});
      b.Key("head", at(f), {x * 0.3f, 0, 0});
      b.Key("lower_body", at(f), {-x * 0.15f, 0, 0});
    }
    b.RelaxedArms(D);
  } else if (type == "talk") {
    const float beats[][3] = {{0, 0, 0}, {0.12f, 5, 4}, {0.3f, -2, -3}, {0.45f, 4, 6},
                              {0.62f, 0, -5}, {0.8f, 5, 2}, {1, 0, 0}};
    for (const auto &bt : beats) {
      b.Key("head", at(bt[0]), {bt[1], bt[2], bt[2] * 0.3f});
      b.Key("upper_body", at(bt[0]), {bt[1] * 0.3f, bt[2] * 0.4f, 0});
    }
    // One forearm gestures while talking.
    b.Pose("arm_l", 0, {0, 0, b.ArmDown(1, 65)});
    b.Pose("arm_l", D, {0, 0, b.ArmDown(1, 65)});
    b.Pose("arm_r", 0, {0, 0, b.ArmDown(-1, 65)});
    const float rest = b.ArmDown(-1, 65);
    const float gesture = rest + (b.ArmDown(-1, 55) - rest) * b.k;
    b.Pose("arm_r", at(0.3f), {0, -15 * b.k, gesture});
    b.Pose("arm_r", at(0.7f), {0, -15 * b.k, gesture});
    b.Pose("arm_r", D, {0, 0, b.ArmDown(-1, 65)});
    b.Pose("elbow_r", 0, o);
    b.Pose("elbow_r", at(0.3f), {0, 45 * b.k, 0});
    b.Pose("elbow_r", at(0.5f), {0, 30 * b.k, 0});
    b.Pose("elbow_r", at(0.7f), {0, 45 * b.k, 0});
    b.Pose("elbow_r", D, o);
  } else if (type == "happy_bounce") {
    for (auto [f, y] : {std::pair{0.0f, 0.0f}, {0.15f, -0.015f}, {0.35f, 0.04f}, {0.55f, -0.01f},
                        {0.7f, 0.025f}, {0.85f, -0.005f}, {1.0f, 0.0f}}) {
      b.Key("center", at(f), o, {0, y, 0});
      b.Key("head", at(f), {-y * 150, 0, 0});
    }
    for (int side : {1, -1}) {
      const char *arm = side > 0 ? "arm_l" : "arm_r";
      const float rest = b.ArmDown(side, 65);
      b.Pose(arm, 0, {0, 0, rest});
      b.Pose(arm, at(0.35f), {0, 0, rest + (b.ArmDown(side, 30) - rest) * b.k});
      b.Pose(arm, at(0.7f), {0, 0, rest + (b.ArmDown(side, 40) - rest) * b.k});
      b.Pose(arm, D, {0, 0, rest});
    }
  } else if (type == "look_around") {
    for (auto [f, y, x] : {std::tuple{0.0f, 0.0f, 0.0f}, {0.2f, 35.0f, -3.0f}, {0.4f, 35.0f, -3.0f},
                           {0.6f, -35.0f, 2.0f}, {0.8f, -35.0f, 2.0f}, {1.0f, 0.0f, 0.0f}}) {
      b.Key("head", at(f), {x, y * 0.7f, 0});
      b.Key("neck", at(f), {0, y * 0.3f, 0});
      b.Key("upper_body", at(f), {0, y * 0.15f, 0});
    }
    b.RelaxedArms(D);
  }
  if (out.tracks.empty()) { error = "rig lacks the bones this motion needs"; return false; }
  if (!ValidateAnimation(out, error)) return false;
  target = std::move(out);
  return true;
}

} // namespace ilmeee
