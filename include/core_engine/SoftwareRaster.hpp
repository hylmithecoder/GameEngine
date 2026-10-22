#pragma once
// Tiny CPU rasterizer shared by the Explorer model thumbnails and the
// animation previews: orthographic z-buffer, two-sided Lambert with flat face
// normals, diffuse texture with alpha cut-out, supersampled. Needs no Vulkan
// context, so it can run on any thread.

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace ilmeee::raster {

// Model files authored on Windows reference textures with backslashes and
// arbitrary letter case; resolves them component by component on disk.
std::string ResolveFile(const std::filesystem::path &dir, std::string rel);

struct Texture {
  int width = 0, height = 0;
  std::vector<unsigned char> rgba;
};
// RGBA8, shrunk (power-of-two steps) to at most maxSize per side.
bool LoadTexture(const std::string &path, Texture &out, int maxSize = 512);

// A material range of triangle corners (3 per triangle).
struct Part {
  uint32_t first = 0, count = 0;
  glm::vec4 color{0.8f, 0.8f, 0.8f, 1.0f};
  std::string texture; // resolved path, "" = untextured
};

// Orthographic camera looking at the model from `toCamera`.
struct View {
  glm::vec3 right{1, 0, 0}, up{0, 1, 0}, toCamera{0, 0, 1}, light{0, 0, 1};
};
// yaw 0 = from the front (+Z), positive yaw orbits towards +X.
View MakeView(float yawDegrees, float pitchDegrees);

class Canvas {
public:
  Canvas(int size, int supersample);
  int Size() const { return size_; }
  int Samples() const { return size_ * supersample_; }
  void Clear();
  // `world` holds triangle corners; `toPixel` maps a view-space point
  // (dot with right, up, toCamera) to supersampled pixels.
  void Draw(const std::vector<glm::vec3> &world, const std::vector<glm::vec2> &uvs,
            const std::vector<Part> &parts, const View &view, glm::vec2 viewCenter,
            float pixelsPerUnit,
            std::unordered_map<std::string, Texture> &textureCache);
  // Box-filters to Size(); `background.a == 0` keeps transparency.
  std::vector<unsigned char> Resolve(glm::vec4 background) const;

private:
  int size_, supersample_;
  std::vector<float> depth_;
  std::vector<glm::vec4> color_;
};

bool WritePng(const std::string &path, const std::vector<unsigned char> &rgba,
              int width, int height);
std::vector<unsigned char> EncodePng(const std::vector<unsigned char> &rgba,
                                     int width, int height);

} // namespace ilmeee::raster
