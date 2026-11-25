#pragma once
// Scene-wide non-photorealistic (toon / cel) lighting. Saved with the scene
// (.ilmeeescene v1.15) and applied by SceneRenderer in the editor and player.

#include <cstdint>
#include <glm/glm.hpp>

namespace ilmeee {

struct NprSettings {
  bool enabled = false;
  int bands = 2;          // light steps, 2 = classic cel shading (2..5)
  float softness = 0.05f; // band edge blur, 0 = hard .. 0.5 = smooth
  glm::vec3 shadowColor{0.62f, 0.58f, 0.72f}; // multiplies albedo in shade

  bool rim = true; // bright edge on the silhouette
  glm::vec3 rimColor{1.0f};
  float rimWidth = 0.25f; // 0..1
  float rimStrength = 0.5f;

  bool specular = true; // hard-edged highlight spot
  float specularSize = 0.06f; // 0..0.5
  float specularStrength = 0.5f;

  bool outline = true; // inverted-hull ink lines
  glm::vec3 outlineColor{0.08f, 0.07f, 0.10f};
  float outlineWidth = 1.5f; // about pixels at 1080p

  enum Flags : uint32_t { Enabled = 1, Rim = 2, Specular = 4, Outline = 8 };
  uint32_t PackFlags() const {
    return (enabled ? Enabled : 0) | (rim ? Rim : 0) |
           (specular ? Specular : 0) | (outline ? Outline : 0);
  }
  void UnpackFlags(uint32_t flags) {
    enabled = flags & Enabled;
    rim = flags & Rim;
    specular = flags & Specular;
    outline = flags & Outline;
  }
  // Keep values from files or scripts inside what the shaders expect.
  void Clamp() {
    bands = glm::clamp(bands, 2, 5);
    softness = glm::clamp(softness, 0.0f, 0.5f);
    rimWidth = glm::clamp(rimWidth, 0.0f, 1.0f);
    rimStrength = glm::clamp(rimStrength, 0.0f, 2.0f);
    specularSize = glm::clamp(specularSize, 0.0f, 0.5f);
    specularStrength = glm::clamp(specularStrength, 0.0f, 2.0f);
    outlineWidth = glm::clamp(outlineWidth, 0.0f, 10.0f);
    shadowColor = glm::clamp(shadowColor, glm::vec3(0.0f), glm::vec3(1.0f));
    rimColor = glm::clamp(rimColor, glm::vec3(0.0f), glm::vec3(1.0f));
    outlineColor = glm::clamp(outlineColor, glm::vec3(0.0f), glm::vec3(1.0f));
  }
};

} // namespace ilmeee
