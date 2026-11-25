#include "../include/core_engine/MovementScript.hpp"
#include <cmath>
#include <cstdio>

static int checks = 0, failures = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
} } while (0)

static void CustomMove(ilmeee::MovementContext &ctx) {
  ctx.position.y += ctx.speed * ctx.deltaSeconds;
}

int main() {
  using namespace ilmeee;
  MovementContext ctx;
  ctx.input.forward = true;
  ctx.input.right = true;
  ctx.move = KeyboardMoveAxis(ctx.input);
  ctx.speed = 6.0f;
  ctx.deltaSeconds = 0.5f;
  CHECK(RunMovementScript("WASD XZ", ctx));
  CHECK(std::abs(ctx.position.x - 3.0f / std::sqrt(2.0f)) < 0.0001f);
  CHECK(std::abs(ctx.position.z + 3.0f / std::sqrt(2.0f)) < 0.0001f);
  CHECK(RegisterMovementScript("CustomMove", CustomMove));
  CHECK(!RegisterMovementScript("CustomMove", CustomMove));
  CHECK(RunMovementScript("CustomMove", ctx));
  CHECK(ctx.position.y == 3.0f);
  CHECK(!RunMovementScript("Missing", ctx));

  // Camera Relative: forward follows the camera basis and faces it.
  {
    MovementContext c;
    c.move = {0.0f, 1.0f};
    c.cameraForward = {1.0f, 0.0f, 0.0f}; // camera looking down +X
    c.cameraRight = {0.0f, 0.0f, 1.0f};
    c.cameraStyle = CameraStyle::ThirdPerson;
    c.speed = 2.0f;
    c.deltaSeconds = 0.5f;
    CHECK(RunMovementScript("Camera Relative", c));
    CHECK(std::abs(c.position.x - 1.0f) < 0.0001f);
    CHECK(std::abs(c.position.z) < 0.0001f);
    CHECK(std::abs(c.rotation.y - 90.0f) < 0.0001f); // +Z model turned to +X
  }

  // Platformer: jumps from the ground and lands back on it.
  {
    MovementContext c;
    c.input.jump = true;
    c.deltaSeconds = 1.0f / 60.0f;
    CHECK(RunMovementScript("Platformer", c));
    CHECK(c.position.y > 0.0f);
    CHECK(c.velocity.y > 0.0f);
    c.input.jump = false;
    for (int i = 0; i < 600; ++i) RunMovementScript("Platformer", c);
    CHECK(c.position.y == 0.0f);
    CHECK(c.velocity.y == 0.0f);
  }

  // Keyboard diagonal is normalised like an analog stick.
  {
    MovementInput in;
    in.forward = in.left = true;
    const glm::vec2 a = KeyboardMoveAxis(in);
    CHECK(std::abs(glm::length(a) - 1.0f) < 0.0001f);
    CHECK(a.x < 0.0f && a.y > 0.0f);
  }
  std::printf("%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
