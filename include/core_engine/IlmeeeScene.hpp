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
//
// Why a custom binary container instead of JSON: scenes will get big
// (lots of meshes, transforms, hierarchy, per-instance overrides), and
// keeping them out of text editors signals to authors that the scene
// graph is authored through the editor — not by hand.

#include <cstdint>
#include <cstring>
#include <fstream>
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
};

struct IlmeeeScene {
  std::vector<SceneEntity> entities;
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
  detail::WriteU16(f, 1); // minor
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
  }
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
    out.entities.push_back(std::move(e));
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
