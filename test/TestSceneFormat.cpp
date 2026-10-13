// .ilmeeescene v1.3–v1.8: round trip, reading
// older files, and rejecting parent links that would break the hierarchy.
//
//   cmake --build build --target TestSceneFormat && ./build/bin/TestSceneFormat

#include "../include/core_engine/IlmeeeScene.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ilmeee;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                          \
  } while (0)

// Bytes the v1.8 media fields add to an entity with empty clips:
// audio u32+str+f32+u32+f32+f32, video u32+str+u32+f32+i32.
static constexpr int kMediaBytes = 22 + 18;

static SceneEntity Entity(const char *name, int parent) {
  SceneEntity e;
  e.name = name;
  e.kind = PrimitiveKind::Cube;
  e.parent = parent;
  return e;
}

static std::vector<char> ReadAll(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

static void WriteAll(const fs::path &p, const std::vector<char> &bytes) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), (std::streamsize)bytes.size());
}

int main() {
  fs::path dir = fs::temp_directory_path() / "ilmeee-scene-format-test";
  fs::create_directories(dir);
  fs::path file = dir / "scene.ilmeeescene";

  // 1. Round trip: parents (including a child listed before its parent).
  {
    IlmeeeScene s;
    s.entities = {Entity("Root", -1), Entity("Child", 2), Entity("Mid", 0),
                  Entity("Other", -1)};
    s.entities[1].position = {1, 2, 3};
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 4);
    CHECK(in.entities[0].parent == -1);
    CHECK(in.entities[1].parent == 2);
    CHECK(in.entities[2].parent == 0);
    CHECK(in.entities[3].parent == -1);
    CHECK(in.entities[1].position == glm::vec3(1, 2, 3));
  }

  // 1b. Environment (v1.4) round-trips.
  {
    IlmeeeScene s;
    s.entities = {Entity("Root", -1)};
    s.backgroundColor = {0.1f, 0.5f, 0.9f, 1.0f};
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.backgroundColor == glm::vec4(0.1f, 0.5f, 0.9f, 1.0f));
  }

  // 1c. Canvas and UI children live in the same scene hierarchy (v1.5).
  {
    IlmeeeScene s;
    SceneEntity canvas;
    canvas.name = "Canvas";
    canvas.kind = PrimitiveKind::Canvas;
    canvas.uiWidth = 1920;
    canvas.uiHeight = 1080;
    SceneEntity button;
    button.name = "Pause";
    button.kind = PrimitiveKind::UiButton;
    button.parent = 0;
    button.uiAnchor = 1;
    button.uiText = "Pause";
    button.uiAction = "TogglePause";
    button.uiColor = {0.2f, 0.4f, 0.8f, 1.0f};
    s.entities = {canvas, button};
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 2);
    CHECK(in.entities[0].kind == PrimitiveKind::Canvas);
    CHECK(in.entities[0].uiWidth == 1920);
    CHECK(in.entities[1].parent == 0);
    CHECK(in.entities[1].kind == PrimitiveKind::UiButton);
    CHECK(in.entities[1].uiAnchor == 1);
    CHECK(in.entities[1].uiAction == "TogglePause");
    CHECK(in.entities[1].uiColor == glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
  }

  // 1d. Movement component survives a scene round trip (v1.6).
  {
    IlmeeeScene s;
    s.entities = {Entity("Mover", -1)};
    s.entities[0].movable = true;
    s.entities[0].movementScript = "WASD XZ";
    s.entities[0].movementSpeed = 7.5f;
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1);
    CHECK(in.entities[0].movable);
    CHECK(in.entities[0].movementScript == "WASD XZ");
    CHECK(in.entities[0].movementSpeed == 7.5f);
  }

  // 1e. Camera rig + joystick links and a UiJoystick survive (v1.7).
  {
    IlmeeeScene s;
    s.entities = {Entity("Player", -1), Entity("Cam", -1),
                  Entity("Canvas", -1), Entity("Stick", 2)};
    s.entities[0].movable = true;
    s.entities[0].movementScript = "Camera Relative";
    s.entities[0].movementRig.camera = 1;
    s.entities[0].movementRig.style = CameraStyle::ThirdPerson;
    s.entities[0].movementRig.distance = 6.0f;
    s.entities[0].movementRig.height = 1.25f;
    s.entities[0].movementRig.moveJoystick = "Stick";
    s.entities[0].movementRig.lookJoystick = "LookStick";
    s.entities[1].kind = PrimitiveKind::Camera;
    s.entities[2].kind = PrimitiveKind::Canvas;
    s.entities[3].kind = PrimitiveKind::UiJoystick;
    s.entities[3].uiWidth = 180.0f;
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 4);
    const MovementRig &rig = in.entities[0].movementRig;
    CHECK(rig.camera == 1);
    CHECK(rig.style == CameraStyle::ThirdPerson);
    CHECK(rig.distance == 6.0f);
    CHECK(rig.height == 1.25f);
    CHECK(rig.moveJoystick == "Stick");
    CHECK(rig.lookJoystick == "LookStick");
    CHECK(in.entities[3].kind == PrimitiveKind::UiJoystick);
    CHECK(in.entities[3].uiWidth == 180.0f);
    CHECK(in.entities[1].movementRig.camera == -1);
  }

  // 1f. An out-of-range camera link falls back to the Game view camera.
  {
    IlmeeeScene s;
    s.entities = {Entity("Player", -1)};
    s.entities[0].movementRig.camera = 9;
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities[0].movementRig.camera == -1);
  }

  // 1g. Audio Source / Video Player components and media kinds (v1.8).
  {
    IlmeeeScene s;
    s.entities = {Entity("Speaker", -1), Entity("TV", -1), Entity("Box", -1)};
    s.entities[0].kind = PrimitiveKind::AudioSource;
    s.entities[0].audio.enabled = true;
    s.entities[0].audio.clip = "assets/audio/rain.ogg";
    s.entities[0].audio.volume = 0.5f;
    s.entities[0].audio.loop = true;
    s.entities[0].audio.playOnStart = false;
    s.entities[0].audio.spatial = true;
    s.entities[0].audio.minDistance = 2.0f;
    s.entities[0].audio.maxDistance = 30.0f;
    s.entities[1].kind = PrimitiveKind::Screen;
    s.entities[1].video.enabled = true;
    s.entities[1].video.clip = "assets/video/intro.mp4";
    s.entities[1].video.loop = false;
    s.entities[1].video.unlit = false;
    s.entities[1].video.volume = 0.8f;
    s.entities[2].video.enabled = true;
    s.entities[2].video.surface = 3;
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 3);
    const AudioSourceComponent &a = in.entities[0].audio;
    CHECK(in.entities[0].kind == PrimitiveKind::AudioSource);
    CHECK(a.enabled && a.clip == "assets/audio/rain.ogg");
    CHECK(a.volume == 0.5f && a.loop && !a.playOnStart && a.spatial);
    CHECK(a.minDistance == 2.0f && a.maxDistance == 30.0f);
    const VideoPlayerComponent &v = in.entities[1].video;
    CHECK(in.entities[1].kind == PrimitiveKind::Screen);
    CHECK(v.enabled && v.clip == "assets/video/intro.mp4");
    CHECK(!v.loop && v.playOnStart && !v.unlit && v.volume == 0.8f);
    CHECK(v.surface == -1);
    CHECK(in.entities[2].video.surface == 3);
    CHECK(!in.entities[2].audio.enabled);
  }

  // 1h. A v1.7 scene (no media fields) loads with components off.
  {
    IlmeeeScene s;
    s.entities = {Entity("Old", -1)};
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 7;
    bytes[7] = 0;
    bytes.erase(bytes.end() - 16 - kMediaBytes, bytes.end() - 16);
    WriteAll(file, bytes);
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1);
    CHECK(!in.entities[0].audio.enabled && !in.entities[0].video.enabled);
  }

  // 1i. A newer minor than this code knows is refused, not misread.
  {
    IlmeeeScene s;
    s.entities = {Entity("Future", -1)};
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = (char)(kSceneFormatMinor + 1);
    bytes[7] = 0;
    WriteAll(file, bytes);
    IlmeeeScene in;
    CHECK(!LoadScene(file.string(), in));
  }

  // 2. A v1.2 file (no parent field) still loads, everything at the root.
  {
    IlmeeeScene s;
    s.entities = {Entity("Only", -1)};
    s.entities[0].name = "Only";
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 2; // minor version (little-endian u16 after the magic + major)
    bytes[7] = 0;
    // Drop v1.4 environment, v1.3 parent, v1.6 movement, v1.7 rig and v1.8
    // media.
    bytes.resize(bytes.size() - 16 - 4 - 10 - 20 - kMediaBytes);
    WriteAll(file, bytes);
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1);
    CHECK(in.entities[0].name == "Only");
    CHECK(in.entities[0].parent == -1);
    CHECK(in.backgroundColor == IlmeeeScene().backgroundColor);
  }

  // 2b. A v1.5 scene without movement fields remains readable.
  {
    IlmeeeScene s;
    s.entities = {Entity("Old", -1)};
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 5;
    bytes[7] = 0;
    bytes.erase(bytes.end() - 16 - 10 - 20 - kMediaBytes, bytes.end() - 16);
    WriteAll(file, bytes);
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1);
    CHECK(!in.entities[0].movable);
    CHECK(in.entities[0].movementScript.empty());
  }

  // 2c. A v1.6 scene without rig fields keeps the default rig.
  {
    IlmeeeScene s;
    s.entities = {Entity("Mover", -1)};
    s.entities[0].movable = true;
    s.entities[0].movementScript = "WASD XZ";
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 6;
    bytes[7] = 0;
    bytes.erase(bytes.end() - 16 - 20 - kMediaBytes, bytes.end() - 16);
    WriteAll(file, bytes);
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1);
    CHECK(in.entities[0].movementScript == "WASD XZ");
    CHECK(in.entities[0].movementRig.camera == -1);
    CHECK(in.entities[0].movementRig.style == CameraStyle::None);
  }

  // 3. Broken links are demoted to roots instead of trusted.
  {
    IlmeeeScene s;
    s.entities = {Entity("Self", 0),   // points at itself
                  Entity("Out", 99),   // out of range
                  Entity("LoopA", 3),  // A -> B -> A
                  Entity("LoopB", 2),
                  Entity("Neg", -7),   // nonsense negative
                  Entity("Fine", 2)};
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities[0].parent == -1);
    CHECK(in.entities[1].parent == -1);
    // Exactly one side of the loop is cut; the result must be acyclic.
    CHECK((in.entities[2].parent == -1) != (in.entities[3].parent == -1));
    CHECK(in.entities[4].parent == -1);
    CHECK(in.entities[5].parent == 2);
  }

  // Native bindings and target identities survive save/reload and ordering.
  {
    IlmeeeScene scene;
    scene.entities = {Entity("Owner", -1), Entity("Target", -1)};
    scene.entities[0].objectId = 42;
    scene.entities[1].objectId = 84;
    NativeScriptBinding binding{"assets/scripts/Probe.cpp", "Probe", false, {}};
    binding.values = {{"Speed", ScriptValueType::Float, 7.5f, false, 0},
                      {"Target", ScriptValueType::Object, 0, false, 84},
                      {"Active", ScriptValueType::Bool, 0, true, 0},
                      {"Offset", ScriptValueType::Vec3, 0, false, 0, {1, -2, 3}}};
    scene.entities[0].scripts.push_back(binding);
    CHECK(SaveScene(file.string(), scene));
    IlmeeeScene loaded;
    CHECK(LoadScene(file.string(), loaded));
    CHECK(loaded.entities[0].objectId == 42 && loaded.entities[1].objectId == 84);
    CHECK(loaded.entities[0].scripts.size() == 1);
    const auto &script = loaded.entities[0].scripts[0];
    CHECK(script.source == binding.source && script.className == "Probe" && !script.enabled);
    CHECK(script.values[0].number == 7.5f);
    CHECK(script.values[1].object == 84);
    CHECK(script.values[2].boolean);
    CHECK(script.values[3].type == ScriptValueType::Vec3 &&
          script.values[3].vec3[0] == 1 && script.values[3].vec3[1] == -2 &&
          script.values[3].vec3[2] == 3);
    // Duplicate/legacy identities are repaired during load.
    scene.entities[1].objectId = 42;
    CHECK(SaveScene(file.string(), scene)); CHECK(LoadScene(file.string(), loaded));
    CHECK(loaded.entities[1].objectId != 42 && loaded.entities[1].objectId != 0);
  }
  fs::remove_all(dir);
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
