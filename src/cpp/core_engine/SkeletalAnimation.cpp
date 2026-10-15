#include "core_engine/SkeletalAnimation.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <unordered_set>

namespace ilmeee {
namespace {
using json = nlohmann::json;
bool Finite(const glm::vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Valid(const BoneTransform &v) {
  return Finite(v.translation) && Finite(v.scale) &&
      v.scale.x > 0.0001f && v.scale.y > 0.0001f && v.scale.z > 0.0001f &&
      std::isfinite(v.rotation.x) && std::isfinite(v.rotation.y) &&
      std::isfinite(v.rotation.z) && std::isfinite(v.rotation.w) &&
      glm::length(v.rotation) > 0.0001f;
}
json Vec(const glm::vec3 &v) { return json::array({v.x, v.y, v.z}); }
glm::vec3 ReadVec(const json &v) {
  if (!v.is_array() || v.size() != 3) throw std::runtime_error("Expected XYZ");
  return {v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()};
}
glm::mat4 Blend(const glm::ivec4 &joints, const glm::vec4 &weights,
                const std::vector<glm::mat4> &matrices) {
  glm::mat4 blended(0);
  float total = 0;
  for (int i = 0; i < 4; ++i) {
    if (joints[i] < 0 || static_cast<size_t>(joints[i]) >= matrices.size() ||
        !std::isfinite(weights[i]) || weights[i] <= 0) continue;
    blended += matrices[joints[i]] * weights[i];
    total += weights[i];
  }
  return total > 0.00001f ? blended / total : glm::mat4(1);
}
} // namespace

bool ValidateAnimation(const AnimationClip &clip, std::string &error) {
  error.clear();
  if (!std::isfinite(clip.duration) || clip.duration <= 0 || clip.duration > 3600 ||
      !std::isfinite(clip.fps) || clip.fps < 1 || clip.fps > 240 ||
      clip.name.size() > 1024 || clip.tracks.size() > 4096) {
    error = "Duration must be 0..3600 seconds and FPS 1..240";
    return false;
  }
  std::unordered_set<std::string> names;
  size_t keyCount = 0;
  for (const auto &track : clip.tracks) {
    if (track.bone.empty() || track.bone.size() > 1024 ||
        !names.insert(track.bone).second) {
      error = "Bone tracks must have unique names";
      return false;
    }
    keyCount += track.keys.size();
    if (keyCount > 100000) { error = "Too many keyframes"; return false; }
    float previous = -1;
    for (const auto &key : track.keys) {
      if (!std::isfinite(key.time) || key.time < 0 || key.time > clip.duration ||
          key.time <= previous || !Valid(key.value)) {
        error = "Invalid keyframe: check time, rotation and positive scale";
        return false;
      }
      previous = key.time;
    }
  }
  return true;
}

bool LoadAnimationClip(const std::string &path, AnimationClip &out,
                       std::string &error) {
  try {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 16 * 1024 * 1024) {
      error = "Cannot read animation (missing or larger than 16 MB)"; return false;
    }
    std::ifstream in(path);
    const json data = json::parse(in);
    if (data.at("format") != "IlmeeeAnimation" || data.at("version") != 1)
      throw std::runtime_error("Unsupported animation format");
    AnimationClip clip;
    clip.name = data.at("name").get<std::string>();
    clip.duration = data.at("duration").get<float>();
    clip.fps = data.value("fps", 30.0f);
    const auto &tracks = data.at("tracks");
    if (!tracks.is_array() || tracks.size() > 4096)
      throw std::runtime_error("Invalid bone tracks");
    for (const auto &t : tracks) {
      BoneTrack track;
      track.bone = t.at("bone").get<std::string>();
      if (!t.at("keys").is_array()) throw std::runtime_error("Invalid keys");
      for (const auto &k : t.at("keys")) {
        BoneKeyframe key;
        key.time = k.at("time").get<float>();
        key.value.translation = ReadVec(k.at("translation"));
        key.value.scale = ReadVec(k.at("scale"));
        const auto &q = k.at("rotation"); // JSON stores quaternion XYZW
        if (!q.is_array() || q.size() != 4) throw std::runtime_error("Invalid rotation");
        key.value.rotation = {q.at(3).get<float>(), q.at(0).get<float>(),
                              q.at(1).get<float>(), q.at(2).get<float>()};
        track.keys.push_back(key);
      }
      clip.tracks.push_back(std::move(track));
    }
    if (!ValidateAnimation(clip, error)) return false;
    for (auto &track : clip.tracks)
      for (auto &key : track.keys)
        key.value.rotation = glm::normalize(key.value.rotation);
    out = std::move(clip);
    return true;
  } catch (const std::exception &e) {
    error = std::string("Cannot load animation: ") + e.what(); return false;
  }
}

bool SaveAnimationClip(const std::string &path, const AnimationClip &clip,
                       std::string &error) {
  if (!ValidateAnimation(clip, error)) return false;
  try {
    json tracks = json::array();
    for (const auto &track : clip.tracks) {
      json keys = json::array();
      for (const auto &key : track.keys) {
        const auto &v = key.value;
        const glm::quat q = glm::normalize(v.rotation);
        keys.push_back({{"time", key.time}, {"translation", Vec(v.translation)},
                         {"rotation", json::array({q.x, q.y, q.z, q.w})},
                         {"scale", Vec(v.scale)}});
      }
      tracks.push_back({{"bone", track.bone}, {"keys", std::move(keys)}});
    }
    const json data = {{"format", "IlmeeeAnimation"}, {"version", 1},
        {"name", clip.name}, {"duration", clip.duration}, {"fps", clip.fps},
        {"tracks", std::move(tracks)}};
    const std::filesystem::path target(path);
    if (!target.parent_path().empty())
      std::filesystem::create_directories(target.parent_path());
    // Serialize first and commit through a sibling temporary: a failed save
    // must not truncate a clip the artist can still reopen.
    const auto temporary = target.string() + ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      file << data.dump(2) << '\n';
      file.close();
      if (!file) throw std::runtime_error("Cannot write animation file");
    }
    std::filesystem::rename(temporary, target);
    error.clear();
    return true;
  } catch (const std::exception &e) {
    error = std::string("Cannot save animation: ") + e.what(); return false;
  }
}

float AnimationTime(float time, float duration, bool loop) {
  if (!std::isfinite(time) || !std::isfinite(duration) || duration <= 0) return 0;
  return loop ? std::fmod(std::max(0.0f, time), duration)
              : std::clamp(time, 0.0f, duration);
}

BoneTransform SampleBone(const BoneTrack &track, float time) {
  if (track.keys.empty()) return {};
  if (!std::isfinite(time) || time <= track.keys.front().time)
    return track.keys.front().value;
  if (time >= track.keys.back().time) return track.keys.back().value;
  const auto next = std::upper_bound(track.keys.begin(), track.keys.end(), time,
      [](float t, const BoneKeyframe &key) { return t < key.time; });
  const auto &a = *(next - 1), &b = *next;
  const float t = (time - a.time) / (b.time - a.time);
  BoneTransform result;
  result.translation = glm::mix(a.value.translation, b.value.translation, t);
  result.rotation = glm::normalize(glm::slerp(glm::normalize(a.value.rotation),
                                              glm::normalize(b.value.rotation), t));
  result.scale = glm::mix(a.value.scale, b.value.scale, t);
  return result;
}

void SetBoneKey(AnimationClip &clip, const std::string &bone,
                const BoneKeyframe &key) {
  auto track = std::find_if(clip.tracks.begin(), clip.tracks.end(),
                            [&](const auto &t) { return t.bone == bone; });
  if (track == clip.tracks.end()) {
    clip.tracks.push_back({bone, {}}); track = clip.tracks.end() - 1;
  }
  auto at = std::lower_bound(track->keys.begin(), track->keys.end(), key.time,
      [](const auto &k, float t) { return k.time < t; });
  if (at != track->keys.end() && std::abs(at->time - key.time) < 0.0001f)
    *at = key;
  else if (at != track->keys.begin() && std::abs((at - 1)->time - key.time) < 0.0001f)
    *(at - 1) = key;
  else track->keys.insert(at, key);
}

bool DeleteBoneKey(AnimationClip &clip, const std::string &bone, float time) {
  for (auto track = clip.tracks.begin(); track != clip.tracks.end(); ++track) {
    if (track->bone != bone) continue;
    auto &keys = track->keys;
    const auto key = std::find_if(keys.begin(), keys.end(),
        [&](const auto &k) { return std::abs(k.time - time) < 0.0001f; });
    if (key == keys.end()) return false;
    keys.erase(key);
    if (keys.empty()) clip.tracks.erase(track);
    return true;
  }
  return false;
}

bool EvaluateSkeleton(const std::vector<SkeletonBone> &bones,
                      const AnimationClip &clip, float time,
                      std::vector<glm::mat4> &matrices,
                      std::vector<glm::vec3> &positions) {
  if (bones.size() > 4096) return false;
  std::unordered_map<std::string, const BoneTrack *> tracks;
  for (const auto &track : clip.tracks) tracks[track.bone] = &track;
  std::vector<glm::mat4> world(bones.size(), glm::mat4(1));
  std::vector<uint8_t> visited(bones.size(), 0);
  matrices.resize(bones.size()); positions.resize(bones.size());
  std::function<bool(size_t)> visit = [&](size_t i) {
    if (visited[i] == 2) return true;
    if (visited[i] == 1) return false;
    visited[i] = 1;
    const auto &bone = bones[i];
    if (!Finite(bone.bindPosition) || bone.parent < -1 ||
        bone.parent >= static_cast<int>(bones.size()) || bone.parent == int(i)) return false;
    glm::vec3 offset = bone.bindPosition;
    glm::mat4 parent(1);
    if (bone.parent >= 0) {
      if (!visit(bone.parent)) return false;
      parent = world[bone.parent];
      offset -= bones[bone.parent].bindPosition;
    }
    BoneTransform pose;
    if (const auto found = tracks.find(bone.name); found != tracks.end())
      pose = SampleBone(*found->second, time);
    if (!Valid(pose)) return false;
    world[i] = parent * glm::translate(glm::mat4(1), offset + pose.translation) *
        glm::mat4_cast(glm::normalize(pose.rotation)) *
        glm::scale(glm::mat4(1), pose.scale);
    matrices[i] = world[i] * glm::translate(glm::mat4(1), -bone.bindPosition);
    positions[i] = glm::vec3(world[i][3]);
    visited[i] = 2;
    return true;
  };
  for (size_t i = 0; i < bones.size(); ++i)
    if (!visit(i)) { matrices.clear(); positions.clear(); return false; }
  return true;
}

glm::vec3 SkinPosition(const glm::vec3 &p, const glm::ivec4 &joints,
                       const glm::vec4 &weights,
                       const std::vector<glm::mat4> &matrices) {
  return glm::vec3(Blend(joints, weights, matrices) * glm::vec4(p, 1));
}
glm::vec3 SkinNormal(const glm::vec3 &n, const glm::ivec4 &joints,
                     const glm::vec4 &weights,
                     const std::vector<glm::mat4> &matrices) {
  const glm::mat3 m(Blend(joints, weights, matrices));
  const glm::vec3 transformed = std::abs(glm::determinant(m)) > 0.000001f
      ? glm::transpose(glm::inverse(m)) * n : m * n;
  const float length = glm::length(transformed);
  return length > 0.000001f ? transformed / length : n;
}

AnimationClip MakeBreathingClip(const std::vector<SkeletonBone> &bones,
                                float height) {
  AnimationClip clip;
  clip.name = "Idle Breathing";
  clip.duration = 4;
  const auto find = [&](std::initializer_list<const char *> names) -> std::string {
    for (const auto *name : names)
      for (const auto &bone : bones)
        if (bone.name == name) return bone.name;
    return {};
  };
  const std::string chest = find({"上半身2", "上半身", "Chest", "chest", "Spine", "spine", "upper_body"});
  const std::string head = find({"頭", "Head", "head"});
  for (const auto &name : {chest, head}) {
    if (name.empty()) continue;
    for (int frame = 0; frame <= 4; ++frame) {
      const float phase = std::sin(float(frame) * glm::pi<float>() * 0.5f);
      BoneTransform pose;
      pose.rotation = glm::angleAxis(glm::radians(phase * (name == head ? -0.35f : 0.65f)), glm::vec3(1, 0, 0));
      if (name == chest)
        pose.translation.y = phase * std::max(0.0f, height) * 0.0008f;
      SetBoneKey(clip, name, {float(frame), pose});
    }
  }
  for (auto [name, angle] : {std::pair{find({"左腕", "LeftArm", "left_arm"}), -25.0f},
                             std::pair{find({"右腕", "RightArm", "right_arm"}), 25.0f}}) {
    if (name.empty()) continue;
    BoneTransform pose;
    pose.rotation = glm::angleAxis(glm::radians(angle), glm::vec3(0, 0, 1));
    SetBoneKey(clip, name, {0, pose});
    SetBoneKey(clip, name, {clip.duration, pose});
  }
  return clip;
}
} // namespace ilmeee
