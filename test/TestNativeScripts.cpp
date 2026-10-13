#include "../include/core_engine/IlmeeeScene.hpp"
#include "../include/core_engine/NativeScripts.hpp"
#include "../include/core_engine/ScriptWorkspace.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
static int checks = 0, failures = 0;
#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      ++failures;                                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
    }                                                                          \
  } while (0)
static void Write(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << text;
}
struct World {
  std::unordered_map<uint64_t, ilmeee::script::Transform> objects;
  std::vector<std::string> log;
};
int main(int argc, char **argv) {
  const auto project =
      (argc == 2 ? fs::path(argv[1]) : fs::temp_directory_path()) /
      ("ilmeee-native-test-" + std::to_string(ilmeee::NewObjectId())) /
      "Project with spaces $(literal)";
  fs::create_directories(project / "assets/scripts");
  auto options = ilmeee::DefaultScriptCompileOptions(project);
  std::string error;
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "Probe", error));
  const auto source = "assets/scripts/Probe.cpp";
  Write(project / source, R"cpp(
#include "Probe.hpp"
void Probe::DescribeProperties(ScriptProperties &p) { p.Field("Speed", speed); p.Field("Target", target); }
void Probe::Start() { Log("started"); gameObject.Translate({1, 0, 0}); Log("x=" + std::to_string(gameObject.GetPosition().x)); }
void Probe::Update(float dt) {
  gameObject.Translate({speed * dt, 0, 0});
  if (target.IsValid()) target.Get().Translate({0, dt, 0});
}
)cpp");
  CHECK(ilmeee::WriteScriptCompilationDatabase(options, error));
  CHECK(fs::exists(project / "compile_commands.json"));
  const auto compiled = ilmeee::CompileNativeScript(options, source);
  if (!compiled.ok)
    std::fprintf(stderr, "%s\n", compiled.diagnostics.c_str());
  CHECK(compiled.ok);
  ilmeee::NativeScripts runtime;
  CHECK(runtime.Load(compiled, error));
  CHECK(runtime.Ready(source));
  // Scripts made by the earlier template have no DescribeProperties override.
  Write(project / "assets/scripts/Legacy.hpp",
        "#pragma once\n#include <core_engine/core/Component.hpp>\nclass Legacy "
        ": public Component { public: void Start() override; void "
        "Update(float) override; };\n");
  Write(project / "assets/scripts/Legacy.cpp",
        "#include \"Legacy.hpp\"\nvoid Legacy::Start() {}\nvoid "
        "Legacy::Update(float) {}\n");
  const auto legacy =
      ilmeee::CompileNativeScript(options, "assets/scripts/Legacy.cpp");
  CHECK(legacy.ok);
  CHECK(runtime.Load(legacy, error));
  CHECK(runtime.Properties({"assets/scripts/Legacy.cpp", "Legacy", true, {}})
            .empty());
  ilmeee::NativeScriptBinding binding{source, "Probe", true, {}};
  binding.values = runtime.Properties(binding);
  CHECK(binding.values.size() == 2);
  for (auto &value : binding.values) {
    if (value.name == "Speed")
      value.number = 4;
    if (value.name == "Target")
      value.object = 84;
  }
  World world;
  world.objects[42] = {};
  world.objects[84] = {};
  ilmeee::script::HostApi host;
  host.context = &world;
  host.valid = [](void *ctx, uint64_t id) {
    return static_cast<World *>(ctx)->objects.contains(id);
  };
  host.getTransform = [](void *ctx, uint64_t id,
                         ilmeee::script::Transform *out) {
    auto &objects = static_cast<World *>(ctx)->objects;
    if (!objects.contains(id))
      return false;
    *out = objects[id];
    return true;
  };
  host.setTransform = [](void *ctx, uint64_t id,
                         const ilmeee::script::Transform *value) {
    auto &objects = static_cast<World *>(ctx)->objects;
    if (!objects.contains(id))
      return false;
    objects[id] = *value;
    return true;
  };
  host.log = [](void *ctx, const char *line) {
    static_cast<World *>(ctx)->log.emplace_back(line);
  };
  std::vector<ilmeee::NativeScriptBinding> bindings{binding};
  const std::vector<ilmeee::ScriptOwner> owners{{42, &bindings}};
  runtime.Start(host, owners);
  CHECK(world.log.size() == 2 && world.log.front() == "started");
  CHECK(world.objects[42].position.x == 1);
  runtime.Update(0.5f); // updates are capped to prevent giant delta steps
  CHECK(world.objects[42].position.x == 2);
  CHECK(world.objects[84].position.y == 0.25f);
  runtime.Stop();
  runtime.Update(0.1f);
  CHECK(world.objects[42].position.x == 2);
  bindings.front().enabled = false;
  runtime.Start(host, owners);
  runtime.Update(0.1f);
  runtime.Stop();
  CHECK(world.objects[42].position.x == 2);
  bindings.front().enabled = true;
  runtime.Start(host, owners);
  CHECK(world.objects[42].position.x == 3);
  world.objects.erase(84); // a missing target is invalid, never another object
  runtime.Update(0.1f);
  CHECK(std::abs(world.objects[42].position.x - 3.4f) < 0.001f);
  world.objects.erase(42);
  world.objects[43] = {};
  runtime.Update(0.1f);
  CHECK(world.objects[43].position.x == 0);
  runtime.Stop();
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "InvalidClass",
                                  error));
  Write(project / "assets/scripts/InvalidClass.hpp",
        "#pragma once\nclass InvalidClass {};\n");
  Write(project / "assets/scripts/InvalidClass.cpp",
        "#include \"InvalidClass.hpp\"\n");
  const auto invalid =
      ilmeee::CompileNativeScript(options, "assets/scripts/InvalidClass.cpp");
  CHECK(!invalid.ok);
  CHECK(invalid.diagnostics.find("derive from Component") != std::string::npos);
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "InvalidApi",
                                  error));
  Write(project / "assets/scripts/InvalidApi.cpp", R"cpp(
#include "InvalidApi.hpp"
void InvalidApi::DescribeProperties(ScriptProperties &) {}
void InvalidApi::Start() { gameObject.FunctionThatDoesNotExist(); }
void InvalidApi::Update(float) {}
)cpp");
  const auto invalidApi =
      ilmeee::CompileNativeScript(options, "assets/scripts/InvalidApi.cpp");
  CHECK(!invalidApi.ok);
  CHECK(invalidApi.diagnostics.find("FunctionThatDoesNotExist") !=
        std::string::npos);
  // A failed validation must not replace the last loaded module.
  Write(project / "assets/scripts/WrongSignature.hpp",
        "#pragma once\n#include <core_engine/core/Component.hpp>\nclass "
        "WrongSignature : public Component { public: void Update(int); };\n");
  Write(
      project / "assets/scripts/WrongSignature.cpp",
      "#include \"WrongSignature.hpp\"\nvoid WrongSignature::Update(int) {}\n");
  const auto wrongSignature =
      ilmeee::CompileNativeScript(options, "assets/scripts/WrongSignature.cpp");
  CHECK(!wrongSignature.ok);
  CHECK(wrongSignature.diagnostics.find("overloaded-virtual") !=
        std::string::npos);
  Write(project / "assets/scripts/AbiMismatch.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
namespace ilmeee::script { inline constexpr auto mismatchedAbi = kComponentAbi ^ 1; }
#define kComponentAbi mismatchedAbi
class AbiMismatch : public Component {};
)cpp");
  Write(project / "assets/scripts/AbiMismatch.cpp",
        "#include \"AbiMismatch.hpp\"\n");
  const auto mismatch =
      ilmeee::CompileNativeScript(options, "assets/scripts/AbiMismatch.cpp");
  CHECK(mismatch.ok);
  CHECK(!runtime.Load(mismatch, error));
  CHECK(error.find("ABI") != std::string::npos);
  CHECK(!runtime.Load(invalidApi, error));
  CHECK(runtime.Ready(source));
  ilmeee::NativeScripts asynchronous;
  CHECK(asynchronous.Configure(options));
  for (int i = 0; i < 6000 && asynchronous.IsCompiling(); ++i) {
    asynchronous.Poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(!asynchronous.IsCompiling());
  CHECK(asynchronous.Ready(source));
  CHECK(!asynchronous.Ready("assets/scripts/InvalidClass.cpp"));
  CHECK(!asynchronous.Diagnostics("assets/scripts/InvalidApi.cpp").empty());
  // Keep a runnable fixture for the real player's renderer/host smoke test.
  ilmeee::IlmeeeScene scene;
  ilmeee::SceneEntity owner;
  owner.name = "ScriptOwner";
  owner.objectId = 42;
  owner.scripts.push_back(binding);
  scene.entities.push_back(owner);
  ilmeee::SceneEntity target;
  target.name = "Target";
  target.objectId = 84;
  target.position = {3, 0, 0};
  scene.entities.push_back(target);
  ilmeee::SceneEntity camera;
  camera.name = "Camera";
  camera.objectId = 86;
  camera.kind = ilmeee::PrimitiveKind::Camera;
  camera.position = {0, 1, 6};
  scene.entities.push_back(camera);
  fs::create_directories(project / "scenes");
  CHECK(
      ilmeee::SaveScene((project / "scenes/main.ilmeeescene").string(), scene));
  fs::create_directories(project / "lib");
  fs::copy_file(compiled.library, project / "lib/Probe.so");
  Write(
      project / "native-scripts.json",
      R"json([{"source":"assets/scripts/Probe.cpp","className":"Probe","library":"lib/Probe.so"}])json");
  Write(
      project / "game.json",
      R"json({"name":"Native Script Test","startScene":"scenes/main.ilmeeescene","window":{"width":640,"height":480,"fullscreen":false}})json");
  const auto shaders =
      fs::path(options.sdkInclude).parent_path() / "assets/shaders/vulkan";
  fs::create_directories(project / "assets/shaders/vulkan");
  fs::copy(shaders, project / "assets/shaders/vulkan",
           fs::copy_options::recursive);
  std::printf("%d/%d checks passed; project: %s\n", checks - failures, checks,
              project.string().c_str());
  return failures ? 1 : 0;
}
