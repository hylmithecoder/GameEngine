#include "../include/core_engine/IlmeeeAsset.hpp"
#include "../include/core_engine/IlmeeeScene.hpp"
#include "../include/core_engine/NativeScripts.hpp"
#include "../include/core_engine/ScriptWorkspace.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
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
  // API 6: PhysicsGameObject, ColliderGameObject and Trigger/Collision events.
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "Bouncer", error));
  Write(project / "assets/scripts/Bouncer.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class Bouncer : public Component {
public:
  ObjectRef<PhysicsGameObject> body;
  ObjectRef<ColliderGameObject> sensor;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Body", body);
    p.Field("Sensor", sensor);
  }
  void Start() override {
    if (body.IsValid() && body.Get().IsGrounded()) {
      body.Get().AddImpulse({0, 5, 0});
      body.Get().AddForce({1, 0, 0});
    }
    if (sensor.IsValid() && sensor.Get().IsTrigger()) {
      sensor.Get().SetShape(ColliderGameObject::Shape::Sphere);
      sensor.Get().SetRadius(2.5f);
      sensor.Get().FitToMesh();
    }
  }
  void OnTriggerEnter(GameObject other) override {
    Log("trig_enter:" + std::to_string(other.Id()));
  }
  void OnTriggerStay(GameObject other) override {
    Log("trig_stay:" + std::to_string(other.Id()));
  }
  void OnTriggerExit(GameObject other) override {
    Log("trig_exit:" + std::to_string(other.Id()));
  }
  void OnCollisionEnter(GameObject other) override {
    Log("col_enter:" + std::to_string(other.Id()));
  }
  void OnCollisionStay(GameObject other) override {
    Log("col_stay:" + std::to_string(other.Id()));
  }
  void OnCollisionExit(GameObject other) override {
    Log("col_exit:" + std::to_string(other.Id()));
  }
};
)cpp");
  Write(project / "assets/scripts/Bouncer.cpp", "#include \"Bouncer.hpp\"\n");
  const auto bouncer =
      ilmeee::CompileNativeScript(options, "assets/scripts/Bouncer.cpp");
  if (!bouncer.ok)
    std::fprintf(stderr, "%s\n", bouncer.diagnostics.c_str());
  CHECK(bouncer.ok);
  CHECK(runtime.Load(bouncer, error));
  ilmeee::NativeScriptBinding bouncerBinding{"assets/scripts/Bouncer.cpp",
                                             "Bouncer", true, {}};
  bouncerBinding.values = runtime.Properties(bouncerBinding);
  CHECK(bouncerBinding.values.size() == 2);
  bouncerBinding.values[0].object = 300;
  bouncerBinding.values[1].object = 400;
  world.objects[42] = {};
  world.objects[300] = {};
  world.objects[400] = {};
  world.log.clear();
  static ilmeee::script::Vec3 lastImpulse{}, lastForce{};
  static float colliderShape = 0, colliderRadius = 0;
  static int fitCalls = 0;
  host.kinds = [](void *, uint64_t id) -> uint32_t {
    using ilmeee::script::ObjectKind;
    if (id == 300)
      return static_cast<uint32_t>(ObjectKind::Physics);
    if (id == 400)
      return static_cast<uint32_t>(ObjectKind::Collider);
    return 0;
  };
  host.getBool = [](void *, uint64_t id, ilmeee::script::Prop prop, bool *out) {
    if (id == 300 && prop == ilmeee::script::Prop::PhysicsGrounded) {
      *out = true;
      return true;
    }
    if (id == 400 && prop == ilmeee::script::Prop::ColliderTrigger) {
      *out = true;
      return true;
    }
    return false;
  };
  host.setVec3 = [](void *, uint64_t id, ilmeee::script::Prop prop,
                    const ilmeee::script::Vec3 *v) {
    if (id == 300 && prop == ilmeee::script::Prop::PhysicsImpulse) {
      lastImpulse = *v;
      return true;
    }
    if (id == 300 && prop == ilmeee::script::Prop::PhysicsForce) {
      lastForce = *v;
      return true;
    }
    return false;
  };
  host.setFloat = [](void *, uint64_t id, ilmeee::script::Prop prop,
                     float value) {
    if (id == 400 && prop == ilmeee::script::Prop::ColliderShape) {
      colliderShape = value;
      return true;
    }
    if (id == 400 && prop == ilmeee::script::Prop::ColliderRadius) {
      colliderRadius = value;
      return true;
    }
    return false;
  };
  host.command = [](void *, uint64_t id, ilmeee::script::Command command) {
    if (id == 400 && command == ilmeee::script::Command::ColliderFit) {
      ++fitCalls;
      return true;
    }
    return false;
  };
  std::vector<ilmeee::NativeScriptBinding> bouncerBindings{bouncerBinding};
  runtime.Start(host, {{42, &bouncerBindings}});
  CHECK(lastImpulse.y == 5.0f && lastForce.x == 1.0f);
  CHECK(colliderShape == 1.0f && colliderRadius == 2.5f && fitCalls == 1);
  // Symmetric trigger & collision edge detection across frames.
  runtime.DispatchPhysicsEvents({{400, 42}}, {{300, 42}});
  CHECK(world.log ==
        (std::vector<std::string>{"trig_enter:400", "col_enter:300"}));
  world.log.clear();
  runtime.DispatchPhysicsEvents({{400, 42}}, {{300, 42}});
  CHECK(world.log ==
        (std::vector<std::string>{"trig_stay:400", "col_stay:300"}));
  world.log.clear();
  runtime.DispatchPhysicsEvents({}, {});
  CHECK(world.log ==
        (std::vector<std::string>{"trig_exit:400", "col_exit:300"}));
  runtime.Stop();
  // API 7: ScriptableObject data assets and AssetRef<T> fields.
  {
    Write(project / "assets/scripts/EnemyStats.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class EnemyStats : public ScriptableObject {
  ILMEEE_SCRIPTABLE_OBJECT(EnemyStats)
public:
  float maxHp = 100;
  std::string displayName = "Slime";
  std::vector<float> drops{0.5f};
  AssetRef<EnemyStats> next; // assets may point at other assets
  int loads = 0;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Max HP", maxHp);
    p.Field("Name", displayName);
    p.Field("Drops", drops);
    p.Field("Next", next);
  }
  void OnLoad() override { ++loads; Log("loaded " + displayName); }
  float Doubled() const { return maxHp * 2; }
};
)cpp");
    Write(project / "assets/scripts/EnemyStats.cpp", "#include \"EnemyStats.hpp\"\n");
    Write(project / "assets/scripts/ItemData.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class ItemData : public ScriptableObject {
  ILMEEE_SCRIPTABLE_OBJECT(ItemData)
public:
  float price = 1;
  void DescribeProperties(ScriptProperties &p) override { p.Field("Price", price); }
};
)cpp");
    Write(project / "assets/scripts/ItemData.cpp", "#include \"ItemData.hpp\"\n");
    Write(project / "assets/scripts/Enemy.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
#include "EnemyStats.hpp"
#include "ItemData.hpp"
#include <type_traits>
class Enemy : public Component {
public:
  AssetRef<EnemyStats> stats;
  AssetRef<ItemData> wrongClass;
  AssetRef<EnemyStats> escape;
  std::vector<AssetRef<EnemyStats>> pool;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Stats", stats);
    p.Field("Wrong", wrongClass);
    p.Field("Escape", escape);
    p.Field("Pool", pool);
  }
  void Start() override {
    static_assert(std::is_same_v<decltype(stats.Get()), const EnemyStats *>);
    const EnemyStats *s = stats.Get();
    if (!s) { Log("no stats"); return; }
    Log("hp=" + std::to_string(int(s->maxHp)) + " name=" + s->displayName +
        " drops=" + std::to_string(s->drops.size()) +
        " doubled=" + std::to_string(int(s->Doubled())));
    if (s->next.IsValid()) Log("next=" + s->next->displayName);
    Log(std::string("wrong=") + (wrongClass.IsValid() ? "yes" : "no"));
    Log(std::string("escape=") + (escape.IsValid() ? "yes" : "no"));
    Log("pool=" + std::to_string(pool.size()) + " same=" +
        std::to_string(!pool.empty() && pool[0].Get() == s));
    Log("loads=" + std::to_string(s->loads));
  }
};
)cpp");
    Write(project / "assets/scripts/Enemy.cpp", "#include \"Enemy.hpp\"\n");
    Write(project / "assets/scripts/Unnamed.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
class Unnamed : public ScriptableObject {};
)cpp");
    Write(project / "assets/scripts/Unnamed.cpp", "#include \"Unnamed.hpp\"\n");
    const auto statsLib = ilmeee::CompileNativeScript(options, "assets/scripts/EnemyStats.cpp");
    const auto itemLib = ilmeee::CompileNativeScript(options, "assets/scripts/ItemData.cpp");
    const auto enemyLib = ilmeee::CompileNativeScript(options, "assets/scripts/Enemy.cpp");
    for (const auto *lib : {&statsLib, &itemLib, &enemyLib})
      if (!lib->ok) std::fprintf(stderr, "%s\n", lib->diagnostics.c_str());
    CHECK(statsLib.ok && itemLib.ok && enemyLib.ok);
    const auto unnamed = ilmeee::CompileNativeScript(options, "assets/scripts/Unnamed.cpp");
    CHECK(!unnamed.ok);
    CHECK(unnamed.diagnostics.find("ILMEEE_SCRIPTABLE_OBJECT(Unnamed)") != std::string::npos);
    CHECK(runtime.Load(statsLib, error));
    CHECK(runtime.Load(itemLib, error));
    CHECK(runtime.Load(enemyLib, error));
    CHECK(runtime.IsAsset("assets/scripts/EnemyStats.cpp"));
    CHECK(!runtime.IsAsset("assets/scripts/Enemy.cpp"));
    const auto classes = runtime.AssetClasses();
    CHECK(classes.size() == 2 && classes[0] == "assets/scripts/EnemyStats.cpp");

    // Asset Inspector values come from the class defaults like a component.
    ilmeee::DataAssetFile slime{"assets/scripts/EnemyStats.cpp", "EnemyStats", {}};
    auto values = runtime.Properties({slime.source, slime.className, true, {}});
    CHECK(values.size() == 4);
    for (auto &value : values) {
      if (value.name == "Max HP") value.number = 250;
      if (value.name == "Drops") value.items.resize(3, value.items.front());
      if (value.name == "Next") {
        CHECK(value.type == ilmeee::ScriptValueType::Asset);
        CHECK(value.assetClass == "EnemyStats");
        value.string = "assets/data/boss.ilmeeeasset";
      }
    }
    slime.values = values;
    fs::create_directories(project / "assets/data");
    CHECK(ilmeee::SaveDataAsset((project / "assets/data/slime.ilmeeeasset").string(), slime));
    ilmeee::DataAssetFile boss{"assets/scripts/EnemyStats.cpp", "EnemyStats", {}};
    ilmeee::ScriptValue bossName;
    bossName.name = "Name";
    bossName.type = ilmeee::ScriptValueType::String;
    bossName.string = "King";
    boss.values.push_back(bossName); // only changed fields need storing
    CHECK(ilmeee::SaveDataAsset((project / "assets/data/boss.ilmeeeasset").string(), boss));
    ilmeee::DataAssetFile item{"assets/scripts/ItemData.cpp", "ItemData", {}};
    CHECK(ilmeee::SaveDataAsset((project / "assets/data/potion.ilmeeeasset").string(), item));
    ilmeee::DataAssetFile reloaded;
    CHECK(ilmeee::LoadDataAsset((project / "assets/data/slime.ilmeeeasset").string(), reloaded));
    CHECK(reloaded.className == "EnemyStats" && reloaded.values.size() == 4);
    CHECK(!fs::exists(project / "assets/data/slime.ilmeeeasset.tmp"));
    std::vector<std::string> referenced;
    ilmeee::CollectAssetPaths(reloaded.values, referenced);
    CHECK(referenced.size() == 1 && referenced[0] == "assets/data/boss.ilmeeeasset");
    CHECK(ilmeee::FindDataAssets(project).size() == 3);
    CHECK(ilmeee::FindDataAssets(project, "EnemyStats").size() == 2);
    Write(project / "assets/data/garbage.ilmeeeasset", "not an asset");
    CHECK(!ilmeee::LoadDataAsset((project / "assets/data/garbage.ilmeeeasset").string(), reloaded));
    CHECK(ilmeee::FindDataAssets(project).size() == 3);

    ilmeee::NativeScriptBinding enemy{"assets/scripts/Enemy.cpp", "Enemy", true, {}};
    enemy.values = runtime.Properties(enemy);
    CHECK(enemy.values.size() == 4);
    for (auto &value : enemy.values) {
      if (value.name == "Stats" || value.name == "Wrong")
        value.string = "assets/data/slime.ilmeeeasset";
      if (value.name == "Escape")
        value.string = "../outside.ilmeeeasset";
      if (value.name == "Pool") {
        CHECK(value.elementType == ilmeee::ScriptValueType::Asset);
        CHECK(value.assetClass == "EnemyStats");
        ilmeee::ScriptValue entry;
        entry.type = ilmeee::ScriptValueType::Asset;
        entry.string = "./assets/data//slime.ilmeeeasset"; // same asset, other spelling
        value.items = {entry};
      }
    }
    // A ScriptableObject bound like a component is skipped, not run.
    std::vector<ilmeee::NativeScriptBinding> enemyBindings{
        enemy, {slime.source, slime.className, true, {}}};
    world.objects[42] = {};
    world.log.clear();
    std::vector<std::string> warnings;
    runtime.log = [&](const std::string &line) { warnings.push_back(line); };
    runtime.Start(host, {{42, &enemyBindings}});
    CHECK(world.log.size() == 1 && world.log[0] == "no stats"); // no root yet
    runtime.Stop();
    world.log.clear();
    warnings.clear();
    runtime.SetAssetRoot(project);
    runtime.Start(host, {{42, &enemyBindings}});
    const auto logged = [&](const std::string &line) {
      return std::find(world.log.begin(), world.log.end(), line) != world.log.end();
    };
    CHECK(logged("loaded Slime"));
    CHECK(logged("hp=250 name=Slime drops=3 doubled=500"));
    CHECK(logged("loaded King"));
    CHECK(logged("next=King"));
    CHECK(logged("wrong=no"));
    CHECK(logged("escape=no"));
    CHECK(logged("pool=1 same=1"));
    CHECK(logged("loads=1"));
    const auto warned = [&](const std::string &part) {
      return std::any_of(warnings.begin(), warnings.end(), [&](const auto &line) {
        return line.find(part) != std::string::npos;
      });
    };
    CHECK(warned("ScriptableObject, not a component"));
    CHECK(warned("is a EnemyStats, not a ItemData"));
    CHECK(warned("inside the project"));
    runtime.Stop();
    runtime.log = nullptr;
    // Asset values in a scene round-trip (v1.20).
    ilmeee::IlmeeeScene assetScene;
    ilmeee::SceneEntity holder;
    holder.name = "Holder";
    holder.scripts.push_back(enemy);
    assetScene.entities.push_back(holder);
    const auto scenePath = (project / "asset-scene.ilmeeescene").string();
    CHECK(ilmeee::SaveScene(scenePath, assetScene));
    ilmeee::IlmeeeScene loadedScene;
    CHECK(ilmeee::LoadScene(scenePath, loadedScene));
    const auto &stored = loadedScene.entities.at(0).scripts.at(0).values;
    CHECK(stored.size() == 4 && stored[0].type == ilmeee::ScriptValueType::Asset &&
          stored[0].string == "assets/data/slime.ilmeeeasset");
    CHECK(stored[3].elementType == ilmeee::ScriptValueType::Asset &&
          stored[3].items.size() == 1);
    fs::remove(scenePath);
    for (const auto *name : {"Unnamed.hpp", "Unnamed.cpp"})
      fs::remove(project / "assets/scripts" / name);
  }
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
  CHECK(ilmeee::CreateScriptFiles(project / "assets/scripts", "McpLive", error));
  const auto liveOk =
      asynchronous.CompileAndLoad("assets/scripts/McpLive.cpp");
  CHECK(liveOk.ok && asynchronous.Ready("assets/scripts/McpLive.cpp"));
  CHECK(asynchronous.Diagnostics("assets/scripts/McpLive.cpp").empty());
  const auto liveBad =
      asynchronous.CompileAndLoad("assets/scripts/InvalidApi.cpp");
  CHECK(!liveBad.ok && !asynchronous.Ready("assets/scripts/InvalidApi.cpp"));
  CHECK(asynchronous.Diagnostics("assets/scripts/InvalidApi.cpp")
            .find("FunctionThatDoesNotExist") != std::string::npos);
  // API 8: safe AssetRef ->, PrefabRef, Instantiate/Destroy lifecycle.
  {
    Write(project / "assets/scripts/Spawner.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
#include "EnemyStats.hpp"
class Spawner : public Component {
public:
  AssetRef<EnemyStats> stats; // left unassigned on purpose
  PrefabRef bullet;
  std::string role;
  bool spawned = false;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("Stats", stats);
    p.Field("Bullet", bullet);
    p.Field("Role", role);
  }
  std::string Id() const { return std::to_string(gameObject.Id()); }
  void Awake() override { Log("awake " + Id()); }
  void Start() override {
    Log("start " + Id());
    if (role == "spawner") {
      // Unassigned AssetRef: -> must not crash; one error per field.
      Log("hp=" + std::to_string(int(stats->maxHp)) + " " + stats->displayName);
      Log("hp=" + std::to_string(int(stats->maxHp)));
    }
    if (role == "child")
      Log(Instantiate(PrefabRef{}).IsValid() ? "bad" : "no prefab");
  }
  void Update(float) override {
    Log("update " + Id());
    if (role == "spawner" && !spawned) {
      spawned = true;
      Log(Instantiate(bullet, {1, 2, 3}).IsValid() ? "spawned" : "spawn failed");
    }
    if (role == "suicide")
      Log(Destroy(gameObject) ? "destroying" : "destroy failed");
  }
  void OnDisable() override { Log("disable " + Id()); }
  void OnDestroy() override { Log("destroy " + Id()); }
};
)cpp");
    Write(project / "assets/scripts/Spawner.cpp", "#include \"Spawner.hpp\"\n");
    const auto spawnerLib =
        ilmeee::CompileNativeScript(options, "assets/scripts/Spawner.cpp");
    if (!spawnerLib.ok)
      std::fprintf(stderr, "%s\n", spawnerLib.diagnostics.c_str());
    CHECK(spawnerLib.ok);
    CHECK(runtime.Load(spawnerLib, error));
    const auto makeBinding = [&](const std::string &role,
                                 const std::string &bullet) {
      ilmeee::NativeScriptBinding b{"assets/scripts/Spawner.cpp", "Spawner",
                                    true, {}};
      b.values = runtime.Properties(b);
      for (auto &value : b.values) {
        if (value.name == "Role")
          value.string = role;
        if (value.name == "Bullet") {
          CHECK(value.type == ilmeee::ScriptValueType::Asset);
          CHECK(value.assetClass == ilmeee::script::kPrefabAssetClass);
          value.string = bullet;
        }
      }
      return b;
    };
    struct SpawnWorld {
      std::vector<std::string> log;
      ilmeee::NativeScripts *runtime = nullptr;
      std::vector<ilmeee::NativeScriptBinding> childScripts;
      std::string lastPath;
      ilmeee::script::Vec3 lastPosition;
      std::set<uint64_t> alive{42, 43};
    } spawnWorld;
    spawnWorld.runtime = &runtime;
    spawnWorld.childScripts = {makeBinding("child", "")};
    ilmeee::script::HostApi spawnHost;
    spawnHost.context = &spawnWorld;
    spawnHost.valid = [](void *ctx, uint64_t id) {
      return static_cast<SpawnWorld *>(ctx)->alive.contains(id);
    };
    spawnHost.log = [](void *ctx, const char *line) {
      static_cast<SpawnWorld *>(ctx)->log.emplace_back(line);
    };
    spawnHost.instantiate = [](void *ctx, const char *path,
                               const ilmeee::script::Vec3 *position,
                               uint64_t) -> uint64_t {
      auto &w = *static_cast<SpawnWorld *>(ctx);
      w.lastPath = path;
      w.lastPosition = *position;
      w.alive.insert(500);
      w.runtime->Attach({{500, &w.childScripts}});
      return 500;
    };
    spawnHost.destroy = [](void *ctx, uint64_t id) {
      auto &w = *static_cast<SpawnWorld *>(ctx);
      w.runtime->Detach({id});
      return true;
    };
    std::vector<ilmeee::NativeScriptBinding> spawnerScripts{
        makeBinding("spawner", "assets/prefabs/Bullet.ilmeeeprefab")};
    std::vector<ilmeee::NativeScriptBinding> suicideScripts{
        makeBinding("suicide", "")};
    runtime.Start(spawnHost, {{42, &spawnerScripts}, {43, &suicideScripts}});
    const auto count = [&](const std::string &needle) {
      return std::count_if(spawnWorld.log.begin(), spawnWorld.log.end(),
                           [&](const std::string &line) {
                             return line.find(needle) != std::string::npos;
                           });
    };
    CHECK(count("hp=100 Slime") == 1); // default-constructed EnemyStats
    CHECK(count("hp=100") == 2);
    CHECK(count("AssetRef<EnemyStats> is not assigned") == 1);
    CHECK(runtime.InstanceCount() == 2);
    spawnWorld.log.clear();
    runtime.Update(0.1f);
    CHECK(spawnWorld.lastPath == "assets/prefabs/Bullet.ilmeeeprefab");
    CHECK(spawnWorld.lastPosition.x == 1 && spawnWorld.lastPosition.y == 2 &&
          spawnWorld.lastPosition.z == 3);
    // Spawned: Awake during the spawner's Update, Start at the end of it,
    // no Update this frame. Destroyed: OnDisable + OnDestroy right away.
    CHECK(spawnWorld.log ==
          (std::vector<std::string>{"update 42", "awake 500", "spawned",
                                    "update 43", "disable 43", "destroy 43",
                                    "destroying", "start 500",
                                    "[ERROR] Instantiate: PrefabRef is not assigned.",
                                    "no prefab"}));
    CHECK(runtime.InstanceCount() == 2);
    spawnWorld.log.clear();
    runtime.Update(0.1f);
    CHECK(spawnWorld.log ==
          (std::vector<std::string>{"update 42", "update 500"}));
    // Destroying again (or an unknown id) is harmless; Stop skips the dead.
    runtime.Detach({43, 999});
    spawnWorld.log.clear();
    runtime.Stop();
    CHECK(spawnWorld.log ==
          (std::vector<std::string>{"disable 42", "disable 500"}));
    CHECK(runtime.InstanceCount() == 0);
    // Instantiate/Destroy without host support fail softly.
    ilmeee::script::HostApi bare = spawnHost;
    bare.instantiate = nullptr;
    bare.destroy = nullptr;
    spawnWorld.log.clear();
    runtime.Start(bare, {{42, &spawnerScripts}, {43, &suicideScripts}});
    runtime.Update(0.1f);
    CHECK(count("spawn failed") == 1 && count("destroy failed") == 1);
    runtime.Stop();
  }
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
