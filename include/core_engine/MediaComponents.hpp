#pragma once
// Audio Source and Video Player components. Like the movement component,
// any scene object (Mesh3D) can carry them; the "Audio Source" and
// "Video Screen" objects in the Add menu are just a marker / screen mesh
// with the component switched on. Plain data — playback lives in
// MediaSystem so the renderer, scene file and undo history stay free of
// SDL/FFmpeg.
//
// Clip paths are relative to the project (game) root when the file lives
// inside it, absolute otherwise (a built game only ships project files).

#include <string>

namespace ilmeee {

struct AudioSourceComponent {
  bool enabled = false;
  std::string clip;
  float volume = 1.0f;
  bool loop = false;
  bool playOnStart = true;
  // 3D: volume falls off linearly from minDistance (full) to maxDistance
  // (silent) around the game camera, and pans left/right. 2D: plain stereo.
  bool spatial = true;
  float minDistance = 1.0f;
  float maxDistance = 20.0f;
};

struct VideoPlayerComponent {
  bool enabled = false;
  std::string clip;
  bool loop = true;
  bool playOnStart = true;
  float volume = 1.0f; // the video's own sound track, played in 2D
  // Unlit: frames show at full brightness like a real screen. Lit: they are
  // shaded like any other texture.
  bool unlit = true;
  int surface = -1; // surface (submesh) showing the video, -1 = all
};

// Lower-case extension checks shared by the editor (drag & drop, file
// pickers) and the runtime.
bool IsAudioClipPath(const std::string &path);
bool IsVideoClipPath(const std::string &path);

} // namespace ilmeee
