#include "../../../include/core_engine/MovementScript.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace ilmeee {
namespace {
constexpr float kStep = 1.0f / 60.0f;
constexpr float kMouseDegreesPerPixel = 0.15f;
constexpr float kStickDegreesPerSecond = 140.0f;

bool Finite(const glm::vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Same convention as the player camera: x = pitch, y = yaw, yaw 0 = -Z.
glm::vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
  const float yaw = glm::radians(yawDeg - 90.0f);
  const float pitch = glm::radians(pitchDeg);
  return {std::cos(yaw) * std::cos(pitch), std::sin(pitch),
          std::sin(yaw) * std::cos(pitch)};
}

// The camera the Game view renders with: the first camera object.
int GameViewCamera(const SceneRenderer &renderer) {
  for (size_t i = 0; i < renderer.GetMesh3DCount(); ++i)
    if (renderer.meshes3d[i].isCamera) return (int)i;
  return -1;
}

int RigCamera(const SceneRenderer &renderer, const MovementRig &rig) {
  if (rig.camera >= 0 && (size_t)rig.camera < renderer.GetMesh3DCount() &&
      renderer.meshes3d[(size_t)rig.camera].isCamera)
    return rig.camera;
  return GameViewCamera(renderer);
}

// Parent rotation only (scale stripped), as Mesh3DLocalDirToWorld uses.
glm::mat3 ParentRotation(const SceneRenderer &renderer, size_t i) {
  const int parent = renderer.GetMesh3DParent(i);
  if (parent < 0) return glm::mat3(1.0f);
  const glm::mat4 P = renderer.GetMesh3DWorldTRS((size_t)parent);
  return glm::mat3(glm::normalize(glm::vec3(P[0])),
                   glm::normalize(glm::vec3(P[1])),
                   glm::normalize(glm::vec3(P[2])));
}

// World-space forward of a camera object, as the Game view sees it.
glm::vec3 CameraWorldForward(const SceneRenderer &renderer, size_t cam) {
  const glm::vec3 rot = renderer.GetMesh3DRotation(cam);
  return renderer.Mesh3DLocalDirToWorld(cam, ForwardFromYawPitch(rot.y, rot.x));
}

// Places a camera at a world pose whatever its parent is. The Game view
// turns the camera's local forward by its parents' rotation, so the parent
// rotation is undone on the direction before converting it to yaw/pitch.
void SetCameraWorldPose(SceneRenderer &renderer, size_t cam,
                        const glm::vec3 &position, float yaw, float pitch) {
  glm::vec3 local = position;
  glm::vec3 rotation(pitch, yaw, 0.0f);
  const int parent = renderer.GetMesh3DParent(cam);
  if (parent >= 0) {
    local = glm::vec3(glm::inverse(renderer.GetMesh3DWorldTRS((size_t)parent)) *
                      glm::vec4(position, 1.0f));
    const glm::vec3 f = glm::transpose(ParentRotation(renderer, cam)) *
                        ForwardFromYawPitch(yaw, pitch);
    rotation = {glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f))),
                glm::degrees(std::atan2(f.z, f.x)) + 90.0f, 0.0f};
  }
  if (Finite(local) && Finite(rotation))
    renderer.SetMesh3DTransform(cam, local, rotation,
                                renderer.GetMesh3DScale(cam));
}

glm::vec2 ClampLength(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1.0f ? v / length : v;
}

glm::vec2 AxisOr(const MovementInput &input, const std::string &id,
                 glm::vec2 fallback) {
  if (id.empty()) return fallback;
  const auto it = input.axes.find(id);
  return it == input.axes.end() ? fallback : ClampLength(it->second);
}
} // namespace

MovementInput ReadKeyboardMouseInput() {
  using namespace ImGui;
  MovementInput input;
  input.forward = IsKeyDown(ImGuiKey_W);
  input.backward = IsKeyDown(ImGuiKey_S);
  input.left = IsKeyDown(ImGuiKey_A);
  input.right = IsKeyDown(ImGuiKey_D);
  input.jump = IsKeyDown(ImGuiKey_Space);
  input.look.x = (IsKeyDown(ImGuiKey_RightArrow) ? 1.0f : 0.0f) -
                 (IsKeyDown(ImGuiKey_LeftArrow) ? 1.0f : 0.0f);
  input.look.y = (IsKeyDown(ImGuiKey_UpArrow) ? 1.0f : 0.0f) -
                 (IsKeyDown(ImGuiKey_DownArrow) ? 1.0f : 0.0f);
  if (IsMouseDown(ImGuiMouseButton_Right)) {
    const ImVec2 delta = GetIO().MouseDelta;
    if (std::isfinite(delta.x) && std::isfinite(delta.y))
      input.lookDelta = {delta.x, delta.y};
  }
  return input;
}

void EchoKeyboardOnJoysticks(const SceneRenderer &renderer,
                             MovementInput &input) {
  const glm::vec2 move = KeyboardMoveAxis(input);
  const glm::vec2 look = ClampLength(input.look);
  for (size_t i = 0; i < renderer.GetMesh3DCount(); ++i) {
    const auto &mesh = renderer.meshes3d[i];
    if (!mesh.movable) continue;
    if (!mesh.movementRig.moveJoystick.empty())
      input.axes[mesh.movementRig.moveJoystick] = move;
    if (!mesh.movementRig.lookJoystick.empty())
      input.axes[mesh.movementRig.lookJoystick] = look;
  }
}

void MovementSystem::Reset() {
  states_.clear();
  accumulator_ = 0.0f;
  pendingLook_ = glm::vec2(0.0f);
}

void MovementSystem::Update(SceneRenderer &renderer, const MovementInput &input,
                            float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  pendingLook_ += input.lookDelta;
  accumulator_ = std::min(accumulator_ + deltaSeconds, 0.25f);
  for (int steps = 0; accumulator_ >= kStep && steps < 5; ++steps) {
    MovementInput stepInput = input;
    stepInput.lookDelta = pendingLook_; // mouse movement is applied once
    pendingLook_ = glm::vec2(0.0f);
    Step(renderer, stepInput, kStep);
    accumulator_ -= kStep;
  }
}

void MovementSystem::Step(SceneRenderer &renderer, const MovementInput &input,
                          float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  const float dt = std::min(deltaSeconds, 1.0f / 30.0f);
  for (size_t i = 0; i < renderer.GetMesh3DCount(); ++i) {
    const auto &mesh = renderer.meshes3d[i];
    if (!mesh.movable || mesh.movementScript.empty() || mesh.isCamera)
      continue;
    const MovementRig &rig = mesh.movementRig;
    const int cam = RigCamera(renderer, rig);
    State &state = states_[i];
    if (!state.initialized) {
      state.initialized = true;
      state.groundY = mesh.userPosition.y;
      state.yaw = 180.0f - mesh.userRotation.y; // behind a +Z-facing model
      state.pitch = rig.style == CameraStyle::TopDown ? -60.0f : -15.0f;
      if (cam >= 0) {
        const glm::vec3 f = CameraWorldForward(renderer, (size_t)cam);
        state.yaw = glm::degrees(std::atan2(f.z, f.x)) + 90.0f;
        state.pitch = glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f)));
      }
    }

    // Look: mouse drag, arrow keys or the look joystick.
    const glm::vec2 lookAxis =
        AxisOr(input, rig.lookJoystick, ClampLength(input.look));
    state.yaw += input.lookDelta.x * kMouseDegreesPerPixel +
                 lookAxis.x * kStickDegreesPerSecond * dt;
    state.pitch += -input.lookDelta.y * kMouseDegreesPerPixel +
                   lookAxis.y * kStickDegreesPerSecond * dt;
    state.yaw = std::remainder(state.yaw, 360.0f);
    switch (rig.style) {
    case CameraStyle::FirstPerson:
      state.pitch = std::clamp(state.pitch, -85.0f, 85.0f); break;
    case CameraStyle::TopDown:
      state.pitch = std::clamp(state.pitch, -89.0f, -20.0f); break;
    default:
      state.pitch = std::clamp(state.pitch, -75.0f, 40.0f); break;
    }

    MovementContext ctx;
    ctx.position = mesh.userPosition;
    ctx.rotation = mesh.userRotation;
    ctx.scale = mesh.userScale;
    ctx.input = input;
    ctx.move = AxisOr(input, rig.moveJoystick, KeyboardMoveAxis(input));
    // Scripts that only read the booleans still follow the joystick.
    ctx.input.forward = input.forward || ctx.move.y > 0.5f;
    ctx.input.backward = input.backward || ctx.move.y < -0.5f;
    ctx.input.right = input.right || ctx.move.x > 0.5f;
    ctx.input.left = input.left || ctx.move.x < -0.5f;
    ctx.cameraStyle = rig.style;
    ctx.velocity = state.velocity;
    ctx.groundY = state.groundY;
    ctx.speed = mesh.movementSpeed;
    ctx.deltaSeconds = dt;

    // Movement basis from where the camera looks (the rig's yaw when it
    // drives the camera, so input and view never disagree), flattened.
    glm::vec3 forward(0.0f, 0.0f, -1.0f);
    if (rig.style == CameraStyle::SideScroller)
      forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else if (rig.style != CameraStyle::None)
      forward = ForwardFromYawPitch(state.yaw, 0.0f);
    else if (cam >= 0) {
      const glm::vec3 f = CameraWorldForward(renderer, (size_t)cam);
      if (f.x * f.x + f.z * f.z > 1e-6f) forward = f;
      else forward = ForwardFromYawPitch(renderer.GetMesh3DRotation(cam).y, 0);
    }
    forward.y = 0.0f;
    forward = glm::normalize(forward);
    const glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat3 toLocal = glm::transpose(ParentRotation(renderer, i));
    ctx.cameraForward = toLocal * forward;
    ctx.cameraRight = toLocal * right;

    if (!RunMovementScript(mesh.movementScript, ctx)) continue;
    if (rig.style == CameraStyle::FirstPerson) // body turns with the view
      ctx.rotation.y = glm::degrees(std::atan2(forward.x, forward.z));
    if (!Finite(ctx.position) || !Finite(ctx.rotation) || !Finite(ctx.scale) ||
        !Finite(ctx.velocity))
      continue;
    renderer.SetMesh3DTransform(i, ctx.position, ctx.rotation, ctx.scale);
    state.velocity = ctx.velocity;

    // Camera rig follows the moved target.
    if (cam < 0 || rig.style == CameraStyle::None) continue;
    const glm::vec3 target = renderer.GetMesh3DWorldPosition(i);
    const glm::vec3 pivot = target + glm::vec3(0.0f, rig.height, 0.0f);
    const float distance = std::max(0.0f, rig.distance);
    glm::vec3 desired;
    float yaw = state.yaw, pitch = state.pitch;
    switch (rig.style) {
    case CameraStyle::FirstPerson:
      desired = pivot;
      break;
    case CameraStyle::SideScroller:
      yaw = 0.0f;
      pitch = 0.0f;
      desired = pivot + glm::vec3(0.0f, 0.0f, distance);
      break;
    default: // ThirdPerson, TopDown: orbit the pivot
      desired = pivot - ForwardFromYawPitch(yaw, pitch) * distance;
      break;
    }
    glm::vec3 position = desired;
    if (rig.style != CameraStyle::FirstPerson) {
      const glm::vec3 current = renderer.GetMesh3DWorldPosition((size_t)cam);
      const float follow = 1.0f - std::exp(-12.0f * dt);
      position = glm::mix(current, desired, follow);
    }
    SetCameraWorldPose(renderer, (size_t)cam, position, yaw, pitch);
  }
}

} // namespace ilmeee
