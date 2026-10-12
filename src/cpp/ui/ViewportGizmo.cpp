#include "../../../include/ui/ViewportGizmo.hpp"
#include "../../../include/core_engine/TransformMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace ilmeee {
namespace {

// Handle ids. Rotate uses only the three axes (rings); Scale uses the axes
// plus kCenter (uniform scale).
enum Handle {
  kAxisX = 0,
  kAxisY = 1,
  kAxisZ = 2,
  kPlaneYZ = 3, // plane handles: normal = axis (id - 3)
  kPlaneXZ = 4,
  kPlaneXY = 5,
  kCenter = 6,
};

constexpr float kGizmoPx = 90.0f;   // on-screen axis length
constexpr float kPickPx = 7.0f;     // hover tolerance around lines
constexpr int kRingSegments = 64;
constexpr float kMoveSnap = 0.25f;
constexpr float kRotateSnapDeg = 15.0f;
constexpr float kScaleSnap = 0.1f;

const glm::vec3 kWorldAxes[3] = {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0),
                                 glm::vec3(0, 0, 1)};

ImU32 AxisColor(int axis, int alpha = 255) {
  switch (axis) {
  case 0:
    return IM_COL32(232, 72, 72, alpha);
  case 1:
    return IM_COL32(120, 200, 60, alpha);
  default:
    return IM_COL32(70, 130, 240, alpha);
  }
}
const ImU32 kHotColor = IM_COL32(255, 220, 60, 255);

struct ViewInfo {
  glm::mat4 viewProj;
  glm::vec3 camPos;
  glm::vec3 camFwd;
  ImVec2 min, size;
};

ViewInfo MakeView(const SceneRenderer &r, ImVec2 imgMin, ImVec2 imgSize) {
  ViewInfo v;
  v.viewProj = r.GetCamera3DProj() * r.GetCamera3DView();
  v.camPos = r.camera3d.position;
  v.camFwd = r.GetCameraForward();
  v.min = imgMin;
  v.size = imgSize;
  return v;
}

// World → panel pixel, matching SceneRenderer::ScreenToRay (the displayed
// image is V-flipped, so NDC +Y is up on screen).
bool Project(const ViewInfo &v, const glm::vec3 &p, ImVec2 &out) {
  glm::vec4 c = v.viewProj * glm::vec4(p, 1.0f);
  if (c.w <= 1e-5f)
    return false;
  out.x = v.min.x + (c.x / c.w * 0.5f + 0.5f) * v.size.x;
  out.y = v.min.y + (0.5f - c.y / c.w * 0.5f) * v.size.y;
  return true;
}

float DistToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const float abx = b.x - a.x, aby = b.y - a.y;
  const float len2 = abx * abx + aby * aby;
  float t = len2 > 1e-6f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2 : 0;
  t = std::clamp(t, 0.0f, 1.0f);
  const float dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
  return std::sqrt(dx * dx + dy * dy);
}

float Dist(ImVec2 a, ImVec2 b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

bool InTriangle(ImVec2 p, ImVec2 a, ImVec2 b, ImVec2 c) {
  auto side = [](ImVec2 p, ImVec2 a, ImVec2 b) {
    return (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
  };
  const float d1 = side(p, a, b), d2 = side(p, b, c), d3 = side(p, c, a);
  const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
  const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}

bool RayPlane(const glm::vec3 &ro, const glm::vec3 &rd, const glm::vec3 &p0,
              const glm::vec3 &n, glm::vec3 &out) {
  const float denom = glm::dot(rd, n);
  if (std::fabs(denom) < 1e-6f)
    return false;
  const float t = glm::dot(p0 - ro, n) / denom;
  if (t <= 0.0f)
    return false;
  out = ro + rd * t;
  return true;
}

bool MouseRay(const SceneRenderer &r, ImVec2 imgMin, glm::vec3 &ro,
              glm::vec3 &rd) {
  const ImVec2 m = ImGui::GetIO().MousePos;
  return r.ScreenToRay(m.x - imgMin.x, m.y - imgMin.y, ro, rd);
}

float Snap(float v, float step) { return std::round(v / step) * step; }

// Rotation part of a TRS matrix (columns normalised).
glm::mat3 RotationOnly(const glm::mat4 &m) {
  glm::mat3 r(m);
  for (int i = 0; i < 3; ++i) {
    const float len = glm::length(r[i]);
    r[i] = len > 1e-8f ? r[i] / len : kWorldAxes[i];
  }
  return r;
}

// World-space direction → the parent space userPosition lives in.
glm::vec3 WorldDeltaToLocal(const SceneRenderer &r, size_t mesh,
                            const glm::vec3 &delta) {
  const int parent = r.GetMesh3DParent(mesh);
  if (parent < 0)
    return delta;
  return glm::vec3(glm::inverse(r.GetMesh3DWorldTRS((size_t)parent)) *
                   glm::vec4(delta, 0.0f));
}

// Plane through `origin` containing `axis` and facing the camera as much as
// possible — dragging along the axis then tracks the cursor well.
bool AxisDragPlane(const glm::vec3 &origin, const glm::vec3 &axis,
                   const glm::vec3 &camPos, glm::vec3 &outNormal) {
  glm::vec3 w = origin - camPos;
  glm::vec3 perp = w - axis * glm::dot(w, axis);
  if (glm::dot(perp, perp) < 1e-8f)
    return false;
  outNormal = glm::normalize(perp);
  return true;
}

// Two unit vectors spanning the plane perpendicular to `n`.
void PlaneBasis(const glm::vec3 &n, glm::vec3 &u, glm::vec3 &w) {
  const glm::vec3 ref =
      std::fabs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  u = glm::normalize(glm::cross(n, ref));
  w = glm::cross(n, u);
}

void DrawArrow(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thick) {
  dl->AddLine(a, b, col, thick);
  float dx = b.x - a.x, dy = b.y - a.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-3f)
    return;
  dx /= len;
  dy /= len;
  const ImVec2 tip(b.x + dx * 13.0f, b.y + dy * 13.0f);
  const ImVec2 l(b.x - dy * 6.0f, b.y + dx * 6.0f);
  const ImVec2 rr(b.x + dy * 6.0f, b.y - dx * 6.0f);
  dl->AddTriangleFilled(tip, l, rr, col);
}

} // namespace

GizmoResult TransformGizmo(SceneRenderer &renderer, size_t mesh,
                           SceneRenderer::EditMode mode, ImVec2 imgMin,
                           ImVec2 imgSize, bool hovered,
                           TransformGizmoState &st) {
  GizmoResult res;
  if (mesh >= renderer.GetMesh3DCount() ||
      mode == SceneRenderer::EditMode::SELECT || imgSize.x <= 0 ||
      imgSize.y <= 0) {
    st.active = -1;
    return res;
  }
  // Selection changed mid-drag (hierarchy click, undo...): drop the drag.
  if (st.active >= 0 && st.mesh != (int)mesh)
    st.active = -1;

  ImGuiIO &io = ImGui::GetIO();
  const ImVec2 mouse = io.MousePos;
  const bool isMove = mode == SceneRenderer::EditMode::MOVE;
  const bool isRotate = mode == SceneRenderer::EditMode::ROTATE;
  const bool isScale = mode == SceneRenderer::EditMode::SCALE;

  // ---- 1. Continue / finish a running drag -------------------------------
  if (st.active >= 0) {
    res.usingMouse = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      renderer.SetMesh3DTransform(mesh, st.startPosition, st.startRotation,
                                  st.startScale);
      st.active = -1;
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const auto changed = [](const glm::vec3 &a, const glm::vec3 &b) {
        return glm::any(glm::greaterThan(glm::abs(a - b), glm::vec3(1e-6f)));
      };
      res.editFinished =
          changed(renderer.GetMesh3DPosition(mesh), st.startPosition) ||
          changed(renderer.GetMesh3DRotation(mesh), st.startRotation) ||
          changed(renderer.GetMesh3DScale(mesh), st.startScale);
      st.active = -1;
    } else {
      glm::vec3 ro, rd, hit;
      const bool ctrl = io.KeyCtrl;
      const bool rayOk = MouseRay(renderer, imgMin, ro, rd);
      if (isMove && rayOk &&
          RayPlane(ro, rd, st.origin, st.planeNormal, hit)) {
        glm::vec3 delta;
        if (st.active <= kAxisZ) {
          float d = glm::dot(hit - st.origin, st.axis) - st.startT;
          if (ctrl)
            d = Snap(d, kMoveSnap);
          delta = st.axis * d;
        } else {
          delta = hit - st.startHit;
          if (ctrl)
            delta = glm::vec3(Snap(delta.x, kMoveSnap), Snap(delta.y, kMoveSnap),
                              Snap(delta.z, kMoveSnap));
        }
        renderer.SetMesh3DTransform(
            mesh, st.startPosition + WorldDeltaToLocal(renderer, mesh, delta),
            st.startRotation, st.startScale);
      } else if (isRotate && rayOk &&
                 RayPlane(ro, rd, st.origin, st.planeNormal, hit)) {
        glm::vec3 dir = hit - st.origin;
        if (glm::dot(dir, dir) > 1e-10f) {
          dir = glm::normalize(dir);
          st.angle += std::atan2(glm::dot(glm::cross(st.prevDir, dir), st.axis),
                                 glm::dot(st.prevDir, dir));
          st.prevDir = dir;
        }
        const float applied =
            ctrl ? Snap(st.angle, glm::radians(kRotateSnapDeg)) : st.angle;
        // New world rotation = R(axis) * parentRot * localRot0; bring it
        // back to the parent's space and re-derive the Euler angles.
        const int parent = renderer.GetMesh3DParent(mesh);
        const glm::mat3 P =
            parent >= 0 ? RotationOnly(renderer.GetMesh3DWorldTRS(parent))
                        : glm::mat3(1.0f);
        const glm::mat3 L0(
            ComposeTRS(glm::vec3(0.0f), st.startRotation, glm::vec3(1.0f)));
        const glm::mat3 Rw(glm::rotate(glm::mat4(1.0f), applied, st.axis));
        const glm::mat3 L = glm::transpose(P) * Rw * P * L0;
        glm::vec3 p, rot, s;
        DecomposeTRS(glm::mat4(L), p, rot, s);
        renderer.SetMesh3DTransform(mesh, st.startPosition, rot,
                                    st.startScale);
      } else if (isScale) {
        glm::vec3 scale = st.startScale;
        if (st.active == kCenter) {
          const float px = (mouse.x - st.startMouse.x) -
                           (mouse.y - st.startMouse.y);
          const float f = std::max(0.01f, 1.0f + px / 120.0f);
          scale = st.startScale * f;
          if (ctrl)
            scale = glm::vec3(Snap(scale.x, kScaleSnap),
                              Snap(scale.y, kScaleSnap),
                              Snap(scale.z, kScaleSnap));
        } else if (rayOk &&
                   RayPlane(ro, rd, st.origin, st.planeNormal, hit)) {
          const float t = glm::dot(hit - st.origin, st.axis);
          float v = st.startScale[st.active] * (t / st.startT);
          if (ctrl)
            v = Snap(v, kScaleSnap);
          scale[st.active] = v;
        }
        for (int i = 0; i < 3; ++i)
          if (std::fabs(scale[i]) < 0.001f)
            scale[i] = scale[i] < 0.0f ? -0.001f : 0.001f;
        renderer.SetMesh3DTransform(mesh, st.startPosition, st.startRotation,
                                    scale);
      }
    }
  }

  // ---- 2. Geometry at the object's current pose ---------------------------
  const ViewInfo view = MakeView(renderer, imgMin, imgSize);
  const glm::vec3 O = renderer.GetMesh3DWorldPosition(mesh);
  const float depth = glm::dot(O - view.camPos, view.camFwd);
  ImVec2 os;
  if (depth <= renderer.camera3d.nearPlane || !Project(view, O, os))
    return res;
  const float L = depth * 2.0f *
                  std::tan(glm::radians(renderer.camera3d.fovDeg) * 0.5f) *
                  (kGizmoPx / imgSize.y);

  glm::vec3 axes[3] = {kWorldAxes[0], kWorldAxes[1], kWorldAxes[2]};
  if (isScale) {
    const glm::mat3 R = RotationOnly(renderer.GetMesh3DWorldTRS(mesh));
    for (int i = 0; i < 3; ++i)
      axes[i] = R[i];
  }

  ImVec2 ends[3];
  bool endOk[3];
  for (int i = 0; i < 3; ++i)
    endOk[i] = Project(view, O + axes[i] * L, ends[i]) &&
               Dist(ends[i], os) > 4.0f;

  // Plane handle quads (Move): square from 0.2L to 0.45L on the two axes
  // other than the plane normal.
  ImVec2 quads[3][4];
  bool quadOk[3] = {false, false, false};
  if (isMove) {
    for (int n = 0; n < 3; ++n) {
      const glm::vec3 a = axes[(n + 1) % 3], b = axes[(n + 2) % 3];
      const float s0 = 0.2f * L, s1 = 0.45f * L;
      quadOk[n] = Project(view, O + a * s0 + b * s0, quads[n][0]) &&
                  Project(view, O + a * s1 + b * s0, quads[n][1]) &&
                  Project(view, O + a * s1 + b * s1, quads[n][2]) &&
                  Project(view, O + a * s0 + b * s1, quads[n][3]);
    }
  }

  // Rotate rings, split into camera-facing (front) and back segments.
  struct RingSeg {
    ImVec2 a, b;
    bool front;
  };
  std::vector<RingSeg> rings[3];
  if (isRotate) {
    const glm::vec3 toCam = view.camPos - O;
    for (int i = 0; i < 3; ++i) {
      glm::vec3 u, w;
      PlaneBasis(axes[i], u, w);
      glm::vec3 prev = O + u * L;
      ImVec2 prevS;
      bool prevOk = Project(view, prev, prevS);
      for (int k = 1; k <= kRingSegments; ++k) {
        const float th = (float)k / kRingSegments * 6.2831853f;
        const glm::vec3 cur = O + (u * std::cos(th) + w * std::sin(th)) * L;
        ImVec2 curS;
        const bool curOk = Project(view, cur, curS);
        if (prevOk && curOk) {
          const glm::vec3 mid = (prev + cur) * 0.5f - O;
          rings[i].push_back({prevS, curS, glm::dot(mid, toCam) >= 0.0f});
        }
        prev = cur;
        prevS = curS;
        prevOk = curOk;
      }
    }
  }

  // ---- 3. Hover + drag start ---------------------------------------------
  int hot = st.active;
  if (st.active < 0 && hovered) {
    float best = kPickPx;
    if ((isMove || isScale) && Dist(mouse, os) < (isScale ? 10.0f : 8.0f)) {
      hot = kCenter;
    } else {
      for (int n = 0; isMove && n < 3 && hot < 0; ++n)
        if (quadOk[n] &&
            (InTriangle(mouse, quads[n][0], quads[n][1], quads[n][2]) ||
             InTriangle(mouse, quads[n][0], quads[n][2], quads[n][3])))
          hot = kPlaneYZ + n;
      for (int i = 0; (isMove || isScale) && i < 3 && hot < 0; ++i)
        if (endOk[i]) {
          const float d = DistToSegment(mouse, os, ends[i]);
          if (d < best) {
            best = d;
            hot = i;
          }
        }
      for (int i = 0; isRotate && i < 3; ++i)
        for (const RingSeg &s : rings[i])
          if (s.front) {
            const float d = DistToSegment(mouse, s.a, s.b);
            if (d < best) {
              best = d;
              hot = i;
            }
          }
    }
    if (hot >= 0)
      res.usingMouse = true;

    if (hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      st.mesh = (int)mesh;
      st.startPosition = renderer.GetMesh3DPosition(mesh);
      st.startRotation = renderer.GetMesh3DRotation(mesh);
      st.startScale = renderer.GetMesh3DScale(mesh);
      st.origin = O;
      st.startMouse = mouse;
      st.angle = 0.0f;
      glm::vec3 ro, rd, hit;
      bool ok = MouseRay(renderer, imgMin, ro, rd);
      if (hot <= kAxisZ) {
        st.axis = axes[hot];
        if (isRotate) {
          st.planeNormal = st.axis;
          ok = ok && RayPlane(ro, rd, O, st.planeNormal, hit);
          if (ok) {
            glm::vec3 d = hit - O;
            ok = glm::dot(d, d) > 1e-10f;
            if (ok)
              st.prevDir = glm::normalize(d);
          }
        } else {
          ok = ok && AxisDragPlane(O, st.axis, view.camPos, st.planeNormal) &&
               RayPlane(ro, rd, O, st.planeNormal, hit);
          if (ok) {
            st.startT = glm::dot(hit - O, st.axis);
            ok = !isScale || std::fabs(st.startT) > 1e-5f;
          }
        }
      } else if (hot == kCenter && isScale) {
        // Uniform scale is driven by mouse pixels; no plane needed.
      } else {
        st.planeNormal = hot == kCenter ? view.camFwd : axes[hot - kPlaneYZ];
        ok = ok && RayPlane(ro, rd, O, st.planeNormal, hit);
        st.startHit = hit;
      }
      if (ok)
        st.active = hot;
    }
  }

  // ---- 4. Draw -------------------------------------------------------------
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(imgMin, ImVec2(imgMin.x + imgSize.x, imgMin.y + imgSize.y),
                   true);
  const auto colorFor = [&](int handle, int axis, int alpha = 255) {
    return handle == hot ? kHotColor : AxisColor(axis, alpha);
  };

  if (isRotate) {
    // Faint screen-space disc so the pivot is visible.
    dl->AddCircle(os, kGizmoPx, IM_COL32(255, 255, 255, 40), 64, 1.0f);
    for (int i = 0; i < 3; ++i)
      for (const RingSeg &s : rings[i])
        if (!s.front)
          dl->AddLine(s.a, s.b, AxisColor(i, 60), 1.5f);
    for (int i = 0; i < 3; ++i)
      for (const RingSeg &s : rings[i])
        if (s.front)
          dl->AddLine(s.a, s.b, colorFor(i, i), i == hot ? 3.5f : 2.5f);
    if (st.active >= 0) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", glm::degrees(st.angle));
      dl->AddText(ImVec2(mouse.x + 16, mouse.y - 4), IM_COL32_WHITE, buf);
    }
  } else {
    for (int n = 0; isMove && n < 3; ++n) {
      if (!quadOk[n])
        continue;
      const ImU32 fill =
          kPlaneYZ + n == hot ? kHotColor : AxisColor(n, 90);
      dl->AddQuadFilled(quads[n][0], quads[n][1], quads[n][2], quads[n][3],
                        fill);
      dl->AddQuad(quads[n][0], quads[n][1], quads[n][2], quads[n][3],
                  AxisColor(n, 200), 1.0f);
    }
    // Far axes first so the ones pointing at the camera stay on top.
    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int a, int b) {
      return glm::dot(axes[a], view.camFwd) > glm::dot(axes[b], view.camFwd);
    });
    for (int i : order) {
      if (!endOk[i])
        continue;
      const ImU32 col = colorFor(i, i);
      if (isMove) {
        DrawArrow(dl, os, ends[i], col, 3.0f);
      } else {
        dl->AddLine(os, ends[i], col, 3.0f);
        dl->AddRectFilled(ImVec2(ends[i].x - 5, ends[i].y - 5),
                          ImVec2(ends[i].x + 5, ends[i].y + 5), col);
      }
    }
    const float c = isScale ? 7.0f : 5.0f;
    dl->AddRectFilled(ImVec2(os.x - c, os.y - c), ImVec2(os.x + c, os.y + c),
                      hot == kCenter ? kHotColor
                                     : IM_COL32(235, 235, 235, 220));
    if (st.active >= 0) {
      const glm::vec3 v = isScale ? renderer.GetMesh3DScale(mesh)
                                  : renderer.GetMesh3DPosition(mesh);
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", v.x, v.y, v.z);
      dl->AddText(ImVec2(mouse.x + 16, mouse.y - 4), IM_COL32_WHITE, buf);
    }
  }
  dl->PopClipRect();
  return res;
}

GizmoResult OrientationGizmo(SceneRenderer &renderer, int selectedMesh,
                             ImVec2 imgMin, ImVec2 imgSize, bool hovered,
                             bool cameraInput, float deltaTime,
                             OrientationGizmoState &st) {
  GizmoResult res;
  auto &cam = renderer.camera3d;

  // ---- Snap animation ------------------------------------------------------
  if (st.animating) {
    if (cameraInput) {
      st.animating = false;
    } else {
      st.t = std::min(1.0f, st.t + deltaTime / 0.3f);
      const float k = st.t * st.t * (3.0f - 2.0f * st.t);
      cam.yaw = st.fromYaw + (st.toYaw - st.fromYaw) * k;
      cam.pitch = st.fromPitch + (st.toPitch - st.fromPitch) * k;
      cam.position = st.pivot - renderer.GetCameraForward() * st.distance +
                     st.startOffset * (1.0f - k);
      if (st.t >= 1.0f) {
        st.animating = false;
        res.editFinished = true;
      }
    }
  }

  constexpr float R = 40.0f; // axis length in pixels
  const ImVec2 c(imgMin.x + imgSize.x - R - 24.0f, imgMin.y + R + 24.0f);
  if (imgSize.x < 4.0f * R || imgSize.y < 4.0f * R)
    return res;

  // ---- Handles ---------------------------------------------------------
  struct AxisHandle {
    int axis;
    bool positive;
    ImVec2 pos;
    float depth; // view-space z: larger = closer to the camera
    float radius;
  };
  const glm::mat3 V(renderer.GetCamera3DView());
  AxisHandle h[6];
  for (int i = 0; i < 6; ++i) {
    const int axis = i % 3;
    const bool positive = i < 3;
    const glm::vec3 v = V * (kWorldAxes[axis] * (positive ? 1.0f : -1.0f));
    h[i] = {axis, positive, ImVec2(c.x + v.x * R, c.y - v.y * R), v.z,
            positive ? 10.0f : 7.5f};
  }
  std::sort(h, h + 6, [](const AxisHandle &a, const AxisHandle &b) {
    return a.depth < b.depth;
  });

  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool overGizmo = hovered && Dist(mouse, c) < R + 16.0f;
  int hot = -1;
  if (overGizmo) {
    res.usingMouse = true;
    for (int i = 5; i >= 0 && hot < 0; --i) // front-most first
      if (Dist(mouse, h[i].pos) <= h[i].radius + 1.0f)
        hot = i;
  }

  // ---- Draw ----------------------------------------------------------------
  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (overGizmo)
    dl->AddCircleFilled(c, R + 16.0f, IM_COL32(255, 255, 255, 28), 48);
  static const char *kLabels[3] = {"X", "Y", "Z"};
  for (int i = 0; i < 6; ++i) {
    const AxisHandle &a = h[i];
    if (a.positive) {
      dl->AddLine(c, a.pos, AxisColor(a.axis), 2.5f);
      dl->AddCircleFilled(a.pos, a.radius, AxisColor(a.axis), 24);
      const ImVec2 ts = ImGui::CalcTextSize(kLabels[a.axis]);
      dl->AddText(ImVec2(a.pos.x - ts.x * 0.5f, a.pos.y - ts.y * 0.5f),
                  IM_COL32(20, 20, 24, 255), kLabels[a.axis]);
    } else {
      dl->AddCircleFilled(a.pos, a.radius, AxisColor(a.axis, 70), 24);
      dl->AddCircle(a.pos, a.radius, AxisColor(a.axis, 220), 24, 1.5f);
    }
    if (i == hot)
      dl->AddCircle(a.pos, a.radius + 2.0f, IM_COL32_WHITE, 24, 2.0f);
  }

  if (hot >= 0) {
    static const char *kViews[2][3] = {{"Left (-X)", "Bottom (-Y)", "Back (-Z)"},
                                       {"Right (+X)", "Top (+Y)", "Front (+Z)"}};
    ImGui::SetTooltip("View from %s", kViews[h[hot].positive][h[hot].axis]);
  }

  // ---- Click: start the snap animation -------------------------------------
  if (hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const glm::vec3 axisDir =
        kWorldAxes[h[hot].axis] * (h[hot].positive ? 1.0f : -1.0f);
    const glm::vec3 targetFwd = -axisDir;
    const glm::vec3 fwd = renderer.GetCameraForward();

    if (selectedMesh >= 0 &&
        (size_t)selectedMesh < renderer.GetMesh3DCount()) {
      st.pivot = renderer.GetMesh3DWorldPosition((size_t)selectedMesh);
      st.distance = glm::length(st.pivot - cam.position);
      if (st.distance < 0.5f)
        st.distance = 5.0f;
    } else {
      st.distance = 10.0f;
      st.pivot = cam.position + fwd * st.distance;
    }

    st.fromYaw = cam.yaw;
    st.fromPitch = cam.pitch;
    if (std::fabs(targetFwd.y) > 0.9f) {
      // Top/bottom: keep the heading, squared to the nearest axis, so the
      // view doesn't spin. Pitch stops at the same ±89 limit as mouselook.
      st.toYaw = std::round(cam.yaw / 90.0f) * 90.0f;
      st.toPitch = targetFwd.y < 0.0f ? -89.0f : 89.0f;
    } else {
      st.toYaw = glm::degrees(std::atan2(targetFwd.z, targetFwd.x));
      st.toPitch = 0.0f;
    }
    // Shortest way round.
    st.toYaw =
        st.fromYaw + std::fmod(st.toYaw - st.fromYaw + 540.0f, 360.0f) - 180.0f;
    st.startOffset = cam.position - (st.pivot - fwd * st.distance);
    st.t = 0.0f;
    st.animating = true;
  }
  return res;
}

} // namespace ilmeee
