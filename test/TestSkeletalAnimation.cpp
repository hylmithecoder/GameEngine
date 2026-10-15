#include "core_engine/SkeletalAnimation.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <unistd.h>
using namespace ilmeee;
namespace fs = std::filesystem;
static int checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static bool Near(glm::vec3 a, glm::vec3 b) { return glm::length(a-b) < 0.0001f; }

int main() {
  const std::vector<SkeletonBone> bones = {{"Child", 1, {0,1,0}}, {"Root", -1, {0,0,0}}};
  AnimationClip clip;
  clip.duration = 2;
  std::vector<glm::mat4> matrices;
  std::vector<glm::vec3> positions;
  CHECK(EvaluateSkeleton(bones, clip, 0, matrices, positions));
  CHECK(Near(positions[0], {0,1,0}));
  CHECK(Near(SkinPosition({0,2,0}, {0,-1,-1,-1}, {1,0,0,0}, matrices), {0,2,0}));
  BoneTransform rotated;
  rotated.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0,0,1));
  SetBoneKey(clip, "Root", {0, {}});
  SetBoneKey(clip, "Root", {2, rotated});
  CHECK(EvaluateSkeleton(bones, clip, 2, matrices, positions));
  CHECK(Near(positions[0], {-1,0,0}));
  CHECK(Near(SkinPosition({0,2,0}, {0,-1,-1,-1}, {0.5f,0,0,0}, matrices), {-2,0,0}));
  CHECK(Near(SkinNormal({0,1,0}, {0,-1,-1,-1}, {1,0,0,0}, matrices), {-1,0,0}));
  CHECK(Near(SkinPosition({2,3,4}, {-1,999,-1,-1}, {1,1,0,0}, matrices), {2,3,4}));
  CHECK(Near(SkinPosition({2,3,4}, {0,1,-1,-1}, {0,0,0,0}, matrices), {2,3,4}));
  CHECK(EvaluateSkeleton(bones, clip, 1, matrices, positions));
  CHECK(Near(positions[0], {-std::sqrt(0.5f),std::sqrt(0.5f),0}));
  const std::vector<glm::mat4> scale = {glm::scale(glm::mat4(1), glm::vec3(2,1,1))};
  CHECK(Near(SkinNormal(glm::normalize(glm::vec3(1,1,0)), {0,-1,-1,-1}, {1,0,0,0}, scale), glm::normalize(glm::vec3(0.5f,1,0))));
  CHECK(AnimationTime(4.5f, 2, true) == 0.5f);
  CHECK(AnimationTime(2, 2, true) == 0);
  CHECK(AnimationTime(4, 2, false) == 2);
  CHECK(AnimationTime(-1, 2, true) == 0);
  CHECK(AnimationTime(std::numeric_limits<float>::infinity(), 2, true) == 0);
  auto invalidBones = bones;
  invalidBones[1].parent = 0;
  CHECK(!EvaluateSkeleton(invalidBones, clip, 0, matrices, positions));
  CHECK(matrices.empty() && positions.empty());
  invalidBones = bones; invalidBones[0].parent = 123;
  CHECK(!EvaluateSkeleton(invalidBones, clip, 0, matrices, positions));

  // Key insertion, replacement, deletion and interpolation after edits.
  BoneTransform offset; offset.translation = {3,4,5};
  SetBoneKey(clip, "Root", {1, offset});
  CHECK(clip.tracks[0].keys.size() == 3);
  CHECK(Near(SampleBone(clip.tracks[0], 0.5f).translation, {1.5f,2,2.5f}));
  SetBoneKey(clip, "Root", {1, {}});
  CHECK(clip.tracks[0].keys.size() == 3);
  CHECK(Near(SampleBone(clip.tracks[0], 1).translation, {0,0,0}));
  CHECK(DeleteBoneKey(clip, "Root", 1));
  CHECK(!DeleteBoneKey(clip, "Root", 1));
  CHECK(clip.tracks[0].keys.size() == 2);
  std::string error;
  CHECK(ValidateAnimation(clip, error));
  char directory[] = "/tmp/ilmeee-animation-test-XXXXXX";
  if (!mkdtemp(directory)) return 1;
  const fs::path file = fs::path(directory) / "pose.ilmeeeanim";
  CHECK(SaveAnimationClip(file.string(), clip, error));
  AnimationClip loaded;
  CHECK(LoadAnimationClip(file.string(), loaded, error));
  CHECK(loaded.duration == 2 && loaded.tracks.size() == 1);
  CHECK(loaded.tracks[0].keys.size() == 2);
  CHECK(EvaluateSkeleton(bones, loaded, 2, matrices, positions));
  CHECK(Near(positions[0], {-1,0,0}));
  // Saving again replaces a complete asset; a failed save preserves it.
  loaded.name = "Edited";
  CHECK(SaveAnimationClip(file.string(), loaded, error));
  auto bad = loaded; bad.duration = 0;
  CHECK(!SaveAnimationClip(file.string(), bad, error));
  CHECK(LoadAnimationClip(file.string(), loaded, error) && loaded.name == "Edited");
  std::ofstream(file, std::ios::trunc) << R"({"format":"IlmeeeAnimation","version":1,"name":"Normalized","duration":2,"tracks":[{"bone":"Root","keys":[{"time":0,"translation":[0,0,0],"rotation":[0,0,0,2],"scale":[1,1,1]}]}]})";
  AnimationClip normalized;
  CHECK(LoadAnimationClip(file.string(), normalized, error));
  CHECK(std::abs(normalized.tracks[0].keys[0].value.rotation.w - 1.0f) < 0.00001f);
  bad = loaded; bad.tracks.push_back(bad.tracks.front());
  CHECK(!ValidateAnimation(bad, error));
  bad = loaded; bad.tracks[0].keys[0].value.rotation = {0,0,0,0};
  CHECK(!ValidateAnimation(bad, error));
  bad = loaded; bad.tracks[0].keys[0].time = std::numeric_limits<float>::quiet_NaN();
  CHECK(!ValidateAnimation(bad, error));
  std::ofstream(file, std::ios::trunc) << "{\"format\":\"other\"}";
  CHECK(!LoadAnimationClip(file.string(), loaded, error));
  CHECK(loaded.name == "Edited");
  fs::remove(file); fs::remove(directory);

  const auto idle = MakeBreathingClip({{"上半身", -1, {0,0,0}}, {"頭", 0, {0,1,0}}}, 20);
  CHECK(idle.tracks.size() == 2 && idle.duration == 4);
  CHECK(ValidateAnimation(idle, error));
  CHECK(EvaluateSkeleton({{"上半身", -1, {0,0,0}}, {"頭", 0, {0,1,0}}}, idle, 1, matrices, positions));
  CHECK(!Near(positions[1], {0,1,0}));
  std::printf("%d/%d checks passed\n", checks-failures, checks);
  return failures ? 1 : 0;
}
