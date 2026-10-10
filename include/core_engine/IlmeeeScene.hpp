#pragma once
// `.ilmeeescene` — Ilmeee engine native scene container.
//
// Binary format v1 (little-endian assumed; ASCII strings):
//
//   uint8  magic[4]      = 'I','L','M','S'
//   uint16 versionMajor  = 1
//   uint16 versionMinor  = 0
//   uint32 entityCount
//   for each entity:
//     uint16 nameLen
//     char   name[nameLen]
//     uint8  kind         (0 = ExternalObj, 1 = Cube, 2 = Sphere, 3 = Plane)
//     uint16 externalPathLen
//     char   externalPath[externalPathLen]   (relative to project root)
//     float32 position[3]
//     float32 rotationEulerDeg[3]
//     float32 scale[3]
//     -- v1.1 light, v1.2 camera + surface textures (see SaveScene) --
//     int32   parent      (v1.3) index of the parent entity, -1 = root;
//                         position/rotation/scale are then parent-local
//   -- after all entities, v1.4 environment --
//   float32 backgroundColor[4]   clear colour the scene is shown against
//   -- v1.5 UI entities: Canvas/UiText/UiImage/UiButton carry screen-space
//      properties after their parent field; world entities have no extra data --
//   -- v1.6 every entity carries movable, movementScript, movementSpeed
//      after its optional UI fields --
//   -- v1.7 then int32 rigCamera (entity index, -1 = Game view camera),
//      uint32 cameraStyle, float32 distance, float32 height,
//      string moveJoystick, string lookJoystick; UiJoystick entities --
//
// Why a custom binary container instead of JSON: scenes will get big
// (lots of meshes, transforms, hierarchy, per-instance overrides), and
// keeping them out of text editors signals to authors that the scene
// graph is authored through the editor — not by hand.

#include <cstdint>
#include <cstring>
#include <fstream>
#include "MovementScript.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace ilmeee {

enum class PrimitiveKind : uint8_t {
  ExternalObj = 0,
  Cube = 1,
  Sphere = 2,
  Plane = 3,
  ExternalPmx = 4,
  Light = 5,
  Camera = 6,
  Canvas = 7,
  UiText = 8,
  UiImage = 9,
  UiButton = 10,
  UiJoystick = 11,
};

inline bool IsUiElementKind(PrimitiveKind kind) {
  return kind == PrimitiveKind::UiText || kind == PrimitiveKind::UiImage ||
         kind == PrimitiveKind::UiButton || kind == PrimitiveKind::UiJoystick;
}

// One per-surface texture override: which material/surface index of the
// entity's mesh, and the texture path (relative to project root when it
// lives inside the project, otherwise absolute).
struct SurfaceTexture {
  uint32_t surfaceIndex = 0;
  std::string texturePath;
};

struct SceneEntity {
  std::string name;
  PrimitiveKind kind = PrimitiveKind::Cube;
  std::string externalPath; // relative to project root, for ExternalObj
  glm::vec3 position{0.0f};
  glm::vec3 rotationEuler{0.0f};
  glm::vec3 scale{1.0f};

  // Light properties (active only when kind == PrimitiveKind::Light)
  float lightGamma = 1.05f;
  glm::vec3 lightColor{1.0f, 0.96f, 0.88f};
  float lightIntensity = 1.0f;
  int lightType = 0; // 0 = Directional, 1 = Point, 2 = Spotlight
  float lightRange = 10.0f;
  float lightSpotAngle = 30.0f;

  // Camera properties (active only when kind == PrimitiveKind::Camera) — v1.2
  int camProjection = 0; // 0 = Perspective, 1 = Orthographic
  float camFov = 60.0f;
  float camOrthoSize = 5.0f;
  float camNear = 0.1f;
  float camFar = 100.0f;

  // Per-surface texture bindings (v1.2). Empty for entities the user never
  // re-textured beyond the model's own embedded textures.
  std::vector<SurfaceTexture> surfaceTextures;

  // Index of the parent in IlmeeeScene::entities, -1 for a root (v1.3).
  int32_t parent = -1;

  // v1.6 native movement component. Empty script means no movement.
  bool movable = false;
  std::string movementScript;
  float movementSpeed = 3.0f;
  // v1.7 camera rig + joystick links; rig.camera is an entity index.
  MovementRig movementRig;

  // v1.5 screen-space UI. Canvas uses uiWidth/uiHeight as reference size;
  // child elements use them as their displayed size.
  uint8_t uiAnchor = 0;
  float uiX = 0.0f, uiY = 0.0f;
  float uiWidth = 160.0f, uiHeight = 44.0f;
  float uiFontSize = 24.0f;
  std::string uiText;
  std::string uiImagePath;
  std::string uiAction;
  glm::vec4 uiColor{1.0f};
  glm::vec4 uiBackground{0.14f, 0.20f, 0.31f, 0.92f};
};

struct IlmeeeScene {
  std::vector<SceneEntity> entities;
  // v1.4. Default matches SceneRenderer's, so older files look unchanged.
  glm::vec4 backgroundColor{0.2f, 0.2f, 0.2f, 1.0f};
};

namespace detail {

inline void WriteBytes(std::ofstream &f, const void *data, size_t n) {
  f.write(reinterpret_cast<const char *>(data), (std::streamsize)n);
}
inline bool ReadBytes(std::ifstream &f, void *data, size_t n) {
  f.read(reinterpret_cast<char *>(data), (std::streamsize)n);
  return f.good() || (f.eof() && (size_t)f.gcount() == n);
}

inline void WriteU16(std::ofstream &f, uint16_t v) { WriteBytes(f, &v, 2); }
inline void WriteU32(std::ofstream &f, uint32_t v) { WriteBytes(f, &v, 4); }
inline void WriteF32(std::ofstream &f, float v) { WriteBytes(f, &v, 4); }
inline void WriteVec3(std::ofstream &f, const glm::vec3 &v) {
  WriteF32(f, v.x);
  WriteF32(f, v.y);
  WriteF32(f, v.z);
}
inline void WriteVec4(std::ofstream &f, const glm::vec4 &v) {
  for (int i = 0; i < 4; ++i) WriteF32(f, v[i]);
}
inline void WriteString(std::ofstream &f, const std::string &s) {
  uint16_t n = (uint16_t)std::min<size_t>(s.size(), 65535);
  WriteU16(f, n);
  if (n)
    f.write(s.data(), n);
}

inline bool ReadU16(std::ifstream &f, uint16_t &v) {
  return (bool)f.read(reinterpret_cast<char *>(&v), 2);
}
inline bool ReadU32(std::ifstream &f, uint32_t &v) {
  return (bool)f.read(reinterpret_cast<char *>(&v), 4);
}
inline bool ReadF32(std::ifstream &f, float &v) {
  return (bool)f.read(reinterpret_cast<char *>(&v), 4);
}
inline bool ReadVec3(std::ifstream &f, glm::vec3 &v) {
  return ReadF32(f, v.x) && ReadF32(f, v.y) && ReadF32(f, v.z);
}
inline bool ReadVec4(std::ifstream &f, glm::vec4 &v) {
  for (int i = 0; i < 4; ++i)
    if (!ReadF32(f, v[i])) return false;
  return true;
}
inline bool ReadString(std::ifstream &f, std::string &out) {
  uint16_t n = 0;
  if (!ReadU16(f, n))
    return false;
  out.resize(n);
  if (n && !f.read(out.data(), n))
    return false;
  return true;
}

} // namespace detail

inline bool SaveScene(const std::string &path, const IlmeeeScene &scene) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open())
    return false;
  const char magic[4] = {'I', 'L', 'M', 'S'};
  detail::WriteBytes(f, magic, 4);
  detail::WriteU16(f, 1); // major
  detail::WriteU16(f, 7); // minor
  detail::WriteU32(f, (uint32_t)scene.entities.size());
  for (const auto &e : scene.entities) {
    detail::WriteString(f, e.name);
    uint8_t k = (uint8_t)e.kind;
    detail::WriteBytes(f, &k, 1);
    detail::WriteString(f, e.externalPath);
    detail::WriteVec3(f, e.position);
    detail::WriteVec3(f, e.rotationEuler);
    detail::WriteVec3(f, e.scale);

    // Version 1.1 extra fields
    detail::WriteF32(f, e.lightGamma);
    detail::WriteVec3(f, e.lightColor);
    detail::WriteF32(f, e.lightIntensity);
    detail::WriteU32(f, (uint32_t)e.lightType);
    detail::WriteF32(f, e.lightRange);
    detail::WriteF32(f, e.lightSpotAngle);

    // Version 1.2: camera params + per-surface texture bindings
    detail::WriteU32(f, (uint32_t)e.camProjection);
    detail::WriteF32(f, e.camFov);
    detail::WriteF32(f, e.camOrthoSize);
    detail::WriteF32(f, e.camNear);
    detail::WriteF32(f, e.camFar);
    detail::WriteU32(f, (uint32_t)e.surfaceTextures.size());
    for (const auto &st : e.surfaceTextures) {
      detail::WriteU32(f, st.surfaceIndex);
      detail::WriteString(f, st.texturePath);
    }

    // Version 1.3: hierarchy
    detail::WriteU32(f, (uint32_t)e.parent);
    // Version 1.5: only UI entities carry screen-space data.
    if (e.kind == PrimitiveKind::Canvas) {
      detail::WriteF32(f, e.uiWidth);
      detail::WriteF32(f, e.uiHeight);
    } else if (IsUiElementKind(e.kind)) {
      detail::WriteBytes(f, &e.uiAnchor, 1);
      detail::WriteF32(f, e.uiX);
      detail::WriteF32(f, e.uiY);
      detail::WriteF32(f, e.uiWidth);
      detail::WriteF32(f, e.uiHeight);
      detail::WriteF32(f, e.uiFontSize);
      detail::WriteString(f, e.uiText);
      detail::WriteString(f, e.uiImagePath);
      detail::WriteString(f, e.uiAction);
      detail::WriteVec4(f, e.uiColor);
      detail::WriteVec4(f, e.uiBackground);
    }
    // Version 1.6: movement component, including inactive/default values.
    detail::WriteU32(f, e.movable ? 1u : 0u);
    detail::WriteString(f, e.movementScript);
    detail::WriteF32(f, e.movementSpeed);
    // Version 1.7: camera rig and joystick links.
    detail::WriteU32(f, (uint32_t)e.movementRig.camera);
    detail::WriteU32(f, (uint32_t)e.movementRig.style);
    detail::WriteF32(f, e.movementRig.distance);
    detail::WriteF32(f, e.movementRig.height);
    detail::WriteString(f, e.movementRig.moveJoystick);
    detail::WriteString(f, e.movementRig.lookJoystick);
  }

  // Version 1.4: environment
  for (int c = 0; c < 4; ++c)
    detail::WriteF32(f, scene.backgroundColor[c]);
  return f.good();
}

inline bool LoadScene(const std::string &path, IlmeeeScene &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  char magic[4];
  if (!f.read(magic, 4))
    return false;
  if (std::memcmp(magic, "ILMS", 4) != 0)
    return false;
  uint16_t major = 0, minor = 0;
  if (!detail::ReadU16(f, major) || !detail::ReadU16(f, minor))
    return false;
  if (major != 1)
    return false; // v1 only for now
  uint32_t count = 0;
  if (!detail::ReadU32(f, count))
    return false;
  out.entities.clear();
  out.entities.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    SceneEntity e;
    if (!detail::ReadString(f, e.name))
      return false;
    uint8_t k = 0;
    if (!f.read(reinterpret_cast<char *>(&k), 1))
      return false;
    e.kind = (PrimitiveKind)k;
    if (!detail::ReadString(f, e.externalPath))
      return false;
    if (!detail::ReadVec3(f, e.position))
      return false;
    if (!detail::ReadVec3(f, e.rotationEuler))
      return false;
    if (!detail::ReadVec3(f, e.scale))
      return false;

    // Load version 1.1 fields if available
    if (minor >= 1) {
      if (!detail::ReadF32(f, e.lightGamma))
        return false;
      if (!detail::ReadVec3(f, e.lightColor))
        return false;
      if (!detail::ReadF32(f, e.lightIntensity))
        return false;
      uint32_t lt = 0;
      if (!detail::ReadU32(f, lt))
        return false;
      e.lightType = (int)lt;
      if (!detail::ReadF32(f, e.lightRange))
        return false;
      if (!detail::ReadF32(f, e.lightSpotAngle))
        return false;
    }

    // Load version 1.2 fields if available (camera + surface textures).
    if (minor >= 2) {
      uint32_t proj = 0;
      if (!detail::ReadU32(f, proj))
        return false;
      e.camProjection = (int)proj;
      if (!detail::ReadF32(f, e.camFov))
        return false;
      if (!detail::ReadF32(f, e.camOrthoSize))
        return false;
      if (!detail::ReadF32(f, e.camNear))
        return false;
      if (!detail::ReadF32(f, e.camFar))
        return false;
      uint32_t texCount = 0;
      if (!detail::ReadU32(f, texCount))
        return false;
      e.surfaceTextures.resize(texCount);
      for (uint32_t t = 0; t < texCount; ++t) {
        if (!detail::ReadU32(f, e.surfaceTextures[t].surfaceIndex))
          return false;
        if (!detail::ReadString(f, e.surfaceTextures[t].texturePath))
          return false;
      }
    }

    // Load version 1.3 fields if available (hierarchy).
    if (minor >= 3) {
      uint32_t parent = 0;
      if (!detail::ReadU32(f, parent))
        return false;
      e.parent = (int32_t)parent;
    }
    if (minor >= 5) {
      if (e.kind == PrimitiveKind::Canvas) {
        if (!detail::ReadF32(f, e.uiWidth) ||
            !detail::ReadF32(f, e.uiHeight)) return false;
      } else if (IsUiElementKind(e.kind)) {
        if (!detail::ReadBytes(f, &e.uiAnchor, 1) ||
            !detail::ReadF32(f, e.uiX) || !detail::ReadF32(f, e.uiY) ||
            !detail::ReadF32(f, e.uiWidth) ||
            !detail::ReadF32(f, e.uiHeight) ||
            !detail::ReadF32(f, e.uiFontSize) ||
            !detail::ReadString(f, e.uiText) ||
            !detail::ReadString(f, e.uiImagePath) ||
            !detail::ReadString(f, e.uiAction) ||
            !detail::ReadVec4(f, e.uiColor) ||
            !detail::ReadVec4(f, e.uiBackground)) return false;
      }
    }
    if (minor >= 6) {
      uint32_t movable = 0;
      if (!detail::ReadU32(f, movable) ||
          !detail::ReadString(f, e.movementScript) ||
          !detail::ReadF32(f, e.movementSpeed)) return false;
      e.movable = movable != 0;
    }
    if (minor >= 7) {
      uint32_t camera = 0, style = 0;
      if (!detail::ReadU32(f, camera) || !detail::ReadU32(f, style) ||
          !detail::ReadF32(f, e.movementRig.distance) ||
          !detail::ReadF32(f, e.movementRig.height) ||
          !detail::ReadString(f, e.movementRig.moveJoystick) ||
          !detail::ReadString(f, e.movementRig.lookJoystick)) return false;
      e.movementRig.camera = (int32_t)camera;
      e.movementRig.style = style <= (uint32_t)CameraStyle::TopDown
                                ? (CameraStyle)style : CameraStyle::None;
    }
    out.entities.push_back(std::move(e));
  }

  out.backgroundColor = IlmeeeScene().backgroundColor;
  if (minor >= 4) {
    for (int c = 0; c < 4; ++c)
      if (!detail::ReadF32(f, out.backgroundColor[c]))
        return false;
  }

  // A link that points outside the file, at itself, or into a loop would
  // hang or confuse the hierarchy; demote such entities to roots.
  const int32_t n = (int32_t)out.entities.size();
  for (auto &e : out.entities)
    if (e.movementRig.camera < -1 || e.movementRig.camera >= n)
      e.movementRig.camera = -1;
  for (int32_t i = 0; i < n; ++i) {
    int32_t p = out.entities[i].parent;
    if (p < -1 || p >= n || p == i) {
      out.entities[i].parent = -1;
      continue;
    }
    int32_t cur = p;
    for (int32_t steps = 0; cur >= 0 && steps <= n; ++steps) {
      if (cur == i) {
        out.entities[i].parent = -1;
        break;
      }
      cur = out.entities[cur].parent;
    }
  }
  return true;
}

// Default scene used the first time a project is opened: one Cube at
// origin. Hub generates this automatically when the user hits
// "+ New Project" but the editor also self-heals when opening a
// project that has no scenes/main.ilmeeescene yet.
inline IlmeeeScene DefaultScene() {
  IlmeeeScene s;
  SceneEntity e;
  e.name = "Cube";
  e.kind = PrimitiveKind::Cube;
  e.scale = glm::vec3(1.0f);
  s.entities.push_back(std::move(e));
  return s;
}

} // namespace ilmeee
