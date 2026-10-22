#include "core_engine/SkeletalAnimation.hpp"
#include "core_engine/AnimationController.hpp"
#include "core_engine/SpringBone.hpp"
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

  // Two-stage evaluation matches the one-shot path.
  {
    std::vector<glm::mat4> world, staged, direct;
    std::vector<glm::vec3> stagedPos, directPos;
    CHECK(EvaluateSkeletonPose(bones, loaded, 2, world));
    BuildSkinMatrices(bones, world, staged, stagedPos);
    CHECK(EvaluateSkeleton(bones, loaded, 2, direct, directPos));
    CHECK(staged.size() == direct.size() && Near(stagedPos[0], directPos[0]));
  }

  // Spring bones: a horizontal strand on a body.
  {
    const std::vector<SkeletonBone> rig = {
        {"Body", -1, {0, 0, 0}}, {"Hair", 0, {0, 0, 0}}, {"HairTip", 1, {1, 0, 0}}};
    const auto tip = [&](const std::vector<glm::mat4> &w) { return glm::vec3(w[2][3]); };
    SpringBoneComponent spring;
    SpringBoneChain chain; chain.rootBone = "Hair";
    chain.stiffness = 0; chain.gravity = 5; chain.drag = 0.5f;
    spring.chains = {chain};
    CHECK(CountSpringJoints(rig, spring) == 2); // Hair + virtual tip on HairTip
    std::vector<glm::mat4> pose, world;
    CHECK(EvaluateSkeletonPose(rig, {}, 0, pose));

    SpringBoneRuntime rt;
    world = pose;
    CHECK(!ApplySpringBones(rig, spring, rt, glm::mat4(1), 1.0f / 60, world)); // disabled
    CHECK(Near(tip(world), {1, 0, 0}));
    spring.enabled = true;
    CHECK(ApplySpringBones(rig, spring, rt, glm::mat4(1), 0, world));
    CHECK(Near(tip(world), {1, 0, 0})); // first frame rests
    for (int i = 0; i < 120; ++i) { world = pose; ApplySpringBones(rig, spring, rt, glm::mat4(1), 1.0f / 60, world); }
    CHECK(tip(world).y < -0.5f);                              // gravity droops
    CHECK(std::abs(glm::length(tip(world)) - 1) < 0.001f);    // length kept

    // A sphere under the strand stops it.
    SpringCollider collider; collider.bone = "Body";
    collider.offset = collider.tailOffset = {0, -1, 0}; collider.radius = 0.5f;
    spring.colliders = {collider};
    rt.Reset();
    for (int i = 0; i < 120; ++i) { world = pose; ApplySpringBones(rig, spring, rt, glm::mat4(1), 1.0f / 60, world); }
    CHECK(glm::length(tip(world) - glm::vec3(0, -1, 0)) >= 0.499f);

    // Stiff, no gravity, standing still: stays in the animated pose.
    spring.colliders.clear();
    spring.chains[0].stiffness = 20; spring.chains[0].gravity = 0;
    rt.Reset();
    for (int i = 0; i < 60; ++i) { world = pose; ApplySpringBones(rig, spring, rt, glm::mat4(1), 1.0f / 60, world); }
    CHECK(Near(tip(world), {1, 0, 0}));

    // Moving the object sideways makes the strand lag behind.
    glm::mat4 model(1);
    for (int i = 0; i < 6; ++i) {
      model = glm::translate(model, {0, 0, 0.1f});
      world = pose; ApplySpringBones(rig, spring, rt, model, 1.0f / 60, world);
    }
    CHECK(tip(world).z < -0.05f);
    // A teleport rests the chain instead of flinging it.
    model = glm::translate(model, {0, 0, 1000});
    world = pose; ApplySpringBones(rig, spring, rt, model, 1.0f / 60, world);
    CHECK(Near(tip(world), {1, 0, 0}));
    // Low frame rates still step (capped substeps), high ones re-apply.
    world = pose; CHECK(ApplySpringBones(rig, spring, rt, model, 0.001f, world));
    world = pose; CHECK(ApplySpringBones(rig, spring, rt, model, 0.5f, world));
    CHECK(rt.accumulator < kSpringBoneStep);
  }

  // Auto setup from PMX names.
  {
    const std::vector<SkeletonBone> rig = {
        {"センター", -1, {0, 8, 0}}, {"上半身", 0, {0, 10, 0}}, {"首", 1, {0, 15, 0}},   // 0-2
        {"頭", 2, {0, 16, 0}}, {"前髪1", 3, {0, 18, 1}}, {"前髪2", 4, {0, 17, 1.3f}},     // 3-5
        {"おっぱい調整", 1, {0, 13, 0}},                                                   // 6 hub
        {"左胸", 6, {0.6f, 13, 0.4f}}, {"左胸先", 7, {0.6f, 12.8f, 1.2f}},                 // 7-8
        {"右胸", 6, {-0.6f, 13, 0.4f}}, {"右胸先", 9, {-0.6f, 12.8f, 1.2f}},               // 9-10
        {"左胸上", 6, {0.6f, 13.5f, 0.2f}},                                                // 11 leaf
        {"下半身", 0, {0, 8, 0}}, {"スカート前", 12, {0, 7.5f, 1}},                         // 12-13
        {"スカート前2", 13, {0, 5, 1.4f}},                                                 // 14
        {"左足", 12, {0.8f, 7, 0}}, {"左ひざ", 15, {0.8f, 4, 0}},                           // 15-16
        {"右足", 12, {-0.8f, 7, 0}}, {"右ひざ", 17, {-0.8f, 4, 0}}};                        // 17-18
    const auto setup = AutoSetupSpringBones(rig);
    CHECK(setup.enabled && setup.chains.size() == 4);
    CHECK(setup.chains[0].rootBone == "前髪1" && setup.chains[0].useColliders);
    // The hub splits into one chain per breast; the tipless leaf is skipped.
    CHECK(setup.chains[1].rootBone == "左胸" && !setup.chains[1].useColliders);
    CHECK(setup.chains[2].rootBone == "右胸");
    CHECK(setup.chains[3].rootBone == "スカート前");
    CHECK(setup.colliders.size() == 4); // head, chest, two thighs
    CHECK(CountSpringJoints(rig, setup) == 8); // each chain: bone + virtual tip
    CHECK(!AutoSetupSpringBones(bones).enabled);

    // Abbreviated Japanese names fall back to the English (alt) name;
    // Chinese rigs use pinyin tokens (Fa = 发 hair, X = 胸, qun = 裙).
    const std::vector<SkeletonBone> abbreviated = {
        {"上半身", -1, {0, 10, 0}}, {"頭", 0, {0, 16, 0}},
        {"HF_1", 1, {0, 17, -1}, "Bdy_B_Hair_00"}, {"HF_2", 2, {0, 14, -1.2f}, "Bdy_B_Hair_00"},
        {"Fa_A_1", 1, {0, 17, 1}}, {"Fa_A_2", 4, {0, 15, 1.2f}},
        {"L_X_1", 0, {0.6f, 13, 0.4f}}, {"L_X_2", 6, {0.6f, 12.8f, 1.2f}},
        {"qun_0_0", 0, {0, 8, 1}}, {"qun_0_1", 8, {0, 5, 1.4f}},
        {"WT_1", 0, {0, 8, -1}, "Skirt_0_1"}, {"WT_2", 10, {0, 5, -1.4f}, "Skirt_0_2"},
        {"L_Er_1", 1, {1, 16, 0}}, {"L_Er_2", 12, {1.5f, 16, 0}}};
    const auto mixed = AutoSetupSpringBones(abbreviated);
    std::vector<std::string> roots;
    for (const auto &chain : mixed.chains) roots.push_back(chain.rootBone);
    CHECK((roots == std::vector<std::string>{"HF_1", "Fa_A_1", "L_X_1", "qun_0_0", "WT_1"}));
    CHECK(mixed.chains[2].stiffness == 25 && mixed.chains[3].stiffness == 10);
  }
  std::printf("%d/%d checks passed\n", checks-failures, checks);
  return failures ? 1 : 0;
}
