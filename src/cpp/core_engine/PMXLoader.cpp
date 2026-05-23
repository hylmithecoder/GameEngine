#include "../../include/core_engine/PMXLoader.hpp"
#include "core_engine/Debugger.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Debug;
// PMX binary format reference (PMX 2.0 / 2.1):
//
// Header:
//   char[4]  magic = "PMX " (note trailing space)
//   float    version (2.0 or 2.1)
//   uint8    globalsCount (always 8)
//   uint8[8] globals:
//     [0] encoding      0 = UTF-16LE, 1 = UTF-8
//     [1] additionalVec4Count (extra UV vectors, 0..4)
//     [2] vertexIndexSize (1, 2, or 4 bytes)
//     [3] textureIndexSize (1, 2, or 4 bytes)
//     [4] materialIndexSize
//     [5] boneIndexSize
//     [6] morphIndexSize
//     [7] rigidBodyIndexSize
//
// Then: model info, vertices, faces, textures, materials, bones, ...

namespace pmx {

namespace {

// ---- Low-level binary readers ----

struct Reader {
  // Bind the input stream explicitly. Without this, `Reader r{file}` would
  // aggregate-init the first data member (setDetail) with the ifstream.
  explicit Reader(std::ifstream &file) : f(file) {}

  struct SetDetail {
    std::vector<std::string> typeDataName;
  };

  SetDetail setDetail;

  void ReadAllDetail() {
    int countU8 = 0, countI8 = 0, countU16 = 0, countI16 = 0, countU32 = 0,
        countI32 = 0, countF32 = 0;
    for (int i = 0; i < setDetail.typeDataName.size(); i++) {
      if (setDetail.typeDataName[i] == "U8 Type Data") {
        countU8++;
      } else if (setDetail.typeDataName[i] == "I8 Type Data") {
        countI8++;
      } else if (setDetail.typeDataName[i] == "U16 Type Data") {
        countU16++;
      } else if (setDetail.typeDataName[i] == "I16 Type Data") {
        countI16++;
      } else if (setDetail.typeDataName[i] == "U32 Type Data") {
        countU32++;
      } else if (setDetail.typeDataName[i] == "I32 Type Data") {
        countI32++;
      } else if (setDetail.typeDataName[i] == "F32 Type Data") {
        countF32++;
      }
    }
    DEBUG_LOG("Count U8: %d", countU8);
    DEBUG_LOG("Count I8: %d", countI8);
    DEBUG_LOG("Count U16: %d", countU16);
    DEBUG_LOG("Count I16: %d", countI16);
    DEBUG_LOG("Count U32: %d", countU32);
    DEBUG_LOG("Count I32: %d", countI32);
    DEBUG_LOG("Count F32: %d", countF32);
  }

  std::ifstream &f;
  uint8_t encoding = 0; // 0 = UTF-16LE, 1 = UTF-8
  uint8_t vertIdxSize = 4;
  uint8_t texIdxSize = 4;
  uint8_t matIdxSize = 4;
  uint8_t boneIdxSize = 4;
  uint8_t morphIdxSize = 4;
  uint8_t rigidIdxSize = 4;

  bool ok() const { return f.good() && !f.eof(); }

  bool readBytes(void *dst, size_t n) {
    f.read(reinterpret_cast<char *>(dst), (std::streamsize)n);
    return f.good() || (f.eof() && (size_t)f.gcount() == (std::streamsize)n);
  }

  uint8_t readU8() {
    uint8_t v = 0;
    readBytes(&v, 1);
    setDetail.typeDataName.push_back("U8 Type Data");
    return v;
  }
  int8_t readI8() {
    int8_t v = 0;
    readBytes(&v, 1);
    setDetail.typeDataName.push_back("I8 Type Data");
    return v;
  }
  uint16_t readU16() {
    uint16_t v = 0;
    readBytes(&v, 2);
    setDetail.typeDataName.push_back("U16 Type Data");
    return v;
  }
  int16_t readI16() {
    int16_t v = 0;
    readBytes(&v, 2);
    setDetail.typeDataName.push_back("I16 Type Data");
    return v;
  }
  uint32_t readU32() {
    uint32_t v = 0;
    readBytes(&v, 4);
    setDetail.typeDataName.push_back("U32 Type Data");
    return v;
  }
  int32_t readI32() {
    int32_t v = 0;
    readBytes(&v, 4);
    setDetail.typeDataName.push_back("I32 Type Data");
    return v;
  }
  float readF32() {
    float v = 0.0f;
    readBytes(&v, 4);
    setDetail.typeDataName.push_back("F32 Type Data");
    return v;
  }

  glm::vec2 readVec2() { return glm::vec2(readF32(), readF32()); }
  glm::vec3 readVec3() { return glm::vec3(readF32(), readF32(), readF32()); }
  glm::vec4 readVec4() {
    return glm::vec4(readF32(), readF32(), readF32(), readF32());
  }

  // Read a variable-size index. Size is 1, 2, or 4 bytes.
  // For vertex indices, they are unsigned.
  int32_t readIndex(uint8_t size) {
    switch (size) {
    case 1: {
      int8_t v = readI8();
      return (v == -1) ? -1 : (int32_t)(uint8_t)v;
    }
    case 2: {
      int16_t v = readI16();
      return (v == -1) ? -1 : (int32_t)(uint16_t)v;
    }
    case 4:
      return readI32();
    default:
      return -1;
    }
  }

  // For vertex indices specifically (always unsigned).
  uint32_t readVertexIndex() {
    switch (vertIdxSize) {
    case 1:
      return (uint32_t)readU8();
    case 2:
      return (uint32_t)readU16();
    case 4:
      return readU32();
    default:
      return 0;
    }
  }

  int32_t readBoneIndex() { return readIndex(boneIdxSize); }
  int32_t readTexIndex() { return readIndex(texIdxSize); }
  int32_t readMatIndex() { return readIndex(matIdxSize); }
  int32_t readMorphIndex() { return readIndex(morphIdxSize); }
  int32_t readRigidIndex() { return readIndex(rigidIdxSize); }

  // Read a PMX text string: int32 byteLength, then bytes.
  std::string readText() {
    int32_t byteLen = readI32();
    if (byteLen <= 0)
      return "";

    std::vector<char> buf(byteLen);
    readBytes(buf.data(), byteLen);

    if (encoding == 1) {
      // UTF-8: use as-is
      return std::string(buf.data(), byteLen);
    } else {
      // UTF-16LE → convert to UTF-8
      // Simple conversion: handles BMP characters (most Japanese text)
      std::string result;
      result.reserve(byteLen);
      for (int i = 0; i + 1 < byteLen; i += 2) {
        uint16_t codepoint =
            (uint8_t)buf[i] | ((uint16_t)(uint8_t)buf[i + 1] << 8);
        if (codepoint < 0x80) {
          result += (char)codepoint;
        } else if (codepoint < 0x800) {
          result += (char)(0xC0 | (codepoint >> 6));
          result += (char)(0x80 | (codepoint & 0x3F));
        } else {
          result += (char)(0xE0 | (codepoint >> 12));
          result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
          result += (char)(0x80 | (codepoint & 0x3F));
        }
      }
      return result;
    }
  }
};

} // namespace

bool LoadPMX(const std::string &path, PMXModel &out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "[PMX] Failed to open: " << path << std::endl;
    return false;
  }

  Reader r{file};

  // ---- Magic ----
  char magic[4];
  r.readBytes(magic, 4);
  if (std::memcmp(magic, "PMX ", 4) != 0) {
    std::cerr << "[PMX] Invalid magic: expected 'PMX ', got '"
              << std::string(magic, 4) << "'" << std::endl;
    return false;
  }

  // ---- Version ----
  out.version = r.readF32();
  if (out.version < 2.0f || out.version > 2.1f + 0.001f) {
    std::cerr << "[PMX] Unsupported version: " << out.version << std::endl;
    return false;
  }

  // ---- Globals ----
  uint8_t globalsCount = r.readU8();
  if (globalsCount < 8) {
    std::cerr << "[PMX] Invalid globals count: " << (int)globalsCount
              << std::endl;
    return false;
  }
  uint8_t globals[8];
  r.readBytes(globals, 8);
  // Skip any extra globals beyond 8
  for (int i = 8; i < globalsCount; ++i)
    r.readU8();

  r.encoding = globals[0];
  uint8_t additionalVec4Count = globals[1];
  r.vertIdxSize = globals[2];
  r.texIdxSize = globals[3];
  r.matIdxSize = globals[4];
  r.boneIdxSize = globals[5];
  r.morphIdxSize = globals[6];
  r.rigidIdxSize = globals[7];

  std::cout << "[PMX] v" << out.version
            << " | enc=" << (r.encoding ? "UTF-8" : "UTF-16LE")
            << " | vertIdx=" << (int)r.vertIdxSize
            << " | boneIdx=" << (int)r.boneIdxSize << std::endl;

  // ---- Model Info ----
  out.nameJP = r.readText();
  out.nameEN = r.readText();
  out.commentJP = r.readText();
  out.commentEN = r.readText();
  std::cout << "[PMX] Model: " << (out.nameEN.empty() ? out.nameJP : out.nameEN)
            << std::endl;

  // ---- Vertices ----
  int32_t vertCount = r.readI32();
  if (vertCount < 0) {
    std::cerr << "[PMX] Invalid vertex count" << std::endl;
    return false;
  }
  out.vertices.resize(vertCount);
  for (int32_t i = 0; i < vertCount; ++i) {
    PMXVertex &v = out.vertices[i];
    v.position = r.readVec3();
    v.normal = r.readVec3();
    v.uv = r.readVec2();

    // Skip additional vec4 UVs
    for (int j = 0; j < additionalVec4Count; ++j)
      r.readVec4();

    uint8_t deform = r.readU8();
    v.deformType = (DeformType)deform;

    switch (v.deformType) {
    case DeformType::BDEF1:
      v.boneIndices[0] = r.readBoneIndex();
      v.boneWeights[0] = 1.0f;
      break;

    case DeformType::BDEF2:
      v.boneIndices[0] = r.readBoneIndex();
      v.boneIndices[1] = r.readBoneIndex();
      v.boneWeights[0] = r.readF32();
      v.boneWeights[1] = 1.0f - v.boneWeights[0];
      break;

    case DeformType::BDEF4:
    case DeformType::QDEF:
      for (int j = 0; j < 4; ++j)
        v.boneIndices[j] = r.readBoneIndex();
      for (int j = 0; j < 4; ++j)
        v.boneWeights[j] = r.readF32();
      break;

    case DeformType::SDEF:
      v.boneIndices[0] = r.readBoneIndex();
      v.boneIndices[1] = r.readBoneIndex();
      v.boneWeights[0] = r.readF32();
      v.boneWeights[1] = 1.0f - v.boneWeights[0];
      v.sdefC = r.readVec3();
      v.sdefR0 = r.readVec3();
      v.sdefR1 = r.readVec3();
      break;

    default:
      // Unknown deform type — treat as BDEF1
      v.boneIndices[0] = r.readBoneIndex();
      v.boneWeights[0] = 1.0f;
      break;
    }

    v.edgeScale = r.readF32();

    if (!r.ok()) {
      std::cerr << "[PMX] Read error at vertex " << i << std::endl;
      return false;
    }
  }

  std::cout << "[PMX] Vertices: " << vertCount << std::endl;

  // ---- Face indices ----
  int32_t indexCount = r.readI32();
  if (indexCount < 0 || indexCount % 3 != 0) {
    std::cerr << "[PMX] Invalid index count: " << indexCount << std::endl;
    return false;
  }
  out.indices.resize(indexCount);
  for (int32_t i = 0; i < indexCount; ++i) {
    out.indices[i] = r.readVertexIndex();
  }
  std::cout << "[PMX] Triangles: " << (indexCount / 3) << std::endl;

  // ---- Texture paths ----
  int32_t texCount = r.readI32();
  if (texCount < 0) {
    std::cerr << "[PMX] Invalid texture count" << std::endl;
    return false;
  }
  out.texturePaths.resize(texCount);
  for (int32_t i = 0; i < texCount; ++i) {
    out.texturePaths[i] = r.readText();
  }
  std::cout << "[PMX] Textures: " << texCount << std::endl;

  // ---- Materials ----
  int32_t matCount = r.readI32();
  if (matCount < 0) {
    std::cerr << "[PMX] Invalid material count" << std::endl;
    return false;
  }
  out.materials.resize(matCount);
  for (int32_t i = 0; i < matCount; ++i) {
    PMXMaterial &m = out.materials[i];
    m.nameJP = r.readText();
    m.nameEN = r.readText();
    m.diffuse = r.readVec4();
    m.specular = r.readVec3();
    m.specularStrength = r.readF32();
    m.ambient = r.readVec3();
    m.drawFlags = r.readU8();
    m.edgeColor = r.readVec4();
    m.edgeSize = r.readF32();
    m.textureIndex = r.readTexIndex();
    m.sphereTextureIndex = r.readTexIndex();
    m.sphereMode = r.readU8();
    m.toonFlag = r.readU8();
    if (m.toonFlag == 0) {
      m.toonTextureIndex = r.readTexIndex();
    } else {
      m.toonTextureIndex = (int32_t)r.readU8();
    }
    m.memo = r.readText();
    m.indexCount = r.readI32();

    if (!r.ok()) {
      std::cerr << "[PMX] Read error at material " << i << std::endl;
      return false;
    }
  }
  std::cout << "[PMX] Materials: " << matCount << std::endl;

  // ---- Bones ----
  int32_t boneCount = r.readI32();
  if (boneCount < 0) {
    std::cerr << "[PMX] Invalid bone count" << std::endl;
    return false;
  }
  out.bones.resize(boneCount);
  for (int32_t i = 0; i < boneCount; ++i) {
    PMXBone &b = out.bones[i];
    b.nameJP = r.readText();
    b.nameEN = r.readText();
    b.position = r.readVec3();
    b.parentIndex = r.readBoneIndex();
    b.transformLayer = r.readI32();
    b.flags = r.readU16();

    // Bit 0: tail position mode
    if (b.flags & 0x0001) {
      b.tailBoneIndex = r.readBoneIndex();
    } else {
      b.tailOffset = r.readVec3();
    }

    // Bit 8 or 9: inherit rotation / translation
    if (b.flags & 0x0100 || b.flags & 0x0200) {
      b.inheritParentIndex = r.readBoneIndex();
      b.inheritWeight = r.readF32();
    }

    // Bit 10: fixed axis
    if (b.flags & 0x0400) {
      b.fixedAxis = r.readVec3();
    }

    // Bit 11: local axis
    if (b.flags & 0x0800) {
      b.localAxisX = r.readVec3();
      b.localAxisZ = r.readVec3();
    }

    // Bit 12: external parent
    if (b.flags & 0x1000) {
      b.externalParentKey = r.readI32();
    }

    // Bit 5: IK
    if (b.flags & 0x0020) {
      b.ikTargetBoneIndex = r.readBoneIndex();
      b.ikLoopCount = r.readI32();
      b.ikLimitAngle = r.readF32();
      int32_t linkCount = r.readI32();
      b.ikLinks.resize(linkCount);
      for (int32_t j = 0; j < linkCount; ++j) {
        PMXBone::IKLink &link = b.ikLinks[j];
        link.boneIndex = r.readBoneIndex();
        uint8_t hasLimits = r.readU8();
        link.hasLimits = (hasLimits != 0);
        if (link.hasLimits) {
          link.lowerLimit = r.readVec3();
          link.upperLimit = r.readVec3();
        }
      }
    }

    if (!r.ok()) {
      std::cerr << "[PMX] Read error at bone " << i << std::endl;
      return false;
    }
  }
  std::cout << "[PMX] Bones: " << boneCount << std::endl;
  r.ReadAllDetail();
  // We stop here — morphs, display frames, rigid bodies, and joints
  // are not parsed yet. The geometry + material + bone data is
  // sufficient for rendering in the bind pose.

  return true;
}

} // namespace pmx
