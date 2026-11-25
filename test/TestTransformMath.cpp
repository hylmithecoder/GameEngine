// Round-trip tests for ComposeTRS / DecomposeTRS — the math behind
// "reparent but keep the object where it is" in the scene hierarchy.
//
//   cmake --build build --target TestTransformMath && ./build/bin/TestTransformMath

#include "../include/core_engine/TransformMath.hpp"
#include <cstdio>
#include <random>

using namespace ilmeee;

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

static bool Near(const glm::mat4 &a, const glm::mat4 &b, float eps = 1e-3f) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      if (std::fabs(a[c][r] - b[c][r]) > eps)
        return false;
  return true;
}

int main() {
  std::mt19937 rng(1234);
  std::uniform_real_distribution<float> pos(-50.0f, 50.0f);
  std::uniform_real_distribution<float> rot(-180.0f, 180.0f);
  std::uniform_real_distribution<float> scl(0.2f, 4.0f);

  // 1. Any TRS decomposes back to the same matrix (angles may differ by an
  //    equivalent representation, so compare matrices, not angles).
  int roundTripFails = 0;
  for (int i = 0; i < 2000; ++i) {
    glm::vec3 p(pos(rng), pos(rng), pos(rng));
    glm::vec3 r(rot(rng), rot(rng), rot(rng));
    glm::vec3 s(scl(rng), scl(rng), scl(rng));
    glm::mat4 M = ComposeTRS(p, r, s);
    glm::vec3 p2, r2, s2;
    DecomposeTRS(M, p2, r2, s2);
    if (!Near(M, ComposeTRS(p2, r2, s2)))
      ++roundTripFails;
  }
  CHECK(roundTripFails == 0);

  // 2. Exact values come back for a plain transform.
  {
    glm::vec3 p, r, s;
    DecomposeTRS(ComposeTRS({1, 2, 3}, {10, 20, 30}, {2, 2, 2}), p, r, s);
    CHECK(glm::length(p - glm::vec3(1, 2, 3)) < 1e-4f);
    CHECK(glm::length(r - glm::vec3(10, 20, 30)) < 1e-3f);
    CHECK(glm::length(s - glm::vec3(2, 2, 2)) < 1e-4f);
  }

  // 3. Gimbal lock (y = ±90) still reproduces the matrix.
  for (float y : {90.0f, -90.0f}) {
    glm::mat4 M = ComposeTRS({0, 0, 0}, {35, y, 50}, {1, 1, 1});
    glm::vec3 p, r, s;
    DecomposeTRS(M, p, r, s);
    CHECK(Near(M, ComposeTRS(p, r, s)));
  }

  // 4. Reparent with keep-world, uniform parent scale: child world is
  //    unchanged after local = inverse(parentWorld) * childWorld.
  int reparentFails = 0;
  for (int i = 0; i < 1000; ++i) {
    float k = scl(rng);
    glm::mat4 parent = ComposeTRS({pos(rng), pos(rng), pos(rng)},
                                  {rot(rng), rot(rng), rot(rng)}, {k, k, k});
    glm::mat4 world = ComposeTRS({pos(rng), pos(rng), pos(rng)},
                                 {rot(rng), rot(rng), rot(rng)},
                                 {scl(rng), scl(rng), scl(rng)});
    glm::vec3 p, r, s;
    DecomposeTRS(glm::inverse(parent) * world, p, r, s);
    if (!Near(parent * ComposeTRS(p, r, s), world, 5e-3f))
      ++reparentFails;
  }
  CHECK(reparentFails == 0);

  // 5. Mirrored (negative scale) survives.
  {
    glm::mat4 M = ComposeTRS({0, 0, 0}, {0, 30, 0}, {-1, 1, 1});
    glm::vec3 p, r, s;
    DecomposeTRS(M, p, r, s);
    CHECK(Near(M, ComposeTRS(p, r, s)));
  }

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
