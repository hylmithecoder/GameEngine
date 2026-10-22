#include "core_engine/SkeletalAnimation.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <unistd.h>
#include <SDL3/SDL_stdinc.h>
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
float Curve(const glm::vec4 &c, float x) {
  if (c.x == c.y && c.z == c.w) return x;
  auto cubic = [](float t, float a, float b) {
    const float u = 1 - t;
    return 3*u*u*t*a + 3*u*t*t*b + t*t*t;
  };
  float lo = 0, hi = 1;
  for (int i = 0; i < 18; ++i) {
    const float t = (lo + hi) * 0.5f;
    if (cubic(t, c.x, c.z) < x) lo = t; else hi = t;
  }
  return cubic((lo + hi) * 0.5f, c.y, c.w);
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
      for (const auto &c : key.curves)
        for (int i = 0; i < 4; ++i)
          if (!std::isfinite(c[i]) || c[i] < 0 || c[i] > 1) {
            error = "Invalid animation interpolation curve"; return false;
          }
    }
  }
  return true;
}

bool LoadAnimationClip(const std::string &path, AnimationClip &out,
                       std::string &error) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (ext == ".vmd") return LoadVmdAnimation(path, out, error);
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
        if (k.contains("curves")) {
          const auto &curves = k.at("curves");
          if (!curves.is_array() || curves.size() != 4) throw std::runtime_error("Invalid curves");
          for (int i = 0; i < 4; ++i) {
            const auto &c = curves.at(i);
            if (!c.is_array() || c.size() != 4) throw std::runtime_error("Invalid curve");
            key.curves[i] = {c.at(0).get<float>(), c.at(1).get<float>(), c.at(2).get<float>(), c.at(3).get<float>()};
          }
        }
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

bool IsAnimationPath(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return ext == ".vmd" || ext == ".ilmeeeanim";
}

bool LoadVmdAnimation(const std::string &path, AnimationClip &out, std::string &error) {
  try {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 16 * 1024 * 1024) throw std::runtime_error("Missing VMD or larger than 16 MB");
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(data.data()), data.size())) throw std::runtime_error("Cannot read VMD");
    size_t pos = 0;
    auto bytes = [&](size_t n) {
      if (n > data.size() - pos) throw std::runtime_error("Truncated VMD");
      const auto *p = data.data() + pos; pos += n; return p;
    };
    auto u32 = [&]() {
      const auto *p = bytes(4);
      return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    };
    auto number = [&]() {
      const uint32_t bits = u32(); float v; std::memcpy(&v, &bits, 4);
      if (!std::isfinite(v)) throw std::runtime_error("Non-finite VMD value");
      return v;
    };
    const auto *header = bytes(30);
    const std::string signature(reinterpret_cast<const char *>(header), 25);
    const bool v2 = signature.starts_with("Vocaloid Motion Data 0002");
    if (!v2 && !signature.starts_with("Vocaloid Motion Data file")) throw std::runtime_error("Unsupported VMD signature");
    bytes(v2 ? 20 : 10); // model name (not needed for bone binding)
    const uint32_t count = u32();
    if (count > 100000 || count > (data.size() - pos) / 111) throw std::runtime_error("Invalid VMD bone key count");
    AnimationClip clip;
    clip.name = std::filesystem::path(path).stem().string();
    clip.fps = 30;
    clip.duration = 1.0f / 30;
    std::unordered_map<std::string, size_t> tracks;
    for (uint32_t i = 0; i < count; ++i) {
      const auto *raw = bytes(15);
      size_t length = 0;
      while (length < 15 && raw[length]) ++length;
      // SDL uses the platform converter, including CP932 Japanese bone names.
      char *converted = SDL_iconv_string("UTF-8", "CP932", reinterpret_cast<const char *>(raw), length);
      if (!converted) throw std::runtime_error("Cannot decode CP932 bone name");
      const std::string name(converted); SDL_free(converted);
      if (name.empty()) throw std::runtime_error("Empty VMD bone name");
      const uint32_t frame = u32();
      if (frame > 108000) throw std::runtime_error("VMD exceeds one hour");
      BoneKeyframe key;
      key.time = float(frame) / 30;
      const float x = number(), y = number(), z = number();
      key.value.translation = {x, y, -z};
      const float qx = number(), qy = number(), qz = number(), qw = number();
      // Reflect MMD's left-handed coordinates just like the PMX loader.
      key.value.rotation = {qw, -qx, -qy, qz};
      if (!Valid(key.value)) throw std::runtime_error("Invalid VMD quaternion");
      key.value.rotation = glm::normalize(key.value.rotation);
      const auto *curve = bytes(64);
      for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 4; ++k)
          if (curve[c + k*4] > 127) throw std::runtime_error("Invalid VMD Bezier control point");
        key.curves[c] = glm::vec4(curve[c], curve[c+4], curve[c+8], curve[c+12]) / 127.0f;
      }
      const auto [it, inserted] = tracks.emplace(name, clip.tracks.size());
      if (inserted) clip.tracks.push_back({name, {}});
      clip.tracks[it->second].keys.push_back(key);
      clip.duration = std::max(clip.duration, key.time);
    }
    // Validate optional trailing sections even though FK playback ignores them.
    for (size_t stride : {23u, 61u, 28u, 9u}) {
      if (pos == data.size()) break;
      const auto n = u32();
      if (n > (data.size() - pos) / stride) throw std::runtime_error("Truncated VMD section");
      bytes(size_t(n) * stride);
    }
    if (pos < data.size()) {
      const auto n = u32();
      if (n > (data.size() - pos) / 9) throw std::runtime_error("Invalid VMD IK count");
      for (uint32_t i = 0; i < n; ++i) {
        bytes(5); const auto ik = u32();
        if (ik > (data.size() - pos) / 21) throw std::runtime_error("Truncated VMD IK section");
        bytes(size_t(ik) * 21);
      }
    }
    if (pos != data.size()) throw std::runtime_error("Unexpected VMD trailing data");
    if (clip.tracks.empty()) throw std::runtime_error("No bone motion (camera/morph-only VMD is not supported)");
    for (auto &track : clip.tracks) {
      std::stable_sort(track.keys.begin(), track.keys.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
      // Some exporters repeat the same frame. Last record wins.
      std::vector<BoneKeyframe> unique;
      for (const auto &key : track.keys) {
        if (!unique.empty() && unique.back().time == key.time) unique.back() = key;
        else unique.push_back(key);
      }
      track.keys = std::move(unique);
    }
    if (!ValidateAnimation(clip, error)) return false;
    out = std::move(clip); error.clear(); return true;
  } catch (const std::exception &e) {
    error = std::string("Cannot load VMD: ") + e.what(); return false;
  }
}

bool SaveAnimationClip(const std::string &path, const AnimationClip &clip,
                       std::string &error) {
  if (!ValidateAnimation(clip, error)) return false;
  std::string temporary;
  int fd = -1;
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
        json curves = json::array();
        for (const auto &c : key.curves) curves.push_back({c.x, c.y, c.z, c.w});
        keys.back()["curves"] = std::move(curves);
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
    // Exclusive creation avoids following a pre-existing .tmp symlink and
    // lets two saves prepare independent complete files.
    const std::string serialized = data.dump(2) + '\n';
    const auto pattern = target.string() + ".tmp.XXXXXX";
    std::vector<char> filename(pattern.begin(), pattern.end());
    filename.push_back('\0');
    fd = mkstemp(filename.data());
    if (fd < 0) throw std::runtime_error("Cannot create animation temporary file");
    temporary = filename.data();
    size_t offset = 0;
    while (offset < serialized.size()) {
      const auto n = write(fd, serialized.data() + offset, serialized.size() - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) throw std::runtime_error("Cannot write animation file");
      offset += static_cast<size_t>(n);
    }
    const int closed = close(fd);
    fd = -1;
    if (closed != 0) throw std::runtime_error("Cannot close animation file");
    std::filesystem::rename(temporary, target);
    temporary.clear();
    error.clear();
    return true;
  } catch (const std::exception &e) {
    if (fd >= 0) close(fd);
    if (!temporary.empty()) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
    }
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
  for (int i = 0; i < 3; ++i)
    result.translation[i] = glm::mix(a.value.translation[i], b.value.translation[i], Curve(b.curves[i], t));
  result.rotation = glm::normalize(glm::slerp(glm::normalize(a.value.rotation),
                                              glm::normalize(b.value.rotation), Curve(b.curves[3], t)));
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

bool EvaluateSkeletonPose(const std::vector<SkeletonBone> &bones,
                          const AnimationClip &clip, float time,
                          std::vector<glm::mat4> &world) {
  world.clear();
  if (bones.size() > 4096) return false;
  std::unordered_map<std::string, const BoneTrack *> tracks;
  for (const auto &track : clip.tracks) tracks[track.bone] = &track;
  world.assign(bones.size(), glm::mat4(1));
  std::vector<uint8_t> visited(bones.size(), 0);
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
    visited[i] = 2;
    return true;
  };
  for (size_t i = 0; i < bones.size(); ++i)
    if (!visit(i)) { world.clear(); return false; }
  return true;
}

void BuildSkinMatrices(const std::vector<SkeletonBone> &bones,
                       const std::vector<glm::mat4> &world,
                       std::vector<glm::mat4> &matrices,
                       std::vector<glm::vec3> &positions) {
  const size_t count = std::min(bones.size(), world.size());
  matrices.resize(count); positions.resize(count);
  for (size_t i = 0; i < count; ++i) {
    matrices[i] = world[i] * glm::translate(glm::mat4(1), -bones[i].bindPosition);
    positions[i] = glm::vec3(world[i][3]);
  }
}

bool EvaluateSkeleton(const std::vector<SkeletonBone> &bones,
                      const AnimationClip &clip, float time,
                      std::vector<glm::mat4> &matrices,
                      std::vector<glm::vec3> &positions) {
  std::vector<glm::mat4> world;
  if (!EvaluateSkeletonPose(bones, clip, time, world)) {
    matrices.clear(); positions.clear(); return false;
  }
  BuildSkinMatrices(bones, world, matrices, positions);
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
