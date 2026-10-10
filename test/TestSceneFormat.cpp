// .ilmeeescene v1.3–v1.7: round trip, reading
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

  // 2. A v1.2 file (no parent field) still loads, everything at the root.
  {
    IlmeeeScene s;
    s.entities = {Entity("Only", -1)};
    s.entities[0].name = "Only";
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 2; // minor version (little-endian u16 after the magic + major)
    bytes[7] = 0;
    // Drop v1.4 environment, v1.3 parent, v1.6 movement and v1.7 rig.
    bytes.resize(bytes.size() - 16 - 4 - 10 - 20);
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
    bytes.erase(bytes.end() - 16 - 10 - 20, bytes.end() - 16);
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
    bytes.erase(bytes.end() - 16 - 20, bytes.end() - 16);
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

  fs::remove_all(dir);
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
