#pragma once
// Editor gizmos drawn with ImGui on top of the Scene viewport image:
//
//  - TransformGizmo: Move / Rotate / Scale handles on the selected object
//    (Move + Rotate work on world axes, Scale on the object's local axes).
//    Hold Ctrl while dragging to snap (0.25 units / 15 deg / 0.1). Escape
//    during a drag cancels it.
//  - OrientationGizmo: the X/Y/Z axis widget in the top-right corner of the
//    viewport. Clicking an axis animates the editor camera to look at the
//    scene from that side, orbiting the selected object (or a point ahead).
//
// Both are pure overlays (ImDrawList + mouse); nothing goes through Vulkan.

#include "../core_engine/SceneRenderer.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

namespace ilmeee {

struct GizmoResult {
  // The cursor is on a handle or a drag is running: the viewport must not
  // treat this mouse input as a pick / free drag.
  bool usingMouse = false;
  // A gizmo action changed the scene/camera and just ended: record history.
  bool editFinished = false;
};

struct TransformGizmoState {
  int active = -1; // handle being dragged, -1 = none
  int mesh = -1;   // mesh the drag started on
  glm::vec3 startPosition{0.0f};
  glm::vec3 startRotation{0.0f};
  glm::vec3 startScale{1.0f};
  glm::vec3 origin{0.0f};      // world pivot at drag start
  glm::vec3 axis{0.0f};        // world axis of an axis/ring drag
  glm::vec3 planeNormal{0.0f}; // plane the mouse ray is intersected with
  glm::vec3 startHit{0.0f};
  glm::vec3 prevDir{0.0f}; // rotate: last pivot→hit direction
  float startT = 0.0f;     // axis drag: hit parameter along `axis`
  float angle = 0.0f;      // rotate: accumulated radians
  ImVec2 startMouse{0.0f, 0.0f};
};

// `hovered`: the viewport image is under the cursor and free for gizmo
// input (no camera mouselook running).
GizmoResult TransformGizmo(SceneRenderer &renderer, size_t mesh,
                           SceneRenderer::EditMode mode, ImVec2 imgMin,
                           ImVec2 imgSize, bool hovered,
                           TransformGizmoState &state);

struct OrientationGizmoState {
  bool animating = false;
  float t = 0.0f;
  float fromYaw = 0.0f, fromPitch = 0.0f;
  float toYaw = 0.0f, toPitch = 0.0f;
  glm::vec3 pivot{0.0f};
  float distance = 5.0f;
  // Start position minus the orbit position at t=0; faded out over the
  // animation so the camera starts exactly where it was.
  glm::vec3 startOffset{0.0f};
};

// `cameraInput`: the user is flying the camera this frame (RMB / WASD),
// which cancels a running snap animation.
GizmoResult OrientationGizmo(SceneRenderer &renderer, int selectedMesh,
                             ImVec2 imgMin, ImVec2 imgSize, bool hovered,
                             bool cameraInput, float deltaTime,
                             OrientationGizmoState &state);

} // namespace ilmeee
