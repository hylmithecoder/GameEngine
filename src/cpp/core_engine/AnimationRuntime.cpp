#include "core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>

bool SceneRenderer::SetMesh3DAnimationPose(size_t index,
                                         const ilmeee::AnimationClip &clip,
                                         float time, float physicsDelta) {
  if (index >= meshes3d.size() || device == VK_NULL_HANDLE) return false;
  auto &mesh = meshes3d[index];
  if (mesh.skinVertices.empty() || mesh.vertexMemory == VK_NULL_HANDLE) return false;
  std::vector<glm::mat4> world, matrices;
  std::vector<glm::vec3> positions;
  if (!ilmeee::EvaluateSkeletonPose(mesh.skeleton, clip, time, world)) {
    mesh.animationError = "Invalid bone hierarchy or pose";
    return false;
  }
  const bool physics = physicsDelta >= 0 &&
      ilmeee::ApplySpringBones(mesh.skeleton, mesh.springBones, mesh.springRuntime,
                               GetMesh3DWorldModel(index), physicsDelta, world);
  ilmeee::BuildSkinMatrices(mesh.skeleton, world, matrices, positions);
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
  mesh.poseActive = !clip.tracks.empty() || physics;
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
    mesh.springRuntime.Reset();
    mesh.loadedAnimationPath.clear();
    mesh.animationError.clear();
    mesh.locomotion = {};
    mesh.locomotion.previousPosition = glm::vec3(GetMesh3DWorldTRS(i)[3]);
    mesh.locomotion.hasPosition = true;
  }
}

void SceneRenderer::UpdateAnimations(float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) return;
  for (size_t i = 0; i < meshes3d.size(); ++i) {
    auto &mesh = meshes3d[i];
    if (mesh.skeleton.empty()) continue;
    if (!mesh.animation.enabled || (mesh.animation.clip.empty() && mesh.animation.controller.empty())) {
      mesh.locomotion = {};
      mesh.loadedAnimationPath.clear(); mesh.animationError.clear();
      // Spring bones still swing on the rest pose (a static character that
      // walks or turns).
      if (mesh.springBones.enabled) SetMesh3DAnimationPose(i, {}, 0, deltaSeconds);
      else ResetMesh3DAnimationPose(i);
      continue;
    }
    if (!mesh.animation.controller.empty()) {
      auto &runtime = mesh.locomotion;
      const glm::vec3 position(GetMesh3DWorldTRS(i)[3]);
      if (runtime.loadedPath != mesh.animation.controller) {
        runtime = {};
        runtime.loadedPath = mesh.animation.controller;
        mesh.loadedAnimationPath.clear();
        mesh.animationError.clear();
        std::filesystem::path path(runtime.loadedPath);
        if (path.is_relative()) path = std::filesystem::path(animationProjectRoot) / path;
        if (!ilmeee::LoadAnimationController(path.string(), runtime.config, mesh.animationError) ||
            !ilmeee::LoadControllerClips(path.string(), runtime.config, runtime.clips, mesh.animationError)) {
          ResetMesh3DAnimationPose(i); continue;
        }
        // Report rigs that cannot use even the Idle clip, rather than a static rest pose.
        bool matched = false;
        for (const auto &track : runtime.clips[0].tracks)
          for (const auto &bone : mesh.skeleton) if (bone.name == track.bone) matched = true;
        if (!matched && !runtime.clips[0].tracks.empty()) { mesh.animationError = "Idle clip has no matching PMX bone tracks"; ResetMesh3DAnimationPose(i); continue; }
      }
      if (!mesh.animationError.empty()) continue;
      if (runtime.hasPosition && deltaSeconds > 0) {
        const glm::vec2 displacement(position.x - runtime.previousPosition.x, position.z - runtime.previousPosition.z);
        const float distance = glm::length(displacement);
        if (std::isfinite(distance) && distance < std::max(10.0f, runtime.config.runSpeed * deltaSeconds * 8)) {
          runtime.sampleDistance += distance; runtime.sampleTime += deltaSeconds;
          // Average fixed movement steps over a short window to avoid idle flicker at high render FPS.
          if (runtime.sampleTime >= 0.05f) {
            runtime.measuredSpeed = runtime.sampleDistance / runtime.sampleTime;
            runtime.sampleDistance = runtime.sampleTime = 0;
          }
        } else { runtime.sampleDistance = runtime.sampleTime = runtime.measuredSpeed = 0; }
      }
      runtime.previousPosition = position; runtime.hasPosition = true;
      const auto pose = runtime.player.Update(mesh.skeleton, runtime.config, runtime.clips,
          runtime.measuredSpeed, deltaSeconds, mesh.animation.speed);
      SetMesh3DAnimationPose(i, pose, 0, deltaSeconds);
      continue;
    }
    mesh.locomotion = {};
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
    SetMesh3DAnimationPose(i, mesh.animationClip, mesh.animationTime, deltaSeconds);
  }
}
