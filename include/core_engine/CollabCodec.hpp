#pragma once
// What the editor puts into the realtime collab room (see net/CollabClient.hpp).
//
// One scene object = key ObjectKey(objectId) with two slots:
//   "data"  {blob, parent, rigCamera, name}
//           blob = base64 of a one-entity .ilmeeescene (same bytes as a scene
//           file, so every component the file format knows travels for free).
//           Transform, parent and rig camera are stripped from the blob: the
//           transform has its own slot, and parent / rig camera are object
//           keys here because entity indices differ between editors.
//   "xform" {p:[x,y,z], r:[x,y,z], s:[x,y,z]}  parent-local, as in the file.
// Scene settings (background, NPR, bloom) = key kEnvironmentKey, slot "data",
// blob = a zero-entity scene.
//
// Two slots keep a gizmo drag cheap (only xform is sent, ~60 bytes, at a
// throttled rate) while still letting any other edit carry the full object.
//
// Pure data, no renderer: unit-tested in test/net/TestCollabClient.cpp.

#include "IlmeeeScene.hpp"
#include "../../vendor/nlohmann/json.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace ilmeee::collab {

using json = nlohmann::json;

inline constexpr const char *kSlotData = "data";
inline constexpr const char *kSlotXform = "xform";
inline constexpr const char *kEnvironmentKey = "env";

inline std::string ObjectKey(uint64_t objectId) {
  if (!objectId)
    return {};
  char buf[24];
  std::snprintf(buf, sizeof buf, "o%016llx", (unsigned long long)objectId);
  return buf;
}

// 0 when `key` is not an object key.
inline uint64_t ParseObjectKey(std::string_view key) {
  if (key.size() != 17 || key[0] != 'o')
    return 0;
  uint64_t v = 0;
  for (char c : key.substr(1)) {
    v <<= 4;
    if (c >= '0' && c <= '9')
      v |= (uint64_t)(c - '0');
    else if (c >= 'a' && c <= 'f')
      v |= (uint64_t)(c - 'a' + 10);
    else
      return 0;
  }
  return v;
}

inline std::string Base64Encode(std::string_view in) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t n = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
    out += kAlphabet[n >> 18 & 63];
    out += kAlphabet[n >> 12 & 63];
    out += kAlphabet[n >> 6 & 63];
    out += kAlphabet[n & 63];
  }
  if (i < in.size()) {
    uint32_t n = (uint8_t)in[i] << 16;
    if (i + 1 < in.size())
      n |= (uint8_t)in[i + 1] << 8;
    out += kAlphabet[n >> 18 & 63];
    out += kAlphabet[n >> 12 & 63];
    out += i + 1 < in.size() ? kAlphabet[n >> 6 & 63] : '=';
    out += '=';
  }
  return out;
}

inline bool Base64Decode(std::string_view in, std::string &out) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  if (in.size() % 4 != 0)
    return false;
  out.clear();
  out.reserve(in.size() / 4 * 3);
  for (size_t i = 0; i < in.size(); i += 4) {
    int v[4];
    int pad = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = in[i + k];
      if (c == '=' && i + 4 == in.size() && k >= 2) {
        v[k] = 0;
        ++pad;
      } else if (pad || (v[k] = value(c)) < 0) {
        return false;
      }
    }
    const uint32_t n = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
    out += (char)(n >> 16 & 255);
    if (pad < 2)
      out += (char)(n >> 8 & 255);
    if (pad < 1)
      out += (char)(n & 255);
  }
  return true;
}

// ---- object data ----------------------------------------------------------

inline json EncodeEntityData(SceneEntity e, uint64_t parentId,
                             uint64_t rigCameraId) {
  e.position = glm::vec3(0.0f);
  e.rotationEuler = glm::vec3(0.0f);
  e.scale = glm::vec3(1.0f);
  e.parent = -1;
  e.movementRig.camera = -1;
  IlmeeeScene one;
  one.entities.push_back(e);
  return {{"blob", Base64Encode(SaveSceneToBytes(one))},
          {"parent", ObjectKey(parentId)},
          {"rigCamera", ObjectKey(rigCameraId)},
          {"name", e.name}};
}

struct EntityData {
  SceneEntity entity; // transform defaulted, parent / rig camera = -1
  uint64_t parentId = 0;
  uint64_t rigCameraId = 0;
};

inline bool DecodeEntityData(const json &data, EntityData &out) {
  if (!data.is_object())
    return false;
  std::string bytes;
  if (!Base64Decode(data.value("blob", std::string()), bytes))
    return false;
  IlmeeeScene one;
  if (!LoadSceneFromBytes(bytes, one) || one.entities.size() != 1)
    return false;
  out.entity = std::move(one.entities[0]);
  out.entity.parent = -1;
  out.entity.movementRig.camera = -1;
  out.parentId = ParseObjectKey(data.value("parent", std::string()));
  out.rigCameraId = ParseObjectKey(data.value("rigCamera", std::string()));
  return true;
}

// ---- transform ------------------------------------------------------------

struct Xform {
  glm::vec3 p{0.0f}, r{0.0f}, s{1.0f};
  bool operator==(const Xform &) const = default;
};

inline json EncodeXform(const Xform &x) {
  return {{"p", {x.p.x, x.p.y, x.p.z}},
          {"r", {x.r.x, x.r.y, x.r.z}},
          {"s", {x.s.x, x.s.y, x.s.z}}};
}

inline bool DecodeXform(const json &data, Xform &out) {
  auto vec = [&](const char *k, glm::vec3 &v) {
    auto it = data.find(k);
    if (it == data.end() || !it->is_array() || it->size() != 3)
      return false;
    for (int i = 0; i < 3; ++i) {
      if (!(*it)[i].is_number())
        return false;
      v[i] = (*it)[i].get<float>();
    }
    return true;
  };
  if (!data.is_object())
    return false;
  Xform x;
  if (!vec("p", x.p) || !vec("r", x.r) || !vec("s", x.s))
    return false;
  out = x;
  return true;
}

// ---- scene settings -------------------------------------------------------

inline json EncodeEnvironment(const IlmeeeScene &scene) {
  IlmeeeScene env;
  env.backgroundColor = scene.backgroundColor;
  env.npr = scene.npr;
  env.bloom = scene.bloom;
  return {{"blob", Base64Encode(SaveSceneToBytes(env))}};
}

inline bool DecodeEnvironment(const json &data, IlmeeeScene &out) {
  if (!data.is_object())
    return false;
  std::string bytes;
  if (!Base64Decode(data.value("blob", std::string()), bytes))
    return false;
  IlmeeeScene env;
  if (!LoadSceneFromBytes(bytes, env))
    return false;
  out.backgroundColor = env.backgroundColor;
  out.npr = env.npr;
  out.bloom = env.bloom;
  return true;
}

} // namespace ilmeee::collab
