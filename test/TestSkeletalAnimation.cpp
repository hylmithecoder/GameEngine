#include "core_engine/SkeletalAnimation.hpp"
#include "core_engine/AnimationController.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <unistd.h>
using namespace ilmeee;
namespace fs = std::filesystem;
static int checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static bool Near(glm::vec3 a, glm::vec3 b) { return glm::length(a-b) < 0.0001f; }

int main(int argc, char **argv) {
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
  // VMD CP932 bone names, coordinate reflection, unsorted/duplicate keys,
  // Bezier interpolation and malformed-input transactional loading.
  const fs::path vmd = fs::path(directory) / "fixture.VMD";
  std::string binary;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) binary += char((v >> (8*i)) & 255); };
  auto f32 = [&](float v) { uint32_t bits; std::memcpy(&bits, &v, 4); u32(bits); };
  binary = std::string("Vocaloid Motion Data 0002"); binary.resize(50, '\0');
  u32(3);
  auto record = [&](uint32_t frame, float z) {
    std::string name("\x8f\xe3\x94\xbc\x90\x67", 6); // 上半身 in CP932
    name.resize(15, '\0'); binary += name; u32(frame);
    f32(0); f32(0); f32(z);
    f32(0); f32(0); f32(0); f32(1);
    // Strong ease-in: x1=.0, y1=.0, x2=1, y2=0.
    for (int i = 0; i < 64; ++i) binary += char(i % 16 >= 8 && i % 16 < 12 ? 127 : 0);
  };
  record(30, 2); record(0, 0); record(30, 4);
  for (int i = 0; i < 5; ++i) u32(0);
  auto writeBinary = [&] { std::ofstream out(vmd, std::ios::binary | std::ios::trunc); out.write(binary.data(), binary.size()); };
  writeBinary();
  CHECK(IsAnimationPath(vmd.string()));
  CHECK(!IsAnimationPath("model.pmx"));
  AnimationClip imported;
  CHECK(LoadAnimationClip(vmd.string(), imported, error));
  CHECK(imported.tracks.size() == 1 && imported.tracks[0].bone == "上半身");
  CHECK(imported.tracks[0].keys.size() == 2 && imported.duration == 1);
  CHECK(Near(SampleBone(imported.tracks[0], 1).translation, {0,0,-4}));
  CHECK(std::abs(SampleBone(imported.tracks[0], 0.5f).translation.z + 0.5f) < 0.001f);
  CHECK(SaveAnimationClip(file.string(), imported, error));
  CHECK(LoadAnimationClip(file.string(), loaded, error));
  CHECK(std::abs(SampleBone(loaded.tracks[0], 0.5f).translation.z + 0.5f) < 0.001f);
  binary.pop_back(); writeBinary();
  CHECK(!LoadAnimationClip(vmd.string(), imported, error));
  CHECK(imported.tracks.size() == 1 && imported.duration == 1);
  binary.resize(54); binary[50] = char(255); binary[51] = char(255); writeBinary();
  CHECK(!LoadAnimationClip(vmd.string(), imported, error));
  binary[0] = 'X'; writeBinary();
  CHECK(!LoadAnimationClip(vmd.string(), imported, error));
  fs::remove(vmd); fs::remove(file); fs::remove(directory);
  for (const char *name : {"Breathing.vmd", "Walk.vmd", "Run.vmd"}) {
    CHECK(LoadAnimationClip((fs::path(ILMEEE_TEST_MOTION_DIR) / name).string(), imported, error));
    CHECK(ValidateAnimation(imported, error));
    CHECK(!imported.tracks.empty());
    for (const auto &track : imported.tracks) {
      const auto a = SampleBone(track, 0), b = SampleBone(track, imported.duration);
      CHECK(Near(a.translation, b.translation));
      CHECK(std::abs(glm::dot(a.rotation, b.rotation)) > 0.9999f);
    }
  }
  for (int i = 1; i < argc; ++i) {
    CHECK(LoadAnimationClip(argv[i], imported, error));
    if (!error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
    std::printf("VMD %s: %zu tracks, %.2f seconds\n", argv[i], imported.tracks.size(), imported.duration);
  }

  const auto idle = MakeBreathingClip({{"上半身", -1, {0,0,0}}, {"頭", 0, {0,1,0}}}, 20);
  CHECK(idle.tracks.size() == 2 && idle.duration == 4);
  CHECK(ValidateAnimation(idle, error));
  CHECK(EvaluateSkeleton({{"上半身", -1, {0,0,0}}, {"頭", 0, {0,1,0}}}, idle, 1, matrices, positions));
  CHECK(!Near(positions[1], {0,1,0}));
  // Locomotion crossfades, interrupted transitions, root filtering and asset I/O.
  AnimationController controller;
  controller.idle = "idle.ilmeeeanim"; controller.walk = "walk.ilmeeeanim"; controller.run = "run.ilmeeeanim";
  CHECK(ValidateController(controller, error));
  const std::vector<SkeletonBone> rig{{"センター", -1, {0,0,0}}, {"leg", 0, {0,1,0}}};
  std::array<AnimationClip, 3> clips;
  BoneTransform moving; moving.translation = {3,2,4};
  moving.rotation = glm::angleAxis(1.0f, glm::vec3(1,0,0));
  SetBoneKey(clips[1], "センター", {0, moving}); SetBoneKey(clips[1], "leg", {0, moving});
  clips[2] = clips[1];
  clips[2].tracks[1].keys[0].value.rotation = glm::angleAxis(2.0f, glm::vec3(1,0,0));
  LocomotionPlayer player;
  auto pose = player.Update(rig, controller, clips, 0, 0.1f);
  CHECK(player.State() == LocomotionState::Idle);
  pose = player.Update(rig, controller, clips, 2, 0.1f);
  CHECK(player.State() == LocomotionState::Walk && player.Blend() == 0.5f);
  CHECK(Near(pose.tracks[0].keys[0].value.translation, {0,1,0}));
  CHECK(Near(pose.tracks[1].keys[0].value.translation, {1.5f,1,2}));
  const auto interrupted = pose.tracks[1].keys[0].value;
  pose = player.Update(rig, controller, clips, 0, 0);
  CHECK(player.State() == LocomotionState::Idle);
  CHECK(Near(pose.tracks[1].keys[0].value.translation, interrupted.translation));
  pose = player.Update(rig, controller, clips, 0, 0.2f);
  CHECK(Near(pose.tracks[1].keys[0].value.translation, {0,0,0}));
  pose = player.Update(rig, controller, clips, 5, 0.2f);
  CHECK(player.State() == LocomotionState::Run);
  CHECK(std::abs(glm::dot(pose.tracks[1].keys[0].value.rotation, clips[2].tracks[1].keys[0].value.rotation)) > 0.9999f);
  player.Update(rig, controller, clips, 3.4f, 0.01f); CHECK(player.State() == LocomotionState::Run);
  player.Update(rig, controller, clips, 3.3f, 0.01f); CHECK(player.State() == LocomotionState::Walk);
  player.Reset(); CHECK(player.State() == LocomotionState::Idle && player.Speed() == 0);
  controller.inPlace = false; controller.transition = 0;
  pose = player.Update(rig, controller, clips, 2, 0.1f);
  CHECK(Near(pose.tracks[0].keys[0].value.translation, moving.translation));
  auto invalidController = controller; invalidController.runSpeed = 1;
  CHECK(!ValidateController(invalidController, error));
  invalidController = controller; invalidController.walkSpeed = std::numeric_limits<float>::quiet_NaN();
  CHECK(!ValidateController(invalidController, error));
  char controlDirectory[] = "/tmp/ilmeee-controller-test-XXXXXX";
  if (!mkdtemp(controlDirectory)) return 1;
  const fs::path controlPath = fs::path(controlDirectory) / "nested/controller.ilmeeecontroller";
  for (int i = 0; i < 3; ++i) {
    const auto path = controlPath.parent_path() / (i == 0 ? controller.idle : i == 1 ? controller.walk : controller.run);
    CHECK(SaveAnimationClip(path.string(), clips[i], error));
  }
  CHECK(SaveAnimationController(controlPath.string(), controller, error));
  AnimationController readController;
  CHECK(LoadAnimationController(controlPath.string(), readController, error));
  CHECK(readController.idle == controller.idle && !readController.inPlace);
  std::array<AnimationClip, 3> readClips;
  CHECK(LoadControllerClips(controlPath.string(), readController, readClips, error));
  CHECK(readClips[2].tracks.size() == 2);
  readController.walk.clear(); readController.run.clear();
  CHECK(LoadControllerClips(controlPath.string(), readController, readClips, error));
  CHECK(readClips[1].tracks.empty() && readClips[2].tracks.empty());
  readController.idle = "missing.ilmeeeanim";
  CHECK(!LoadControllerClips(controlPath.string(), readController, readClips, error));
  CHECK(!SaveAnimationController(controlPath.string(), invalidController, error));
  CHECK(LoadAnimationController(controlPath.string(), readController, error));
  fs::remove_all(controlDirectory);
  std::printf("%d/%d checks passed\n", checks-failures, checks);
  return failures ? 1 : 0;
}
