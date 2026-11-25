#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace ilmeee {
// A world-space XY text surface, facing local +Z. Size is line height in
// world units; the normal GameObject transform and parent still apply.
struct WorldTextComponent {
  std::string text = "Text";
  float size = 0.5f;
  glm::vec4 color{1.0f};
  int alignment = 1; // 0 left, 1 center, 2 right, relative to the origin
  bool visible = true;
};
struct WorldTextVertex { glm::vec3 position; glm::vec2 uv; };
struct WorldTextGeometry {
  std::vector<WorldTextVertex> vertices;
  std::vector<uint32_t> indices;
  glm::vec3 min{0}, max{0};
};
struct WorldTextFontImage {
  int width = 0, height = 0;
  const unsigned char *rgba = nullptr;
};
// Private 64px font atlas, independent of ImGui's UI font/texture lifecycle.
// Uses the bundled MiSans font, with an embedded fallback when unavailable.
WorldTextFontImage GetWorldTextFontImage();
WorldTextGeometry BuildWorldTextGeometry(const WorldTextComponent &text);
} // namespace ilmeee
