#pragma once
// Offline preview of a clip on a PMX character: skinned with the clip,
// spring bones simulated at 60 Hz, drawn by the CPU rasterizer. Produces an
// animated GIF (for people) and a contact-sheet PNG (for tools/agents that
// can only look at still images). Vulkan-free; safe on a worker thread.

#include "SkeletalAnimation.hpp"
#include "SpringBone.hpp"
#include <string>
#include <vector>

namespace pmx { struct PMXModel; }

namespace ilmeee {

struct AnimationPreviewRequest {
  std::string modelPath; // .pmx
  AnimationClip clip;
  SpringBoneComponent springBones; // simulated when enabled && physics
  bool physics = true;
  float start = 0;
  float duration = -1;  // <= 0: whole clip (capped at 12 s)
  float fps = 12;       // GIF frame rate, 4..30
  int size = 256;       // GIF frame side in pixels, 64..512
  float yaw = 0;        // 0 = front, 90 = character's left side
  float pitch = 8;
  int sheetFrames = 8;  // frames on the contact sheet (1..16)
  int sheetColumns = 4;
  std::string gifPath;  // "" = no GIF file
};

struct AnimationPreviewResult {
  std::vector<unsigned char> sheetPng;
  int sheetWidth = 0, sheetHeight = 0, columns = 0, rows = 0;
  std::vector<float> sheetTimes; // clip time of each sheet cell, row-major
  int frames = 0;
  int matchedTracks = 0, totalTracks = 0;
  std::vector<std::string> unmatchedBones; // clip tracks the rig lacks
  std::string gifPath;
  size_t gifBytes = 0;
  double seconds = 0; // render time
};

// Same skeleton the renderer builds: Japanese name (or English), English as
// alt name, Z mirrored.
std::vector<SkeletonBone> SkeletonFromPmx(const pmx::PMXModel &model);

bool RenderAnimationPreview(const AnimationPreviewRequest &request,
                            AnimationPreviewResult &result, std::string &error);

} // namespace ilmeee
