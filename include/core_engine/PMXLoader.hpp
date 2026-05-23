#pragma once
// PMX (MikuMikuDance eXtended) model loader — binary format v2.0 / v2.1.
//
// References:
//   - PMX specification (Japanese, MMD community wiki)
//   - https://gist.github.com/felixjones/f8a06bd48f9da9a4539f
//
// This loader reads geometry (vertices, indices), materials, texture
// paths, and the full bone hierarchy. Morphs, physics bodies, and
// joints are skipped for now — they will be implemented when animation
// (VMD) support lands.

#include "Debugger.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace pmx {

// ---------------------------------------------------------------------------
// Vertex weight deformation types
// ---------------------------------------------------------------------------

enum class DeformType : uint8_t {
  BDEF1 = 0,
  BDEF2 = 1,
  BDEF4 = 2,
  SDEF = 3,
  QDEF = 4, // PMX 2.1+
};

struct PMXVertex {
  glm::vec3 position{0.0f};
  glm::vec3 normal{0.0f};
  glm::vec2 uv{0.0f};
  DeformType deformType = DeformType::BDEF1;
  int32_t boneIndices[4] = {0, 0, 0, 0};
  float boneWeights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  // SDEF-specific
  glm::vec3 sdefC{0.0f};
  glm::vec3 sdefR0{0.0f};
  glm::vec3 sdefR1{0.0f};
  float edgeScale = 1.0f;
};

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

struct PMXMaterial {
  std::string nameJP;
  std::string nameEN;
  glm::vec4 diffuse{1.0f};
  glm::vec3 specular{0.0f};
  float specularStrength = 0.0f;
  glm::vec3 ambient{0.0f};
  uint8_t drawFlags = 0; // bit flags: double-sided, ground shadow, etc.
  glm::vec4 edgeColor{0.0f};
  float edgeSize = 0.0f;
  int32_t textureIndex = -1;
  int32_t sphereTextureIndex = -1;
  uint8_t sphereMode = 0; // 0=disabled, 1=multiply, 2=add, 3=sub-texture
  uint8_t toonFlag = 0;   // 0=texture reference, 1=internal toon
  int32_t toonTextureIndex = -1;
  std::string memo;
  int32_t indexCount = 0; // number of face indices this material covers
};

// ---------------------------------------------------------------------------
// Bone
// ---------------------------------------------------------------------------

struct PMXBone {
  std::string nameJP;
  std::string nameEN;
  glm::vec3 position{0.0f};
  int32_t parentIndex = -1;
  int32_t transformLayer = 0;
  uint16_t flags = 0;

  // Tail position (if flag bit 0 set: bone index, else offset)
  int32_t tailBoneIndex = -1;
  glm::vec3 tailOffset{0.0f};

  // Inherit (flag bit 8/9)
  int32_t inheritParentIndex = -1;
  float inheritWeight = 0.0f;

  // Fixed axis (flag bit 10)
  glm::vec3 fixedAxis{0.0f};

  // Local axis (flag bit 11)
  glm::vec3 localAxisX{1.0f, 0.0f, 0.0f};
  glm::vec3 localAxisZ{0.0f, 0.0f, 1.0f};

  // External parent (flag bit 12)
  int32_t externalParentKey = 0;

  // IK (flag bit 5)
  int32_t ikTargetBoneIndex = -1;
  int32_t ikLoopCount = 0;
  float ikLimitAngle = 0.0f;
  struct IKLink {
    int32_t boneIndex = -1;
    bool hasLimits = false;
    glm::vec3 lowerLimit{0.0f};
    glm::vec3 upperLimit{0.0f};
  };
  std::vector<IKLink> ikLinks;
};

// ---------------------------------------------------------------------------
// Top-level model container
// ---------------------------------------------------------------------------

struct PMXModel {
  float version = 2.0f;
  std::string nameJP;
  std::string nameEN;
  std::string commentJP;
  std::string commentEN;

  std::vector<PMXVertex> vertices;
  std::vector<uint32_t> indices;         // triangle face indices
  std::vector<std::string> texturePaths; // relative to .pmx directory
  std::vector<PMXMaterial> materials;
  std::vector<PMXBone> bones;
};

// Load a PMX file from disk. Returns true on success, fills `out`.
bool LoadPMX(const std::string &path, PMXModel &out);

} // namespace pmx
