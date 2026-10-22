#include "core_engine/AnimationTooling.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>

namespace ilmeee {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;

std::string String(const json &args, const char *key, std::string fallback = {}) {
  const auto it = args.find(key);
  if (it == args.end()) return fallback;
  if (!it->is_string()) throw std::runtime_error(std::string(key) + " must be a string");
  return it->get<std::string>();
}
float Number(const json &args, const char *key, float fallback, float lo, float hi) {
  const auto it = args.find(key);
  if (it == args.end()) return fallback;
  if (!it->is_number()) throw std::runtime_error(std::string(key) + " must be a number");
  const double n = it->get<double>();
  if (!std::isfinite(n) || n < lo || n > hi)
    throw std::runtime_error(std::string(key) + " out of range");
  return static_cast<float>(n);
}
bool Boolean(const json &args, const char *key, bool fallback = false) {
  const auto it = args.find(key);
  if (it == args.end()) return fallback;
  if (!it->is_boolean()) throw std::runtime_error(std::string(key) + " must be a boolean");
  return it->get<bool>();
}
size_t Integer(const json &args, const char *key, size_t fallback, size_t hi) {
  const auto it = args.find(key);
  if (it == args.end()) return fallback;
  if (!it->is_number_integer()) throw std::runtime_error(std::string(key) + " must be an integer");
  if ((!it->is_number_unsigned() && it->get<int64_t>() < 0) || it->get<uint64_t>() > hi)
    throw std::runtime_error(std::string(key) + " out of range");
  return it->get<size_t>();
}
json Vec(const glm::vec3 &v) { return {v.x, v.y, v.z}; }
json Transform(const BoneTransform &v) {
  return {{"translation", Vec(v.translation)},
          {"rotation", Vec(QuatToEulerDegrees(v.rotation))}, {"scale", Vec(v.scale)}};
}
std::string BoneFilter(const json &args, const std::vector<SkeletonBone> &bones) {
  const auto ref = String(args, "bone");
  if (ref.empty()) return {};
  std::string name, error;
  if (!ResolveBone(bones, ref, name, error)) throw std::runtime_error(error);
  return name;
}
json Summary(const AnimationClip &clip, const std::vector<SkeletonBone> &bones) {
  json tracks = json::array(), unmatched = json::array();
  size_t keys = 0, matched = 0;
  std::unordered_set<std::string> names;
  for (const auto &b : bones) names.insert(b.name);
  for (const auto &t : clip.tracks) {
    keys += t.keys.size();
    tracks.push_back({{"bone", t.bone}, {"keys", t.keys.size()}});
    if (!bones.empty()) {
      if (names.count(t.bone)) ++matched;
      else unmatched.push_back(t.bone);
    }
  }
  json out = {{"name", clip.name}, {"duration", clip.duration}, {"fps", clip.fps},
              {"trackCount", clip.tracks.size()}, {"keyCount", keys}, {"tracks", tracks}};
  if (!bones.empty()) {
    out["matchedTracks"] = matched;
    out["unmatchedBones"] = unmatched;
  }
  return out;
}
} // namespace

bool ResolveAnimationAsset(const std::string &project, const std::string &path,
                           bool writing, std::string &absolute,
                           std::string &relative, std::string &error) {
  try {
    if (project.empty()) throw std::runtime_error("No project is open");
    if (path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos ||
        fs::path(path).is_absolute())
      throw std::runtime_error("path must be a nonempty project-relative path");
    const auto root = fs::canonical(project);
    if (!fs::is_directory(root)) throw std::runtime_error("Project is not a directory");
    const auto candidate = fs::weakly_canonical(root / path);
    const auto rel = candidate.lexically_relative(root);
    if (rel.empty() || rel == "." || *rel.begin() == ".." || rel.is_absolute())
      throw std::runtime_error("Animation assets must stay inside the active project");
    if (!writing && !fs::is_regular_file(candidate))
      throw std::runtime_error("File not found: " + path);
    if (writing && fs::exists(candidate) && !fs::is_regular_file(candidate))
      throw std::runtime_error("Output is not a regular file");
    absolute = candidate.string();
    relative = rel.generic_string();
    error.clear();
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}

json HandleAnimationTool(const std::string &tool, const json &args,
                         const std::string &project,
                         const std::vector<SkeletonBone> &bones) {
  try {
    if (!args.is_object()) throw std::runtime_error("Arguments must be an object");
    if (tool == "get_skeleton") {
      if (bones.empty()) throw std::runtime_error("Select a skinned object with bones");
      const auto group = String(args, "group"), contains = String(args, "contains");
      const size_t offset = Integer(args, "offset", 0, 100000);
      const size_t limit = Integer(args, "limit", 200, 1000);
      if (!limit) throw std::runtime_error("limit must be 1..1000");
      json roles = json::object(), groups = json::object(), entries = json::array();
      for (const auto &[role, i] : HumanoidRoles(bones))
        roles[role] = {{"index", i}, {"name", bones[i].name}};
      size_t filtered = 0;
      for (size_t i = 0; i < bones.size(); ++i) {
        const auto &b = bones[i];
        const auto g = BoneGroup(bones, int(i));
        groups[g] = groups.value(g, size_t(0)) + 1;
        if ((!group.empty() && group != g) || (!contains.empty() &&
            b.name.find(contains) == std::string::npos && b.altName.find(contains) == std::string::npos)) continue;
        if (filtered++ < offset || entries.size() >= limit) continue;
        entries.push_back({{"index", i}, {"name", b.name}, {"altName", b.altName},
                           {"parent", b.parent}, {"bindPosition", Vec(b.bindPosition)}, {"group", g}});
      }
      return {{"boneCount", bones.size()}, {"roles", roles}, {"groups", groups},
              {"generatedTypes", GeneratedAnimationTypes()}, {"bones", entries},
              {"total", filtered}, {"offset", offset},
              {"nextOffset", offset + entries.size() < filtered ? json(offset + entries.size()) : json(nullptr)},
              {"axes", "+Z forward, +X character left, +Y up; rotation is local Euler degrees"}};
    }

    const bool creating = tool == "create_animation" || tool == "generate_animation";
    const bool editing = tool == "edit_animation";
    if (!creating && !editing && tool != "get_animation" && tool != "sample_animation")
      throw std::runtime_error("Unknown animation tool");
    const auto path = String(args, "path");
    const auto output = editing ? String(args, "output", path) : path;
    std::string absolute, relative, error;
    if (!ResolveAnimationAsset(project, path, creating, absolute, relative, error))
      throw std::runtime_error(error);
    if (!IsAnimationPath(absolute)) throw std::runtime_error("Expected .ilmeeeanim or .vmd");
    AnimationClip clip;
    if (!creating && !LoadAnimationClip(absolute, clip, error)) throw std::runtime_error(error);

    if (tool == "get_animation") {
      json result = Summary(clip, bones);
      result["path"] = relative;
      const auto bone = BoneFilter(args, bones);
      const bool include = Boolean(args, "includeKeys");
      const size_t offset = Integer(args, "offset", 0, 100000);
      const size_t limit = Integer(args, "limit", 200, 1000);
      if (!limit) throw std::runtime_error("limit must be 1..1000");
      if (include) {
        json keys = json::array();
        size_t total = 0;
        for (const auto &track : clip.tracks) {
          if (!bone.empty() && track.bone != bone) continue;
          for (const auto &k : track.keys) {
            if (total++ < offset || keys.size() >= limit) continue;
            json key = Transform(k.value);
            key["bone"] = track.bone; key["time"] = k.time;
            key["curves"] = json::array();
            for (const auto &c : k.curves) key["curves"].push_back({c.x, c.y, c.z, c.w});
            keys.push_back(std::move(key));
          }
        }
        result["keys"] = keys; result["total"] = total; result["offset"] = offset;
        result["nextOffset"] = offset + keys.size() < total ? json(offset + keys.size()) : json(nullptr);
      }
      return result;
    }
    if (tool == "sample_animation") {
      if (!args.contains("time")) throw std::runtime_error("time is required");
      const float requested = Number(args, "time", 0, 0, 3600);
      const float time = AnimationTime(requested, clip.duration, Boolean(args, "loop"));
      const auto bone = BoneFilter(args, bones);
      json pose = json::array();
      if (bones.empty()) {
        for (const auto &t : clip.tracks) if (bone.empty() || bone == t.bone) {
          json v = Transform(SampleBone(t, time)); v["bone"] = t.bone;
          pose.push_back(std::move(v));
        }
      } else {
        std::vector<glm::mat4> world;
        if (!EvaluateSkeletonPose(bones, clip, time, world)) throw std::runtime_error("Invalid bone hierarchy or pose");
        for (size_t i = 0; i < bones.size(); ++i) if (bone.empty() || bone == bones[i].name) {
          const auto t = std::find_if(clip.tracks.begin(), clip.tracks.end(),
                                     [&](const auto &t) { return t.bone == bones[i].name; });
          json v = Transform(t == clip.tracks.end() ? BoneTransform{} : SampleBone(*t, time));
          v["bone"] = bones[i].name; v["position"] = Vec(glm::vec3(world[i][3]));
          pose.push_back(std::move(v));
        }
      }
      return {{"path", relative}, {"time", time}, {"pose", pose},
              {"physics", false}, {"space", "local transforms; position is model-space FK"}};
    }

    std::string target, targetRelative;
    if (!ResolveAnimationAsset(project, output, true, target, targetRelative, error))
      throw std::runtime_error(error);
    if (fs::path(target).extension() != ".ilmeeeanim")
      throw std::runtime_error("Output must be .ilmeeeanim; use output to convert a VMD");
    const bool same = !creating && target == absolute;
    if (!same && fs::exists(target) && !Boolean(args, "overwrite"))
      throw std::runtime_error("Output already exists; choose a new path or set overwrite=true");
    // Validate even when the destination is new.
    Boolean(args, "overwrite");
    if (tool == "generate_animation") {
      if (!GenerateAnimation(String(args, "type"), bones,
                              Number(args, "duration", 0, 0.2f, 60),
                              Number(args, "intensity", 1, 0, 3), clip, error))
        throw std::runtime_error(error);
    } else if (tool == "create_animation") {
      clip.duration = Number(args, "duration", 3, 0.001f, 600);
      clip.fps = Number(args, "fps", 30, 1, 240);
    }
    clip.name = String(args, "name", creating ? fs::path(target).stem().string() : clip.name);
    std::vector<std::string> log;
    if (editing && !args.contains("ops")) throw std::runtime_error("ops is required");
    if (args.contains("ops") && !ApplyAnimationEdits(clip, args.at("ops"), bones, log, error))
      throw std::runtime_error(error);
    if (!SaveAnimationClip(target, clip, error)) throw std::runtime_error(error);
    json result = Summary(clip, bones);
    result["path"] = targetRelative; result["saved"] = true; result["log"] = log;
    result["note"] = "Clip saved to disk. Use set_animation to assign it, then save_scene. File edits are outside scene Undo/Redo.";
    return result;
  } catch (const std::exception &e) {
    return {{"error", e.what()}};
  }
}
} // namespace ilmeee
