// PMX component order must be identical under GCC (Linux) and Clang (Android).
// The fixture uses distinct components so swapped UVs, axes and RGBA fail.
#include "../include/core_engine/PMXLoader.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <unistd.h>

namespace fs = std::filesystem;
static int checks = 0, failures = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);         \
    }                                                                          \
  } while (0)

struct Fixture {
  std::ofstream out;
  explicit Fixture(const fs::path &path) : out(path, std::ios::binary) {}
  template <typename T> void scalar(T value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
  }
  void floats(std::initializer_list<float> values) {
    for (float value : values)
      scalar(value);
  }
  void text(const std::string &value) {
    scalar<int32_t>(static_cast<int32_t>(value.size()));
    out.write(value.data(), value.size());
  }
  void material(const char *name, int8_t texture) {
    text(name);
    text(name);
    floats({0.2f, 0.4f, 0.6f, 0.8f}); // diffuse RGBA
    floats({0.1f, 0.3f, 0.5f});       // specular RGB
    scalar(8.0f);
    floats({0.15f, 0.25f, 0.35f});    // ambient RGB
    scalar<uint8_t>(0);
    floats({0.12f, 0.34f, 0.56f, 0.78f}); // edge RGBA
    scalar(1.0f);
    scalar(texture);
    scalar<int8_t>(-1);              // no sphere texture
    scalar<uint8_t>(0);
    scalar<uint8_t>(1);              // shared toon
    scalar<uint8_t>(0);
    text("");
    scalar<int32_t>(3);              // one triangle per material
  }
};

static void WriteFixture(const fs::path &path) {
  Fixture f(path);
  f.out.write("PMX ", 4);
  f.scalar(2.0f);
  f.scalar<uint8_t>(8);
  // UTF-8, no extra UVs, all indices one byte.
  for (uint8_t value : {1, 0, 1, 1, 1, 1, 1, 1})
    f.scalar(value);
  f.text("Component order");
  f.text("Component order");
  f.text("");
  f.text("");
  f.scalar<int32_t>(3);
  for (int i = 0; i < 3; ++i) {
    f.floats({1.0f + i, 2.0f + i, 3.0f + i});
    f.floats({0.0f, 0.6f, 0.8f});
    f.floats({0.125f, 0.75f});
    f.scalar<uint8_t>(i == 2 ? 3 : i); // BDEF1, BDEF2, SDEF
    f.scalar<int8_t>(0);
    if (i != 0) {
      f.scalar<int8_t>(1);
      f.scalar(0.25f);
    }
    if (i == 2) {
      f.floats({4.0f, 5.0f, 6.0f}); // SDEF C, R0, R1
      f.floats({7.0f, 8.0f, 9.0f});
      f.floats({10.0f, 11.0f, 12.0f});
    }
    f.scalar(1.0f);
  }
  f.scalar<int32_t>(6);
  for (uint8_t index : {0, 1, 2, 2, 1, 0})
    f.scalar(index);
  f.scalar<int32_t>(2);
  f.text("tex/body.png");
  f.text("tex/face.png");
  f.scalar<int32_t>(2);
  f.material("Face", 1); // texture table order differs from material order
  f.material("Shirt", 0);
  f.scalar<int32_t>(2);
  f.text("Root");
  f.text("Root");
  f.floats({1.0f, 2.0f, 3.0f});
  f.scalar<int8_t>(-1);
  f.scalar<int32_t>(0);
  f.scalar<uint16_t>(0x0c20); // fixed axis, local axes, IK
  f.floats({4.0f, 5.0f, 6.0f}); // tail offset
  f.floats({0.0f, 0.6f, 0.8f}); // fixed axis
  f.floats({1.0f, 0.0f, 0.0f}); // local X
  f.floats({0.0f, 0.0f, 1.0f}); // local Z
  f.scalar<int8_t>(1);
  f.scalar<int32_t>(8);
  f.scalar(0.5f);
  f.scalar<int32_t>(1);
  f.scalar<int8_t>(1);
  f.scalar<uint8_t>(1);
  f.floats({-0.1f, -0.2f, -0.3f});
  f.floats({0.4f, 0.5f, 0.6f});
  f.text("Child");
  f.text("Child");
  f.floats({7.0f, 8.0f, 9.0f});
  f.scalar<int8_t>(0);
  f.scalar<int32_t>(0);
  f.scalar<uint16_t>(1); // tail bone index, no optional fields
  f.scalar<int8_t>(-1);
  // Remaining PMX sections are not consumed by the current loader.
  for (int i = 0; i < 4; ++i)
    f.scalar<int32_t>(0);
}

int main() {
  char directory[] = "/tmp/ilmeee-pmx-test-XXXXXX";
  if (!mkdtemp(directory))
    return 1;
  const fs::path path = fs::path(directory) / "components.pmx";
  WriteFixture(path);
  pmx::PMXModel model;
  const bool loaded = pmx::LoadPMX(path.string(), model);
  CHECK(loaded);
  CHECK(model.vertices.size() == 3);
  CHECK(model.materials.size() == 2);
  CHECK(model.bones.size() == 2);
  if (loaded && model.vertices.size() == 3 && model.materials.size() == 2 &&
      model.bones.size() == 2) {
    for (int i = 0; i < 3; ++i) {
      const auto &v = model.vertices[i];
      CHECK(v.position == glm::vec3(1 + i, 2 + i, 3 + i));
      CHECK(v.normal == glm::vec3(0.0f, 0.6f, 0.8f));
      CHECK(v.uv == glm::vec2(0.125f, 0.75f));
      CHECK(v.boneIndices[0] == 0);
      CHECK(v.boneWeights[0] == (i == 0 ? 1.0f : 0.25f));
    }
    CHECK(model.vertices[2].sdefC == glm::vec3(4, 5, 6));
    CHECK(model.vertices[2].sdefR0 == glm::vec3(7, 8, 9));
    CHECK(model.vertices[2].sdefR1 == glm::vec3(10, 11, 12));
    CHECK(model.vertices[2].boneIndices[1] == 1);
    CHECK(model.vertices[2].boneWeights[1] == 0.75f);
    CHECK(model.indices == std::vector<uint32_t>({0, 1, 2, 2, 1, 0}));
    for (const auto &m : model.materials) {
      CHECK(m.diffuse == glm::vec4(0.2f, 0.4f, 0.6f, 0.8f));
      CHECK(m.specular == glm::vec3(0.1f, 0.3f, 0.5f));
      CHECK(m.ambient == glm::vec3(0.15f, 0.25f, 0.35f));
      CHECK(m.edgeColor == glm::vec4(0.12f, 0.34f, 0.56f, 0.78f));
      CHECK(m.indexCount == 3);
    }
    CHECK(model.texturePaths[model.materials[0].textureIndex] == "tex/face.png");
    CHECK(model.texturePaths[model.materials[1].textureIndex] == "tex/body.png");
    const auto &b = model.bones[0];
    CHECK(b.position == glm::vec3(1, 2, 3));
    CHECK(b.tailOffset == glm::vec3(4, 5, 6));
    CHECK(b.fixedAxis == glm::vec3(0.0f, 0.6f, 0.8f));
    CHECK(b.localAxisX == glm::vec3(1, 0, 0));
    CHECK(b.localAxisZ == glm::vec3(0, 0, 1));
    CHECK(b.ikLinks.size() == 1);
    if (b.ikLinks.size() == 1) {
      CHECK(b.ikLinks[0].lowerLimit == glm::vec3(-0.1f, -0.2f, -0.3f));
      CHECK(b.ikLinks[0].upperLimit == glm::vec3(0.4f, 0.5f, 0.6f));
    }
    CHECK(model.bones[1].position == glm::vec3(7, 8, 9));
    CHECK(model.bones[1].parentIndex == 0);
  }
  fs::remove(path);
  fs::remove(directory);
  std::printf("%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
