#include "core_engine/AnimationTooling.hpp"
#include "core_engine/net/McpServer.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <thread>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXHttpClient.h>
#include <unistd.h>

using namespace ilmeee;
using json = nlohmann::json;
namespace fs = std::filesystem;
static int checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static std::string Read(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static bool Near(const glm::quat &a, const glm::quat &b) {
  return std::abs(glm::dot(a, b)) > 0.99999f;
}

int main() {
  char pattern[] = "/tmp/ilmeee-animation-tooling-XXXXXX";
  if (!mkdtemp(pattern)) return 1;
  const fs::path root(pattern), project = root / "project", outside = root / "outside";
  fs::create_directories(project);
  fs::create_directories(outside);
  const std::vector<SkeletonBone> bones = {
    {"センター", -1, {0,1,0}, "Center"}, {"下半身", 0, {0,1,0}, "Pelvis"},
    {"上半身", 0, {0,1.3f,0}, "Spine"}, {"上半身2", 2, {0,1.5f,0}, "Chest"},
    {"首", 3, {0,1.8f,0}, "Neck"}, {"頭", 4, {0,2,0}, "Head"},
    {"左肩", 3, {0.1f,1.6f,0}}, {"左腕", 6, {0.2f,1.6f,0}},
    {"左ひじ", 7, {0.6f,1.6f,0}}, {"左手首", 8, {0.9f,1.6f,0}},
    {"右肩", 3, {-0.1f,1.6f,0}}, {"右腕", 10, {-0.2f,1.6f,0}},
    {"右ひじ", 11, {-0.6f,1.6f,0}}, {"右手首", 12, {-0.9f,1.6f,0}},
    {"左足", 1, {0.1f,0.9f,0}}, {"左ひざ", 14, {0.1f,0.5f,0}},
    {"右足", 1, {-0.1f,0.9f,0}}, {"右ひざ", 16, {-0.1f,0.5f,0}}
  };
  std::string error, resolved;
  CHECK(HumanoidRoles(bones).at("head") == 5);
  CHECK(ResolveBone(bones, "@head", resolved, error) && resolved == "頭");
  CHECK(ResolveBone(bones, "Head", resolved, error) && resolved == "頭");
  CHECK(!ResolveBone(bones, "@missing", resolved, error));
  CHECK(BoneGroup(bones, 7) == "arm_l");
  CHECK(BoneGroup(bones, 14) == "leg_l");
  for (const auto &name : {"左腕", "mixamorig:LeftArm", "J_Bip_L_UpperArm", "Bip001 L Hand", "Arm_L"}) {
    std::string mirrored, twice;
    CHECK(MirrorBoneName(name, mirrored));
    CHECK(MirrorBoneName(mirrored, twice) && twice == name);
  }
  CHECK(!MirrorBoneName("Head", resolved));
  const auto rotation = EulerDegreesToQuat({10,20,30});
  CHECK(Near(EulerDegreesToQuat(QuatToEulerDegrees(rotation)), rotation));
  const std::vector<SkeletonBone> mixamo = {{"mixamorig:Head", -1, {0,1,0}}};
  CHECK(HumanoidRoles(mixamo).at("head") == 0);
  const std::vector<SkeletonBone> alt = {{"custom", -1, {0,1,0}, "J_Bip_C_Head"}};
  CHECK(HumanoidRoles(alt).at("head") == 0);

  AnimationClip clip;
  std::vector<std::string> log;
  CHECK(ApplyAnimationEdits(clip, json::array({
    {{"op", "set_key"}, {"bone", "@arm_l"}, {"time", 0}, {"translation", {1,2,3}}, {"rotation", {10,20,30}}, {"scale", {2,2,2}}, {"ease", "linear"}},
    {{"op", "set_key"}, {"bone", "@arm_l"}, {"time", 2}, {"rotation", {20,30,40}}},
    {{"op", "mirror"}, {"from", "left"}}
  }), bones, log, error));
  CHECK(clip.tracks.size() == 2 && log.size() == 3);
  CHECK(clip.tracks[1].bone == "右腕");
  CHECK(clip.tracks[1].keys[0].value.translation == glm::vec3(-1,2,3));
  const auto mirrored = clip.tracks[1].keys[0].value.rotation;
  CHECK(Near(mirrored, {rotation.w, rotation.x, -rotation.y, -rotation.z}));
  CHECK(clip.tracks[0].keys[1].value.scale == glm::vec3(2));
  const auto original = clip;
  const auto originalLog = log;
  CHECK(!ApplyAnimationEdits(clip, json::array({
    {{"op", "clear_track"}, {"bone", "@arm_l"}}, {{"op", "unknown"}}
  }), bones, log, error));
  CHECK(clip.tracks.size() == original.tracks.size() && log == originalLog);
  CHECK(error.find("op 2:") != std::string::npos);
  for (const json &bad : {
    json{{"op", "set_key"}, {"bone", ""}, {"time", 0}},
    json{{"op", "set_key"}, {"bone", "@head"}, {"time", 1e300}},
    json{{"op", "set_key"}, {"bone", "@head"}, {"time", 0}, {"ease", "typo"}},
    json{{"op", "set_key"}, {"bone", "@head"}, {"time", 0}, {"ease", 5}},
    json{{"op", "set_key"}, {"bone", "@head"}, {"time", 0}, {"scale", {0,1,1}}},
    json{{"op", "add_rotation"}, {"bone", "@arm_l"}, {"rotation", {1,2,3}}, {"from", 2}, {"to", 1}},
    json{{"op", "mirror"}, {"bone", 7}},
    json{{"op", "set_duration"}, {"duration", 1}}
  }) CHECK(!ApplyAnimationEdits(clip, json::array({bad}), bones, log, error));
  CHECK(clip.duration == original.duration && clip.tracks.size() == original.tracks.size());
  CHECK(ApplyAnimationEdits(clip, json::array({
    {{"op", "add_rotation"}, {"bone", "@arm_l"}, {"rotation", {5,0,0}}},
    {{"op", "scale_time"}, {"factor", 2}}, {{"op", "shift"}, {"by", -5}},
    {{"op", "set_duration"}, {"duration", 2}, {"fps", 60}}
  }), bones, log, error));
  CHECK(clip.tracks[0].keys.size() == 1 && clip.tracks[0].keys[0].time == 0);
  CHECK(clip.fps == 60 && ValidateAnimation(clip, error));
  CHECK(ApplyAnimationEdits(clip, json::array({{{"op", "delete_key"}, {"bone", "@arm_l"}, {"time", 0}}}), bones, log, error));
  CHECK(clip.tracks.size() == 1);
  CHECK(ApplyAnimationEdits(clip, json::array({{{"op", "clear_track"}, {"bone", "@arm_r"}}}), bones, log, error));
  CHECK(clip.tracks.empty());

  for (const auto &type : GeneratedAnimationTypes()) {
    AnimationClip generated;
    CHECK(GenerateAnimation(type, bones, 2, 1, generated, error));
    CHECK(generated.duration == 2 && !generated.tracks.empty());
    CHECK(ValidateAnimation(generated, error));
    std::vector<glm::mat4> world;
    CHECK(EvaluateSkeletonPose(bones, generated, 1, world));
    CHECK(world.size() == bones.size());
    for (const auto &t : generated.tracks) {
      CHECK(t.keys.front().time == 0 && t.keys.back().time == 2);
      CHECK(Near(t.keys.front().value.rotation, t.keys.back().value.rotation));
    }
    CHECK(GenerateAnimation(type, bones, 2, 0, generated, error));
    for (const auto &t : generated.tracks)
      for (const auto &k : t.keys) {
        CHECK(Near(k.value.rotation, t.keys.front().value.rotation));
        CHECK(glm::length(k.value.translation) < 0.00001f);
      }
  }
  clip.name = "Preserved";
  CHECK(!GenerateAnimation("unknown", bones, 2, 1, clip, error));
  CHECK(!GenerateAnimation("idle", {}, 2, 1, clip, error));
  CHECK(!GenerateAnimation("idle", bones, 2, std::numeric_limits<float>::infinity(), clip, error));
  CHECK(clip.name == "Preserved");

  auto call = [&](const std::string &tool, const json &args) {
    return HandleAnimationTool(tool, args, project.string(), bones);
  };
  const auto skeleton = call("get_skeleton", {{"group", "arm_l"}, {"limit", 2}});
  CHECK(skeleton["roles"]["head"]["name"] == "頭");
  CHECK(skeleton["total"] == 4 && skeleton["bones"].size() == 2 && skeleton["nextOffset"] == 2);
  CHECK(call("get_skeleton", {{"contains", "Head"}})["total"] == 1);
  CHECK(call("get_skeleton", {{"offset", -1}}).contains("error"));
  CHECK(call("get_skeleton", {{"limit", 0}}).contains("error"));
  const std::string asset = "assets/animations/Test.ilmeeeanim";
  const auto created = call("create_animation", {{"path", asset}, {"duration", 2}, {"ops", json::array({
    {{"op", "set_key"}, {"bone", "@head"}, {"time", 0}, {"ease", "linear"}},
    {{"op", "set_key"}, {"bone", "@head"}, {"time", 2}, {"translation", {0,4,0}}, {"ease", "linear"}}
  })}});
  CHECK(!created.contains("error") && created["saved"] == true);
  CHECK(created["matchedTracks"] == 1 && created["keyCount"] == 2);
  const auto before = Read(project / asset);
  CHECK(!before.empty());
  CHECK(call("create_animation", {{"path", asset}}).contains("error"));
  CHECK(call("create_animation", {{"path", "bad.vmd"}}).contains("error"));
  CHECK(call("edit_animation", {{"path", asset}, {"ops", json::array({
    {{"op", "clear_track"}, {"bone", "@head"}}, {{"op", "not_an_op"}}
  })}}).contains("error"));
  CHECK(Read(project / asset) == before);
  CHECK(call("edit_animation", {{"path", asset}}).contains("error"));
  const auto keys = call("get_animation", {{"path", asset}, {"includeKeys", true}, {"bone", "@head"}, {"limit", 1}});
  CHECK(keys["keys"].size() == 1 && keys["nextOffset"] == 1 && keys["total"] == 2);
  const auto last = call("get_animation", {{"path", asset}, {"includeKeys", true}, {"offset", 1}, {"limit", 1}});
  CHECK(last["keys"][0]["time"] == 2 && last["nextOffset"].is_null());
  CHECK(call("get_animation", {{"path", asset}, {"limit", 2.5}}).contains("error"));
  const auto sample = call("sample_animation", {{"path", asset}, {"time", 1}, {"bone", "@head"}});
  CHECK(sample["pose"].size() == 1 && sample["pose"][0]["translation"][1] == 2);
  CHECK(sample["pose"][0]["position"][1] == 4 && sample["physics"] == false);
  CHECK(call("sample_animation", {{"path", asset}, {"time", 2}, {"loop", true}, {"bone", "@head"}})["time"] == 0);
  CHECK(call("sample_animation", {{"path", asset}}).contains("error"));
  CHECK(call("sample_animation", {{"path", asset}, {"time", 1e300}}).contains("error"));
  CHECK(call("edit_animation", {{"path", asset}, {"ops", json::array()}, {"output", "copy.ilmeeeanim"}})["saved"] == true);
  CHECK(Read(project / asset) == before);
  CHECK(call("edit_animation", {{"path", asset}, {"ops", json::array()}, {"output", "copy.ilmeeeanim"}}).contains("error"));
  CHECK(call("edit_animation", {{"path", asset}, {"ops", json::array()}, {"output", "copy.ilmeeeanim"}, {"overwrite", true}})["saved"] == true);
  CHECK(call("generate_animation", {{"path", "wave.ilmeeeanim"}, {"type", "wave"}, {"duration", 2}})["saved"] == true);
  CHECK(call("generate_animation", {{"path", "default.ilmeeeanim"}, {"type", "idle"}})["duration"] == 4);
  CHECK(call("generate_animation", {{"path", "bad.ilmeeeanim"}, {"type", "wave"}, {"duration", 70}}).contains("error"));
  CHECK(!fs::exists(project / "bad.ilmeeeanim"));

  // Reject escapes through both paths and existing symlink ancestors.
  fs::create_directory_symlink(outside, project / "linked");
  fs::copy_file(project / asset, outside / "external.ilmeeeanim");
  fs::create_symlink(outside / "external.ilmeeeanim", project / "escape.ilmeeeanim");
  fs::create_symlink(outside / "external.ilmeeeanim", project / (asset + ".tmp"));
  std::string absolute, relative;
  CHECK(!ResolveAnimationAsset(project.string(), "../outside/external.ilmeeeanim", false, absolute, relative, error));
  CHECK(!ResolveAnimationAsset(project.string(), (outside / "external.ilmeeeanim").string(), false, absolute, relative, error));
  CHECK(!ResolveAnimationAsset(project.string(), "linked/new.ilmeeeanim", true, absolute, relative, error));
  CHECK(!ResolveAnimationAsset(project.string(), "escape.ilmeeeanim", true, absolute, relative, error));
  CHECK(!ResolveAnimationAsset("", asset, false, absolute, relative, error));
  CHECK(!ResolveAnimationAsset(project.string(), std::string("x\0y", 3), true, absolute, relative, error));
  CHECK(call("edit_animation", {{"path", asset}, {"name", "After"}, {"ops", json::array()}})["saved"] == true);
  CHECK(Read(outside / "external.ilmeeeanim") == before); // fixed .tmp symlink was never followed
  for (const auto &entry : fs::directory_iterator((project / asset).parent_path()))
    CHECK(entry.path().filename().string().find(".tmp.") == std::string::npos);
  fs::copy_file(fs::path(ILMEEE_TEST_MOTION_DIR) / "Breathing.vmd", project / "import.vmd");
  const auto originalVmd = Read(project / "import.vmd");
  CHECK(call("edit_animation", {{"path", "import.vmd"}, {"ops", json::array()}}).contains("error"));
  CHECK(call("edit_animation", {{"path", "import.vmd"}, {"output", "import.ilmeeeanim"}, {"ops", json::array()}})["saved"] == true);
  CHECK(Read(project / "import.vmd") == originalVmd);

  // Exercise the actual HTTP registry, editor queue and result/error envelopes.
  const int port = ix::getFreePort();
  net::McpServer server([&](const std::string &name, const json &args) { return call(name, args); });
  const bool listening = port > 0 && server.Start(port, "animation-test", error);
  CHECK(listening);
  if (!listening) {
    std::fprintf(stderr, "Cannot start local MCP test server: %s\n", error.c_str());
    fs::remove_all(root);
    return 1;
  }
  std::atomic<bool> running{true};
  std::thread editor([&] {
    while (running) { server.Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  });
  int id = 0;
  auto http = [&](const std::string &tool, json args) {
    ix::HttpClient client;
    const auto request = client.createRequest();
    request->extraHeaders["Authorization"] = "Bearer animation-test";
    request->extraHeaders["Content-Type"] = "application/json";
    request->extraHeaders["Accept"] = "application/json, text/event-stream";
    json body = {{"jsonrpc", "2.0"}, {"id", ++id}, {"method", "tools/call"},
                 {"params", {{"name", tool}, {"arguments", args}}}};
    const auto response = client.post("http://127.0.0.1:" + std::to_string(port) + "/mcp", body.dump(), request);
    CHECK(response->statusCode == 200);
    if (response->statusCode != 200) {
      std::fprintf(stderr, "HTTP %d calling %s: %s\n", response->statusCode, tool.c_str(), response->body.c_str());
      return json::object();
    }
    return json::parse(response->body).at("result");
  };
  CHECK(http("get_skeleton", {{"index", 0}})["structuredContent"]["boneCount"] == bones.size());
  CHECK(http("create_animation", {{"path", "http.ilmeeeanim"}})["isError"] == false);
  CHECK(http("generate_animation", {{"path", "http-wave.ilmeeeanim"}, {"index", 0}, {"type", "wave"}})["isError"] == false);
  CHECK(http("edit_animation", {{"path", "http.ilmeeeanim"}, {"ops", json::array({
    {{"op", "set_key"}, {"bone", "@head"}, {"time", 0}, {"rotation", {10,0,0}}}
  })}})["isError"] == false);
  CHECK(http("get_animation", {{"path", "http.ilmeeeanim"}})["structuredContent"]["keyCount"] == 1);
  CHECK(http("sample_animation", {{"path", "http.ilmeeeanim"}, {"time", 0}})["isError"] == false);
  const auto failure = http("edit_animation", {{"path", "http.ilmeeeanim"}, {"ops", json::array({{{"op", "bad"}}})}});
  CHECK(failure["isError"] == true && !failure.contains("structuredContent"));
  running = false;
  editor.join();
  server.Stop();
  fs::remove_all(root);
  std::printf("%d/%d animation tooling checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
