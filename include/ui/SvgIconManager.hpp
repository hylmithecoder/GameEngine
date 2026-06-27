#pragma once
#include "../vulkan/vulkanhandler.hpp"
#include <imgui.h>
#include <string>
#include <unordered_map>

// Centralized SVG icon loader and cache.
// Parses SVG files with nanosvg, rasterizes to RGBA pixels at the
// requested size, and uploads to Vulkan via VulkanHandler::LoadImage
// (raw-pixel overload). Results are cached by (path, size) key.
class SvgIconManager {
public:
  SvgIconManager() = default;
  ~SvgIconManager() = default;

  // Must be called once after Vulkan is initialised.
  void Init(VulkanHandler *vkHandler);

  // Returns an ImTextureID for the given SVG file, rasterized at
  // size×size pixels. Returns 0 (null) on failure.
  ImTextureID GetIcon(const std::string &svgPath, int size = 20);

  // Convenience: render an SVG icon inline (Image + SameLine).
  void DrawIcon(const std::string &svgPath, int size = 20);

  // Convenience: render an SVG icon as a clickable button.
  // Returns true if clicked.
  bool DrawIconButton(const char *id, const std::string &svgPath, int size,
                      const ImVec4 &bgColor = ImVec4(0, 0, 0, 0),
                      const ImVec4 &tintColor = ImVec4(1, 1, 1, 1));

private:
  VulkanHandler *vulkan_ = nullptr;

  // Cache key: "path:size"
  std::unordered_map<std::string, ImTextureID> cache_;

  // Rasterize SVG file to RGBA pixel buffer. Caller owns the returned
  // pointer (free with free()). Returns nullptr on failure.
  unsigned char *RasterizeSvg(const std::string &path, int size);
};
