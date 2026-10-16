#include "../include/core_engine/IlmeeeScene.hpp"
#include "../include/core_engine/NativeScripts.hpp"
#include "../include/core_engine/ScriptWorkspace.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>
#include <tuple>

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
  float volume = 1;
  int played = 0;
  std::string uiText;
  bool uiVisible = true, clicked = false, ePressed = false;
  int dialogStarts = 0;
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
  // Target validation must never silently fall back to a host .so.
  auto unsupported = options;
  unsupported.androidAbi = "not-an-android-abi";
  const auto rejectedTarget = ilmeee::CompileNativeScript(unsupported, source);
  CHECK(!rejectedTarget.ok);
  CHECK(rejectedTarget.diagnostics.find("Unsupported Android") != std::string::npos);
  bool missingToolchain = false;
  try {
    ilmeee::AndroidScriptCompileOptions(project, project / "no-player", "arm64-v8a");
  } catch (const std::exception &e) {
    missingToolchain = std::string(e.what()).find("build-player.sh") != std::string::npos;
  }
  CHECK(missingToolchain);
  // Optional NDK integration: use metadata from the real rebuilt player.
  if (const char *player = std::getenv("ILMEEE_TEST_ANDROID_PLAYER")) {
    const auto android = ilmeee::AndroidScriptCompileOptions(project, player, "arm64-v8a");
    CHECK(android.androidAbi == "arm64-v8a" && android.androidApi == 26);
    CHECK(android.sdkInclude != options.sdkInclude);
    const auto module = ilmeee::CompileNativeScript(android, source);
    if (!module.ok) std::fprintf(stderr, "%s\n", module.diagnostics.c_str());
    CHECK(module.ok);
    unsigned char elf[20]{};
    std::ifstream(module.library, std::ios::binary).read(reinterpret_cast<char *>(elf), sizeof elf);
    CHECK(elf[0] == 0x7f && elf[1] == 'E' && elf[2] == 'L' && elf[3] == 'F');
    CHECK(elf[4] == 2 && elf[18] == 183 && elf[19] == 0); // ELF64 / AArch64
    CHECK(!runtime.Load(module, error)); // an ARM module cannot be loaded by the host
    for (const auto &[abi, machine, bits] :
         {std::tuple{"armeabi-v7a", 40, 1}, std::tuple{"x86_64", 62, 2},
          std::tuple{"x86", 3, 1}}) {
      auto other = android;
      other.androidAbi = abi;
      other.cache /= abi;
      const auto built = ilmeee::CompileNativeScript(other, source);
      if (!built.ok) std::fprintf(stderr, "%s\n", built.diagnostics.c_str());
      CHECK(built.ok);
      std::ifstream(built.library, std::ios::binary).read(reinterpret_cast<char *>(elf), sizeof elf);
      CHECK(elf[4] == bits && elf[18] == machine && elf[19] == 0);
    }
    auto badApi = android;
    badApi.androidApi = 0;
    CHECK(!ilmeee::CompileNativeScript(badApi, source).ok);
  }
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
  // Vec3 fields and typed objects.
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "Typed", error));
  Write(project / "assets/scripts/Typed.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class Typed : public Component {
public:
  Vec3 offset{0, 1, 0};
  ObjectRef<AudioGameObject> music;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Offset", offset);
    p.Field("Music", music);
  }
  void Start() override {
    gameObject.SetPosition(offset);
    if (music.IsValid() && music.Get().SetVolume(0.5f)) music.Get().Play();
    if (!gameObject.As<LightGameObject>().IsValid()) LogWarning("not a light");
  }
};
)cpp");
  Write(project / "assets/scripts/Typed.cpp", "#include \"Typed.hpp\"\n");
  const auto typed =
      ilmeee::CompileNativeScript(options, "assets/scripts/Typed.cpp");
  if (!typed.ok)
    std::fprintf(stderr, "%s\n", typed.diagnostics.c_str());
  CHECK(typed.ok);
  CHECK(runtime.Load(typed, error));
  ilmeee::NativeScriptBinding typedBinding{"assets/scripts/Typed.cpp", "Typed",
                                           true, {}};
  typedBinding.values = runtime.Properties(typedBinding);
  CHECK(typedBinding.values.size() == 2 &&
        typedBinding.values[0].type == ilmeee::ScriptValueType::Vec3 &&
        typedBinding.values[0].vec3[1] == 1);
  typedBinding.values[0].vec3[0] = 4;
  typedBinding.values[0].vec3[2] = 6;
  typedBinding.values[1].object = 84;
  world.objects[42] = {};
  world.objects[84] = {};
  world.log.clear();
  host.kinds = [](void *, uint64_t id) -> uint32_t {
    return id == 84 ? static_cast<uint32_t>(ilmeee::script::ObjectKind::Audio)
                    : 0;
  };
  host.setFloat = [](void *ctx, uint64_t id, ilmeee::script::Prop prop,
                     float value) {
    if (id != 84 || prop != ilmeee::script::Prop::AudioVolume)
      return false;
    static_cast<World *>(ctx)->volume = value;
    return true;
  };
  host.command = [](void *ctx, uint64_t id, ilmeee::script::Command command) {
    if (id != 84 || command != ilmeee::script::Command::AudioPlay)
      return false;
    ++static_cast<World *>(ctx)->played;
    return true;
  };
  std::vector<ilmeee::NativeScriptBinding> typedBindings{typedBinding};
  runtime.Start(host, {{42, &typedBindings}});
  const auto placed = world.objects[42].position;
  CHECK(placed.x == 4 && placed.y == 1 && placed.z == 6);
  CHECK(world.volume == 0.5f && world.played == 1);
  CHECK(world.log.size() == 1 && world.log.front() == "[WARNING] not a light");
  runtime.Stop();
  // String and std::vector fields.
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "Lists", error));
  Write(project / "assets/scripts/Lists.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class Lists : public Component {
public:
  std::string greeting = "hi";
  std::vector<std::string> names{"a", "b"};
  std::vector<float> speeds{1.5f};
  std::vector<bool> flags{true, false};
  std::vector<Vec3> points;
  std::vector<ObjectRef<AudioGameObject>> songs;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Greeting", greeting);
    p.Field("Names", names);
    p.Field("Speeds", speeds);
    p.Field("Flags", flags);
    p.Field("Points", points);
    p.Field("Songs", songs);
  }
  void Start() override {
    std::string line = greeting;
    for (const auto &name : names) line += "," + name;
    for (bool flag : flags) line += flag ? ",T" : ",F";
    for (const auto &point : points) line += "," + std::to_string(int(point.y));
    for (auto &song : songs) if (song.IsValid()) song.Get().Play();
    Log(line);
  }
};
)cpp");
  Write(project / "assets/scripts/Lists.cpp", "#include \"Lists.hpp\"\n");
  const auto lists =
      ilmeee::CompileNativeScript(options, "assets/scripts/Lists.cpp");
  if (!lists.ok)
    std::fprintf(stderr, "%s\n", lists.diagnostics.c_str());
  CHECK(lists.ok);
  CHECK(runtime.Load(lists, error));
  ilmeee::NativeScriptBinding listBinding{"assets/scripts/Lists.cpp", "Lists",
                                          true, {}};
  listBinding.values = runtime.Properties(listBinding);
  CHECK(listBinding.values.size() == 6);
  if (listBinding.values.size() == 6) {
    auto &values = listBinding.values;
    CHECK(values[0].type == ilmeee::ScriptValueType::String &&
          values[0].string == "hi");
    CHECK(values[1].type == ilmeee::ScriptValueType::List &&
          values[1].elementType == ilmeee::ScriptValueType::String &&
          values[1].items.size() == 2 && values[1].items[1].string == "b");
    CHECK(values[2].items.size() == 1 && values[2].items[0].number == 1.5f);
    CHECK(values[3].items.size() == 2 && values[3].items[0].boolean &&
          !values[3].items[1].boolean);
    CHECK(values[5].elementType == ilmeee::ScriptValueType::Object);
    values[0].string = "yo";
    values[1].items.resize(3, values[1].items[0]);
    values[1].items[2].string = "c";
    values[3].items.pop_back();
    values[4].items.push_back(
        {"", ilmeee::ScriptValueType::Vec3, 0, false, 0, {0, 7, 0}});
    ilmeee::ScriptValue song{"", ilmeee::ScriptValueType::Object};
    song.object = 84;
    values[5].items = {song, song};
  }
  world.log.clear();
  world.played = 0;
  std::vector<ilmeee::NativeScriptBinding> listBindings{listBinding};
  runtime.Start(host, {{42, &listBindings}});
  CHECK(world.log.size() == 1 && world.log.front() == "yo,a,b,c,T,7");
  CHECK(world.played == 2);
  runtime.Stop();
  // Coroutines: waits, nesting, WaitUntil, stopping, errors.
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "Waiter", error));
  Write(project / "assets/scripts/Waiter.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class Waiter : public Component {
public:
  bool go = false;
  CoroutineId forever = 0;
  Coroutine Inner() { Log("inner"); co_await WaitForSeconds(0.2f); Log("inner done"); }
  Coroutine Main() {
    Log("a");
    co_await WaitForNextFrame();
    Log("b");
    co_await WaitForSeconds(0.25f);
    Log("c");
    co_await Inner();
    Log("d");
    co_await WaitUntil([this] { return go; });
    Log("e");
    StopCoroutine(forever);
  }
  Coroutine Forever() { for (;;) co_await WaitForNextFrame(); }
  Coroutine Boom() { co_await WaitForNextFrame(); throw std::runtime_error("boom"); }
  void Start() override {
    StartCoroutine(Main());
    forever = StartCoroutine(Forever());
    Log(IsCoroutineRunning(forever) ? "running" : "not running");
  }
  void Update(float) override {
    if (IsKeyDown("Space")) go = true;
    if (IsKeyDown("E")) { StartCoroutine(Boom()); }
    if (go && !IsCoroutineRunning(forever)) Log("stopped");
  }
};
)cpp");
  Write(project / "assets/scripts/Waiter.cpp", "#include \"Waiter.hpp\"\n");
  const auto waiter =
      ilmeee::CompileNativeScript(options, "assets/scripts/Waiter.cpp");
  if (!waiter.ok)
    std::fprintf(stderr, "%s\n", waiter.diagnostics.c_str());
  CHECK(waiter.ok);
  CHECK(runtime.Load(waiter, error));
  static bool space = false, eKey = false;
  host.keyDown = [](void *, const char *key) {
    return (std::string(key) == "Space" && space) ||
           (std::string(key) == "E" && eKey);
  };
  world.log.clear();
  std::vector<ilmeee::NativeScriptBinding> waitBindings{
      {"assets/scripts/Waiter.cpp", "Waiter", true, {}}};
  std::vector<std::string> got;
  runtime.Start(host, {{42, &waitBindings}});
  CHECK(world.log == (std::vector<std::string>{"a", "running"}));
  runtime.Update(0.1f); // frame 1 is Start's frame: not resumed yet
  CHECK(world.log.back() == "running");
  runtime.Update(0.1f); // frame 2: WaitForNextFrame done, wait 0.25s
  CHECK(world.log.back() == "b");
  runtime.Update(0.1f);
  runtime.Update(0.1f);
  CHECK(world.log.back() == "b");
  runtime.Update(0.1f); // 0.3s >= 0.25s; Inner starts in the same step
  CHECK(world.log.size() == 5 && world.log[3] == "c" && world.log[4] == "inner");
  runtime.Update(0.1f);
  CHECK(world.log.back() == "inner");
  runtime.Update(0.1f); // the parent continues as soon as its child ends
  CHECK(world.log.size() == 7 && world.log[5] == "inner done" &&
        world.log[6] == "d");
  runtime.Update(0.1f);
  CHECK(world.log.back() == "d"); // WaitUntil still false
  space = true;
  runtime.Update(0.1f);
  CHECK(world.log.back() == "e");
  runtime.Update(0.1f);
  CHECK(world.log.back() == "stopped");
  std::string scriptError;
  runtime.log = [&](const std::string &line) { scriptError += line; };
  eKey = true;
  runtime.Update(0.1f);
  runtime.Update(0.1f); // the first Boom throws on its first resume
  CHECK(scriptError.find("[ERROR]") == 0 &&
        scriptError.find("boom") != std::string::npos);
  runtime.log = nullptr;
  world.log.clear();
  runtime.Update(0.1f); // a failed script stays stopped
  CHECK(world.log.empty());
  runtime.Stop();
  host.keyDown = nullptr;
  // API 3: UI elements, Dialog commands and IsKeyPressed.
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "Talker", error));
  Write(project / "assets/scripts/Talker.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class Talker : public Component {
public:
  ObjectRef<UiGameObject> label;
  ObjectRef<DialogGameObject> npc;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Label", label);
    p.Field("Npc", npc);
  }
  void Start() override {
    if (label.IsValid() && label.Get().SetText("Halo")) label.Get().Hide();
    if (!npc.Get().As<UiGameObject>().IsValid() && npc.IsValid())
      npc.Get().StartDialog();
  }
  void Update(float) override {
    if (IsKeyPressed("E")) Log("pressed");
    if (label.Get().WasClicked()) Log("clicked");
  }
};
)cpp");
  Write(project / "assets/scripts/Talker.cpp", "#include \"Talker.hpp\"\n");
  const auto talker =
      ilmeee::CompileNativeScript(options, "assets/scripts/Talker.cpp");
  if (!talker.ok)
    std::fprintf(stderr, "%s\n", talker.diagnostics.c_str());
  CHECK(talker.ok);
  CHECK(runtime.Load(talker, error));
  ilmeee::NativeScriptBinding talkerBinding{"assets/scripts/Talker.cpp",
                                            "Talker", true, {}};
  talkerBinding.values = runtime.Properties(talkerBinding);
  CHECK(talkerBinding.values.size() == 2);
  talkerBinding.values[0].object = 100;
  talkerBinding.values[1].object = 200;
  world.objects[100] = {};
  world.objects[200] = {};
  world.log.clear();
  host.kinds = [](void *, uint64_t id) -> uint32_t {
    using ilmeee::script::ObjectKind;
    return id == 100 ? static_cast<uint32_t>(ObjectKind::Ui)
           : id == 200 ? static_cast<uint32_t>(ObjectKind::Dialog)
                       : 0;
  };
  host.setString = [](void *ctx, uint64_t id, ilmeee::script::Prop prop,
                      const char *value) {
    if (id != 100 || prop != ilmeee::script::Prop::UiText)
      return false;
    static_cast<World *>(ctx)->uiText = value;
    return true;
  };
  host.setBool = [](void *ctx, uint64_t id, ilmeee::script::Prop prop,
                    bool value) {
    if (id != 100 || prop != ilmeee::script::Prop::UiVisible)
      return false;
    static_cast<World *>(ctx)->uiVisible = value;
    return true;
  };
  host.getBool = [](void *ctx, uint64_t id, ilmeee::script::Prop prop,
                    bool *out) {
    if (id != 100 || prop != ilmeee::script::Prop::UiClicked)
      return false;
    *out = static_cast<World *>(ctx)->clicked;
    return true;
  };
  host.command = [](void *ctx, uint64_t id, ilmeee::script::Command command) {
    if (id != 200 || command != ilmeee::script::Command::DialogStart)
      return false;
    ++static_cast<World *>(ctx)->dialogStarts;
    return true;
  };
  host.keyPressed = [](void *ctx, const char *key) {
    return std::string(key) == "E" && static_cast<World *>(ctx)->ePressed;
  };
  std::vector<ilmeee::NativeScriptBinding> talkerBindings{talkerBinding};
  runtime.Start(host, {{42, &talkerBindings}});
  CHECK(world.uiText == "Halo" && !world.uiVisible);
  CHECK(world.dialogStarts == 1);
  runtime.Update(0.1f);
  CHECK(world.log.empty());
  world.ePressed = world.clicked = true;
  runtime.Update(0.1f);
  CHECK(world.log.size() == 2 && world.log[0] == "pressed" &&
        world.log[1] == "clicked");
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
  for (int i = 0; i < 18000 && asynchronous.IsCompiling(); ++i) {
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
