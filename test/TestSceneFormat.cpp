// .ilmeeescene v1.3–v1.13: round trip, reading
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
static constexpr int kNativeBytes = 8 + 4; // objectId + zero scripts
static constexpr int kDialogBytes = 4 + 4 + 32 + 2 + 2 + 16;
static constexpr int kAnimationBytes = 4 + 2 + 4 + 4;

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
    bytes.erase(bytes.end() - 16 - kMediaBytes - kNativeBytes - kDialogBytes - kAnimationBytes, bytes.end() - 16);
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
    bytes.resize(bytes.size() - 16 - 4 - 10 - 20 - kMediaBytes - kNativeBytes - kDialogBytes - kAnimationBytes);
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
    bytes.erase(bytes.end() - 16 - 10 - 20 - kMediaBytes - kNativeBytes - kDialogBytes - kAnimationBytes, bytes.end() - 16);
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
    bytes.erase(bytes.end() - 16 - 20 - kMediaBytes - kNativeBytes - kDialogBytes - kAnimationBytes, bytes.end() - 16);
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
  // v1.11: UI visibility and the Dialog component.
  {
    IlmeeeScene scene;
    scene.entities = {Entity("Npc", -1), Entity("Box", -1)};
    DialogComponent &d = scene.entities[0].dialog;
    d.enabled = true;
    d.box = 11;
    d.text = 22;
    d.speaker = 33;
    d.prompt = 44;
    d.lines = "Sakura: Halo!\n: angin";
    d.file = "assets/dialog/sakura.txt";
    d.trigger = DialogTrigger::ScriptOnly;
    d.range = 4.5f;
    d.charsPerSecond = 0;
    d.repeatable = false;
    d.freezePlayer = true;
    scene.entities[1].kind = PrimitiveKind::UiText;
    scene.entities[1].uiVisible = false;
    CHECK(SaveScene(file.string(), scene));
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    const DialogComponent &l = in.entities[0].dialog;
    CHECK(l.enabled && l.box == 11 && l.text == 22 && l.speaker == 33 &&
          l.prompt == 44);
    CHECK(l.lines == d.lines && l.file == d.file);
    CHECK(l.trigger == DialogTrigger::ScriptOnly && l.range == 4.5f &&
          l.charsPerSecond == 0 && !l.repeatable && l.freezePlayer);
    CHECK(in.entities[0].uiVisible && !in.entities[1].uiVisible);
    CHECK(!in.entities[1].dialog.enabled);

    // A v1.10 file (no v1.11 block) loads with defaults.
    IlmeeeScene old;
    old.entities = {Entity("Old", -1)};
    CHECK(SaveScene(file.string(), old));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 10;
    bytes[7] = 0;
    // uiVisible + dialog with empty strings: 4 + 4 + 32 + 2 + 2 + 4*4.
    constexpr int kV11Bytes = 4 + 4 + 32 + 2 + 2 + 16;
    bytes.erase(bytes.end() - 16 - kV11Bytes - kAnimationBytes, bytes.end() - 16);
    WriteAll(file, bytes);
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1 && in.entities[0].uiVisible &&
          !in.entities[0].dialog.enabled);
    CHECK(in.backgroundColor == IlmeeeScene().backgroundColor);
  }

  // v1.12: animation binding and defaults when loading a real v1.11 layout.
  {
    IlmeeeScene scene;
    scene.entities = {Entity("Animated", -1), Entity("Static", -1)};
    scene.entities[0].animation = {true, "assets/animations/idle.ilmeeeanim", 0.75f, false};
    CHECK(SaveScene(file.string(), scene));
    IlmeeeScene loaded;
    CHECK(LoadScene(file.string(), loaded));
    const auto &a = loaded.entities[0].animation;
    CHECK(a.enabled && a.clip == scene.entities[0].animation.clip);
    CHECK(a.speed == 0.75f && !a.loop);
    CHECK(!loaded.entities[1].animation.enabled);
    scene.entities = {Entity("Legacy", -1)};
    CHECK(SaveScene(file.string(), scene));
    auto bytes = ReadAll(file);
    bytes[6] = 11; bytes[7] = 0;
    bytes.erase(bytes.end() - 16 - kAnimationBytes, bytes.end() - 16);
    WriteAll(file, bytes);
    CHECK(LoadScene(file.string(), loaded));
    CHECK(!loaded.entities[0].animation.enabled && loaded.entities[0].animation.clip.empty());
    CHECK(loaded.backgroundColor == scene.backgroundColor);
  }

  // World Text is not a Canvas element; all its appearance and transform
  // fields survive, including a parent link and native script bindings.
  {
    IlmeeeScene s;
    s.entities = {Entity("NPC", -1), Entity("Name", 0), Entity("After", -1)};
    auto &e = s.entities[1];
    e.kind = PrimitiveKind::WorldText;
    e.position = {0, 2, 0};
    e.worldText = {"Wise\nNPC", 0.35f, {0.2f, 0.5f, 0.8f, 0.7f}, 2, false};
    e.scripts.push_back({"assets/scripts/Name.cpp", "Name", true, {}});
    CHECK(!IsUiElementKind(e.kind));
    CHECK(SaveScene(file.string(), s));
    IlmeeeScene loaded;
    CHECK(LoadScene(file.string(), loaded));
    CHECK(loaded.entities.size() == 3);
    const auto &back = loaded.entities[1];
    CHECK(back.kind == PrimitiveKind::WorldText && back.parent == 0);
    CHECK(back.position == e.position && back.worldText.text == e.worldText.text);
    CHECK(back.worldText.size == 0.35f && back.worldText.color == e.worldText.color);
    CHECK(back.worldText.alignment == 2 && !back.worldText.visible);
    CHECK(back.scripts.size() == 1 && back.scripts[0].className == "Name");
    CHECK(loaded.entities[2].name == "After");
    // Older players must reject, not misread WorldText data as an entity.
    auto bytes = ReadAll(file);
    bytes[6] = 12;
    WriteAll(file, bytes);
    CHECK(!LoadScene(file.string(), loaded));
  }

  // v1.12 world entities are unchanged; malformed Text fields get safe
  // defaults/clamps before the renderer consumes them.
  {
    IlmeeeScene s;
    s.entities = {Entity("Legacy", -1)};
    CHECK(SaveScene(file.string(), s));
    auto bytes = ReadAll(file);
    bytes[6] = 12;
    WriteAll(file, bytes);
    IlmeeeScene loaded;
    CHECK(LoadScene(file.string(), loaded));
    CHECK(loaded.entities[0].name == "Legacy");
    s.entities[0].kind = PrimitiveKind::WorldText;
    s.entities[0].worldText.size = -100;
    s.entities[0].worldText.color = {-2, 3, 0.5f, 4};
    s.entities[0].worldText.alignment = 99;
    CHECK(SaveScene(file.string(), s));
    CHECK(LoadScene(file.string(), loaded));
    CHECK(loaded.entities[0].worldText.size == 0.5f);
    CHECK(loaded.entities[0].worldText.color == glm::vec4(0, 1, 0.5f, 1));
    CHECK(loaded.entities[0].worldText.alignment == 1);
  }

  // Dialog script parsing and UTF-8 typing.
  {
    const auto lines = ParseDialogScript(
        "# intro\r\nSakura: Halo!\n\n  Apa kabar?  \n: Angin bertiup.\n"
        "Jam 10:30 sudah lewat\nBaris\\npanjang\nKosong: \n", "Npc");
    CHECK(lines.size() == 6);
    CHECK(lines[0].speaker == "Sakura" && lines[0].text == "Halo!");
    CHECK(lines[1].speaker == "Npc" && lines[1].text == "Apa kabar?");
    CHECK(lines[2].speaker.empty() && lines[2].text == "Angin bertiup.");
    CHECK(lines[3].speaker == "Npc" && lines[3].text == "Jam 10:30 sudah lewat");
    CHECK(lines[4].text == "Baris\npanjang");
    // "Name:" without text after it is plain text, not an empty line.
    CHECK(lines[5].speaker == "Npc" && lines[5].text == "Kosong:");
    CHECK(Utf8Length("héllo") == 5);
    CHECK(Utf8Prefix("héllo", 2) == "hé" && Utf8Prefix("héllo", 0).empty());
    CHECK(Utf8Prefix("héllo", 99) == "héllo");
  }
  fs::remove_all(dir);
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
