#include "../../../include/core_engine/MovementScript.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace ilmeee {
namespace {
void MoveXZ(MovementContext &ctx) {
  const glm::vec3 direction(ctx.move.x, 0.0f, -ctx.move.y);
  ctx.position += direction * ctx.speed * ctx.deltaSeconds;
}

// Turns the object's yaw toward `direction` (models face +Z).
void FaceDirection(MovementContext &ctx, const glm::vec3 &direction) {
  if (direction.x * direction.x + direction.z * direction.z < 1e-6f) return;
  const float target = glm::degrees(std::atan2(direction.x, direction.z));
  float diff = std::remainder(target - ctx.rotation.y, 360.0f);
  const float maxTurn = 720.0f * ctx.deltaSeconds;
  ctx.rotation.y += std::clamp(diff, -maxTurn, maxTurn);
}

// Moves on the ground plane relative to the camera and faces the movement.
void MoveCameraRelative(MovementContext &ctx) {
  const glm::vec3 direction =
      ctx.cameraRight * ctx.move.x + ctx.cameraForward * ctx.move.y;
  ctx.position += direction * ctx.speed * ctx.deltaSeconds;
  // First person keeps the rig's yaw; everything else turns to walk.
  if (ctx.cameraStyle != CameraStyle::FirstPerson)
    FaceDirection(ctx, direction);
}

// Side movement along the camera's right axis plus a jump. The ground is the
// height the object started the play session at.
void MovePlatformer(MovementContext &ctx) {
  const glm::vec3 direction = ctx.cameraRight * ctx.move.x;
  ctx.position += direction * ctx.speed * ctx.deltaSeconds;
  FaceDirection(ctx, direction);
  const bool grounded = ctx.position.y <= ctx.groundY + 1e-4f;
  if (grounded && ctx.input.jump)
    ctx.velocity.y = std::sqrt(2.0f * 9.81f * 1.5f); // ~1.5 units high
  ctx.velocity.y -= 9.81f * 2.0f * ctx.deltaSeconds;
  ctx.position.y += ctx.velocity.y * ctx.deltaSeconds;
  if (ctx.position.y <= ctx.groundY) {
    ctx.position.y = ctx.groundY;
    ctx.velocity.y = 0.0f;
  }
}

std::map<std::string, MovementFunction> &Registry() {
  static std::map<std::string, MovementFunction> scripts{
      {"WASD XZ", MoveXZ},
      {"Camera Relative", MoveCameraRelative},
      {"Platformer", MovePlatformer}};
  return scripts;
}
} // namespace

glm::vec2 KeyboardMoveAxis(const MovementInput &input) {
  glm::vec2 axis((input.right ? 1.0f : 0.0f) - (input.left ? 1.0f : 0.0f),
                 (input.forward ? 1.0f : 0.0f) - (input.backward ? 1.0f : 0.0f));
  const float length = glm::length(axis);
  return length > 1.0f ? axis / length : axis;
}

bool RegisterMovementScript(const std::string &id, MovementFunction function) {
  if (id.empty() || !function) return false;
  return Registry().emplace(id, function).second;
}

std::vector<std::string> MovementScriptNames() {
  std::vector<std::string> names;
  for (const auto &[name, function] : Registry()) names.push_back(name);
  return names;
}

bool RunMovementScript(const std::string &id, MovementContext &context) {
  const auto it = Registry().find(id);
  if (it == Registry().end() || !std::isfinite(context.deltaSeconds) ||
      context.deltaSeconds < 0.0f || !std::isfinite(context.speed) ||
      context.speed < 0.0f) return false;
  it->second(context);
  return true;
}
} // namespace ilmeee
