#include "core_engine/AnimationController.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace ilmeee {
namespace fs = std::filesystem;
using json = nlohmann::json;
bool IsAnimationControllerPath(const std::string &path) {
  auto ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return ext == ".ilmeeecontroller";
}
const char *LocomotionName(LocomotionState state) {
  switch (state) {
    case LocomotionState::Walk: return "Walk";
    case LocomotionState::Run: return "Run";
    default: return "Idle";
  }
}
bool ValidateController(const AnimationController &c, std::string &error) {
  error.clear();
  if (c.idle.empty() || c.idle.size() > 4096 || c.walk.size() > 4096 || c.run.size() > 4096 ||
      !IsAnimationPath(c.idle) || (!c.walk.empty() && !IsAnimationPath(c.walk)) ||
      (!c.run.empty() && !IsAnimationPath(c.run)) || c.rootBone.size() > 1024 ||
      !std::isfinite(c.walkSpeed) || c.walkSpeed <= 0 || c.walkSpeed > 10000 ||
      !std::isfinite(c.runSpeed) || c.runSpeed <= c.walkSpeed || c.runSpeed > 10000 ||
      !std::isfinite(c.idleThreshold) || c.idleThreshold < 0 || c.idleThreshold >= c.walkSpeed ||
      !std::isfinite(c.transition) || c.transition < 0 || c.transition > 10) {
    error = "Choose an Idle clip, 0 <= idle threshold < walk speed < run speed, and a 0..10s transition.";
    return false;
  }
  return true;
}
bool LoadAnimationController(const std::string &path, AnimationController &out, std::string &error) {
  try {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > 65536) throw std::runtime_error("Missing controller or larger than 64 KB");
    std::ifstream file(path);
    const auto j = json::parse(file);
    if (j.at("format") != "IlmeeeAnimationController" || j.at("version") != 1)
      throw std::runtime_error("Unsupported controller format");
    AnimationController c;
    c.idle = j.at("idle").get<std::string>(); c.walk = j.value("walk", ""); c.run = j.value("run", "");
    c.walkSpeed = j.value("walkSpeed", 2.0f); c.runSpeed = j.value("runSpeed", 5.0f);
    c.idleThreshold = j.value("idleThreshold", 0.05f); c.transition = j.value("transition", 0.2f);
    c.inPlace = j.value("inPlace", true); c.rootBone = j.value("rootBone", "センター");
    if (!ValidateController(c, error)) return false;
    out = std::move(c); error.clear(); return true;
  } catch (const std::exception &e) { error = std::string("Cannot load controller: ") + e.what(); return false; }
}
bool SaveAnimationController(const std::string &path, const AnimationController &c, std::string &error) {
  if (!ValidateController(c, error)) return false;
  try {
    const json j = {{"format", "IlmeeeAnimationController"}, {"version", 1},
      {"idle", c.idle}, {"walk", c.walk}, {"run", c.run}, {"walkSpeed", c.walkSpeed},
      {"runSpeed", c.runSpeed}, {"idleThreshold", c.idleThreshold}, {"transition", c.transition},
      {"inPlace", c.inPlace}, {"rootBone", c.rootBone}};
    const fs::path target(path);
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path());
    const auto temp = path + ".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << j.dump(2) << '\n'; file.close();
    if (!file) throw std::runtime_error("Cannot write controller");
    fs::rename(temp, target); error.clear(); return true;
  } catch (const std::exception &e) { error = std::string("Cannot save controller: ") + e.what(); return false; }
}
bool LoadControllerClips(const std::string &path, const AnimationController &c,
                         std::array<AnimationClip, 3> &out, std::string &error) {
  if (!ValidateController(c, error)) return false;
  std::array<AnimationClip, 3> clips;
  const std::array<std::string, 3> paths{c.idle, c.walk, c.run};
  for (int i = 0; i < 3; ++i) {
    if (paths[i].empty()) { clips[i] = clips[i == 2 && !c.walk.empty() ? 1 : 0]; continue; }
    fs::path p(paths[i]); if (p.is_relative()) p = fs::path(path).parent_path() / p;
    if (!LoadAnimationClip(p.string(), clips[i], error)) {
      error = std::string(LocomotionName(static_cast<LocomotionState>(i))) + ": " + error; return false;
    }
  }
  out = std::move(clips); error.clear(); return true;
}
void LocomotionPlayer::Reset() { *this = LocomotionPlayer{}; }
AnimationClip LocomotionPlayer::Update(const std::vector<SkeletonBone> &bones,
    const AnimationController &c, const std::array<AnimationClip, 3> &clips,
    float horizontalSpeed, float dt, float playbackSpeed) {
  dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.25f) : 0;
  horizontalSpeed = std::isfinite(horizontalSpeed) ? std::max(0.0f, horizontalSpeed) : 0;
  playbackSpeed = std::isfinite(playbackSpeed) ? std::clamp(playbackSpeed, 0.0f, 100.0f) : 1;
  speed_ = horizontalSpeed;
  // Hysteresis avoids flickering at standstill and walk/run boundaries.
  const float threshold = c.idleThreshold * (state_ == LocomotionState::Idle ? 1.5f : 1);
  const float runAt = (c.walkSpeed + c.runSpeed) * 0.5f;
  const float runHysteresis = (c.runSpeed - c.walkSpeed) * 0.05f;
  LocomotionState next = LocomotionState::Idle;
  if (horizontalSpeed > threshold && (!c.walk.empty() || !c.run.empty())) {
    next = (!c.run.empty() && (c.walk.empty() || horizontalSpeed > runAt +
        (state_ == LocomotionState::Run ? -runHysteresis : runHysteresis)))
        ? LocomotionState::Run : LocomotionState::Walk;
  }
  if (pose_.size() != bones.size()) { initialized_ = false; pose_.assign(bones.size(), {}); }
  if (next != state_) {
    from_ = pose_; blend_ = initialized_ ? 0 : 1;
    // Walk/run share cycle phase to keep the same foot forward.
    if (next == LocomotionState::Idle || state_ == LocomotionState::Idle) phase_ = 0;
    state_ = next;
  }
  const int index = static_cast<int>(state_);
  const auto &clip = clips[index];
  const float reference = state_ == LocomotionState::Run ? c.runSpeed : c.walkSpeed;
  const float rate = state_ == LocomotionState::Idle ? 1 : std::clamp(horizontalSpeed / reference, 0.35f, 1.75f);
  phase_ = AnimationTime(phase_ + dt * rate * playbackSpeed / std::max(clip.duration, 0.001f), 1, true);
  std::unordered_map<std::string, const BoneTrack *> tracks;
  for (const auto &track : clip.tracks) tracks[track.bone] = &track;
  blend_ = c.transition > 0 ? std::min(1.0f, blend_ + dt / c.transition) : 1;
  // Smoothstep gives a zero-slope beginning/end to each crossfade.
  const float blend = blend_ * blend_ * (3 - 2 * blend_);
  AnimationClip result; result.name = LocomotionName(state_); result.duration = 1;
  for (size_t i = 0; i < bones.size(); ++i) {
    BoneTransform value;
    if (const auto it = tracks.find(bones[i].name); it != tracks.end())
      value = SampleBone(*it->second, phase_ * clip.duration);
    // Only selected/root carrier translations are removed. Hip bounce stays.
    if (c.inPlace && (bones[i].name == c.rootBone || bones[i].name == "全ての親" ||
                      bones[i].name == "グルーブ" || bones[i].name == "センター")) {
      value.translation.x = 0; value.translation.z = 0;
    }
    if (initialized_ && blend_ < 1 && from_.size() == bones.size()) {
      const auto &a = from_[i];
      value.translation = glm::mix(a.translation, value.translation, blend);
      value.rotation = glm::normalize(glm::slerp(a.rotation, value.rotation, blend));
      value.scale = glm::mix(a.scale, value.scale, blend);
    }
    pose_[i] = value;
    result.tracks.push_back({bones[i].name, {{0, value}}});
  }
  initialized_ = true; return result;
}
} // namespace ilmeee
