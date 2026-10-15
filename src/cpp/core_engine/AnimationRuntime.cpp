#include "core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>

bool SceneRenderer::SetMesh3DAnimationPose(size_t index,
                                         const ilmeee::AnimationClip &clip,
                                         float time) {
  if (index >= meshes3d.size() || device == VK_NULL_HANDLE) return false;
  auto &mesh = meshes3d[index];
  if (mesh.skinVertices.empty() || mesh.vertexMemory == VK_NULL_HANDLE) return false;
  std::vector<glm::mat4> matrices;
  std::vector<glm::vec3> positions;
  if (!ilmeee::EvaluateSkeleton(mesh.skeleton, clip, time, matrices, positions)) {
    mesh.animationError = "Invalid bone hierarchy or pose";
    return false;
  }
  std::vector<MeshVertex> vertices(mesh.skinVertices.size());
  glm::vec3 minimum(std::numeric_limits<float>::max());
  glm::vec3 maximum(-std::numeric_limits<float>::max());
  for (size_t i = 0; i < vertices.size(); ++i) {
    const auto &v = mesh.skinVertices[i];
    const glm::ivec4 joints(v.boneIndices[0], v.boneIndices[1],
                            v.boneIndices[2], v.boneIndices[3]);
    const glm::vec4 weights(v.boneWeights[0], v.boneWeights[1],
                            v.boneWeights[2], v.boneWeights[3]);
    auto &out = vertices[i];
    out.pos = ilmeee::SkinPosition({v.position.x, v.position.y, -v.position.z},
                                   joints, weights, matrices);
    out.normal = ilmeee::SkinNormal({v.normal.x, v.normal.y, -v.normal.z},
                                    joints, weights, matrices);
    out.uv = v.uv;
    minimum = glm::min(minimum, out.pos);
    maximum = glm::max(maximum, out.pos);
  }
  // These buffers are host-visible/coherent (UploadMeshBuffers). Wait for
  // the previous scene pass before writing; this also covers async players.
  WaitOffscreenWork();
  void *mapped = nullptr;
  const VkDeviceSize bytes = vertices.size() * sizeof(MeshVertex);
  if (vkMapMemory(device, mesh.vertexMemory, 0, bytes, 0, &mapped) != VK_SUCCESS)
    return false;
  std::memcpy(mapped, vertices.data(), static_cast<size_t>(bytes));
  vkUnmapMemory(device, mesh.vertexMemory);
  mesh.cpuPositions.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) mesh.cpuPositions[i] = vertices[i].pos;
  mesh.aabbMin = minimum;
  mesh.aabbMax = maximum;
  mesh.posedBones = std::move(positions);
  mesh.poseActive = !clip.tracks.empty();
  return true;
}

void SceneRenderer::ResetMesh3DAnimationPose(size_t index) {
  if (index < meshes3d.size() && meshes3d[index].poseActive)
    SetMesh3DAnimationPose(index, ilmeee::AnimationClip{}, 0);
}

void SceneRenderer::ResetAnimations() {
  for (size_t i = 0; i < meshes3d.size(); ++i) {
    auto &mesh = meshes3d[i];
    ResetMesh3DAnimationPose(i);
    mesh.animationTime = 0;
    mesh.loadedAnimationPath.clear();
    mesh.animationError.clear();
  }
}

void SceneRenderer::UpdateAnimations(float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) return;
  for (size_t i = 0; i < meshes3d.size(); ++i) {
    auto &mesh = meshes3d[i];
    if (mesh.skeleton.empty()) continue;
    if (!mesh.animation.enabled || mesh.animation.clip.empty()) {
      ResetMesh3DAnimationPose(i);
      continue;
    }
    if (mesh.loadedAnimationPath != mesh.animation.clip) {
      mesh.loadedAnimationPath = mesh.animation.clip;
      mesh.animationTime = 0;
      mesh.animationError.clear();
      std::filesystem::path path(mesh.animation.clip);
      if (path.is_relative()) path = std::filesystem::path(animationProjectRoot) / path;
      if (!ilmeee::LoadAnimationClip(path.string(), mesh.animationClip,
                                    mesh.animationError)) {
        ResetMesh3DAnimationPose(i);
        continue;
      }
    }
    if (!mesh.animationError.empty()) continue;
    const float speed = std::isfinite(mesh.animation.speed)
        ? std::clamp(mesh.animation.speed, 0.0f, 100.0f) : 1.0f;
    mesh.animationTime = ilmeee::AnimationTime(mesh.animationTime + deltaSeconds * speed,
                                              mesh.animationClip.duration, mesh.animation.loop);
    SetMesh3DAnimationPose(i, mesh.animationClip, mesh.animationTime);
  }
}
