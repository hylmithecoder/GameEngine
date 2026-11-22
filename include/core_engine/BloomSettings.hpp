#pragma once
// Scene-wide bloom (glow around bright pixels). Saved with the scene
// (.ilmeeescene v1.21) and applied by SceneRenderer to the Game view, the
// editor's Game tab and the built player; optionally to the Scene view.

#include <cstdint>
#include <string_view>
#include <glm/glm.hpp>

namespace ilmeee {

struct BloomSettings {
  bool enabled = false;
  bool sceneView = false;  // also apply to the editor's Scene view
  float threshold = 0.8f;  // brightness (0..1 per channel) that starts to glow
  float softKnee = 0.5f;   // 0 = hard cut at threshold .. 1 = gradual
  float intensity = 0.8f;  // how much glow is added back (0..10)
  float radius = 0.85f;    // spread of each blur step, 0.1 tight .. 1 wide
  glm::vec3 tint{1.0f};    // colour of the glow
  int iterations = 6;      // blur steps (mip levels), 1..8: more = wider

  static constexpr int kMaxIterations = 8;
  enum Flags : uint32_t { Enabled = 1, SceneView = 2 };
  uint32_t PackFlags() const {
    return (enabled ? Enabled : 0) | (sceneView ? SceneView : 0);
  }
  void UnpackFlags(uint32_t flags) {
    enabled = flags & Enabled;
    sceneView = flags & SceneView;
  }
  // Keep values from files, MCP or the UI inside what the shaders expect.
  void Clamp() {
    if (!(threshold == threshold)) threshold = 0.8f; // NaN
    if (!(softKnee == softKnee)) softKnee = 0.5f;
    if (!(intensity == intensity)) intensity = 0.8f;
    if (!(radius == radius)) radius = 0.85f;
    threshold = glm::clamp(threshold, 0.0f, 4.0f);
    softKnee = glm::clamp(softKnee, 0.0f, 1.0f);
    intensity = glm::clamp(intensity, 0.0f, 10.0f);
    radius = glm::clamp(radius, 0.1f, 1.0f);
    tint = glm::clamp(tint, glm::vec3(0.0f), glm::vec3(4.0f));
    if (!(tint == tint)) tint = glm::vec3(1.0f);
    iterations = glm::clamp(iterations, 1, kMaxIterations);
  }
};

// Named looks shared by the Lighting window and MCP.
struct BloomPreset {
  const char *id;      // MCP name
  const char *label;   // UI name
  const char *tooltip;
  float threshold, softKnee, intensity, radius;
  int iterations;
};
inline constexpr BloomPreset kBloomPresets[] = {
    {"subtle", "Subtle", "Only the brightest spots glow a little", 0.9f, 0.4f,
     0.5f, 0.7f, 5},
    {"neon_night", "Neon Night", "Strong, wide glow for lamps and neon signs",
     0.65f, 0.6f, 1.6f, 0.9f, 7},
    {"dreamy", "Dreamy", "Soft haze over everything bright (VN flashback)",
     0.45f, 1.0f, 1.0f, 1.0f, 8},
};
// Enables bloom with the preset's values (tint and sceneView are kept).
inline void ApplyBloomPreset(BloomSettings &s, const BloomPreset &p) {
  s.enabled = true;
  s.threshold = p.threshold;
  s.softKnee = p.softKnee;
  s.intensity = p.intensity;
  s.radius = p.radius;
  s.iterations = p.iterations;
}
inline bool ApplyBloomPreset(BloomSettings &s, std::string_view id) {
  for (const auto &p : kBloomPresets)
    if (id == p.id) {
      ApplyBloomPreset(s, p);
      return true;
    }
  return false;
}

} // namespace ilmeee
