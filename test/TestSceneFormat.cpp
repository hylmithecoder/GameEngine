// .ilmeeescene v1.3 (hierarchy) / v1.4 (environment): round trip, reading
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

  // 2. A v1.2 file (no parent field) still loads, everything at the root.
  {
    IlmeeeScene s;
    s.entities = {Entity("Only", -1)};
    s.entities[0].name = "Only";
    CHECK(SaveScene(file.string(), s));
    std::vector<char> bytes = ReadAll(file);
    bytes[6] = 2; // minor version (little-endian u16 after the magic + major)
    bytes[7] = 0;
    // drop the v1.4 environment (16 bytes) and the v1.3 parent field (4)
    bytes.resize(bytes.size() - 16 - 4);
    WriteAll(file, bytes);
    IlmeeeScene in;
    CHECK(LoadScene(file.string(), in));
    CHECK(in.entities.size() == 1);
    CHECK(in.entities[0].name == "Only");
    CHECK(in.entities[0].parent == -1);
    CHECK(in.backgroundColor == IlmeeeScene().backgroundColor);
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
