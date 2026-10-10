#pragma once

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class SceneRenderer;

namespace ilmeee {

// Input is supplied only while the Game tab has keyboard focus (or by the
// standalone player). Script callbacks do not need to depend on SDL or ImGui.
struct MovementInput {
  bool forward = false;
  bool backward = false;
  bool left = false;
  bool right = false;
  bool jump = false;
  // Keyboard look axis (arrow keys), -1..1, x = turn right, y = look up.
  glm::vec2 look{0.0f};
  // Mouse look this frame in pixels (right button drag). Consumed once.
  glm::vec2 lookDelta{0.0f};
  // On-screen joystick values keyed by UI element id, filled by DrawGameUI.
  std::unordered_map<std::string, glm::vec2> axes;
};

// How the camera referenced by a movable object follows it.
enum class CameraStyle : int {
  None = 0,         // camera is left alone; scripts still get its basis
  ThirdPerson = 1,  // orbit behind the target, look rotates the orbit
  FirstPerson = 2,  // camera at the target's eye height, object turns with it
  SideScroller = 3, // platformer: camera beside the target looking down -Z
  TopDown = 4,      // camera above the target looking down at it
};
inline constexpr const char *kCameraStyleNames =
    "None\0Third Person\0First Person\0Side Scroller\0Top Down\0";

// Links a movable object to a camera and to on-screen joysticks. Stored per
// object next to movable/movementScript/movementSpeed.
struct MovementRig {
  int camera = -1; // object index of the camera, -1 = the Game view camera
  CameraStyle style = CameraStyle::None;
  float distance = 4.0f; // camera distance from the target
  float height = 1.5f;   // pivot / eye height above the target's origin
  std::string moveJoystick; // UI joystick id driving movement ("" = none)
  std::string lookJoystick; // UI joystick id driving the camera look
};

struct MovementContext {
  glm::vec3 position{0.0f}; // parent-local position
  glm::vec3 rotation{0.0f}; // Euler degrees
  glm::vec3 scale{1.0f};
  MovementInput input;
  // Keyboard and joystick combined: x = right, y = forward, length <= 1.
  glm::vec2 move{0.0f};
  // Camera basis flattened onto the ground plane, in the object's
  // parent-local space. World -Z / +X when no camera is available.
  glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
  glm::vec3 cameraRight{1.0f, 0.0f, 0.0f};
  CameraStyle cameraStyle = CameraStyle::None;
  // Persist between ticks of one play session (reset on Play).
  glm::vec3 velocity{0.0f};
  float groundY = 0.0f;
  float speed = 3.0f;
  float deltaSeconds = 0.0f;
};

using MovementFunction = void (*)(MovementContext &);

// Register a C++ function under a stable ID. The same function must be
// linked into the editor and IlmeeePlayer for built games to run it too.
bool RegisterMovementScript(const std::string &id, MovementFunction function);
std::vector<std::string> MovementScriptNames();
bool RunMovementScript(const std::string &id, MovementContext &context);

// Called once at startup. Add project-specific registrations in
// UserMovementScripts.cpp, then rebuild both editor and player.
void RegisterUserMovementScripts();

// WASD as an analog axis (x = right, y = forward), length <= 1.
glm::vec2 KeyboardMoveAxis(const MovementInput &input);

// Keyboard (WASD, arrows, Space) and right-drag mouse look read through
// ImGui. Call only while the game view owns input.
MovementInput ReadKeyboardMouseInput();

// Shows keyboard input on the joysticks movable objects reference, so the
// on-screen stick and WASD/arrows stay in sync. Call before DrawGameUI.
void EchoKeyboardOnJoysticks(const SceneRenderer &renderer,
                             MovementInput &input);

// Runs attached movement components and camera rigs on loaded scene objects
// at a fixed 60 Hz. Shared by the editor's Game tab and IlmeeePlayer.
class MovementSystem {
public:
  // Call when a play session starts so orbit angles and jumps start fresh.
  void Reset();
  void Update(SceneRenderer &renderer, const MovementInput &input,
              float deltaSeconds);
  // One fixed step; Update calls this.
  void Step(SceneRenderer &renderer, const MovementInput &input,
            float deltaSeconds);

private:
  struct State {
    bool initialized = false;
    float yaw = 0.0f;   // camera rig yaw, degrees
    float pitch = 0.0f; // camera rig pitch, degrees
    glm::vec3 velocity{0.0f};
    float groundY = 0.0f;
  };
  std::unordered_map<size_t, State> states_;
  float accumulator_ = 0.0f;
  glm::vec2 pendingLook_{0.0f};
};

} // namespace ilmeee
