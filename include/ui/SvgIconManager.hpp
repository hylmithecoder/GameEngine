#pragma once
#include "../vulkan/vulkanhandler.hpp"
#include <filesystem>
#include <imgui.h>
#include <string>
#include <unordered_map>

// Unicode Private Use Area (U+E100..U+E11F) UTF-8 icon glyphs baked into ImFont
#define ICON_UI_HIERARCHY "\xEE\x84\x80"
#define ICON_UI_SCENE     "\xEE\x84\x81"
#define ICON_UI_GAME      "\xEE\x84\x82"
#define ICON_UI_INSPECTOR "\xEE\x84\x83"
#define ICON_UI_EXPLORER  "\xEE\x84\x84"
#define ICON_UI_CONSOLE   "\xEE\x84\x85"
#define ICON_UI_ANIMATION "\xEE\x84\x86"
#define ICON_UI_LIGHTING  "\xEE\x84\x87"
#define ICON_UI_DEVICE    "\xEE\x84\x88"
#define ICON_UI_VIDEO     "\xEE\x84\x89"
#define ICON_UI_BUILD     "\xEE\x84\x8A"
#define ICON_UI_SELECT    "\xEE\x84\x8B"
#define ICON_UI_MOVE      "\xEE\x84\x8C"
#define ICON_UI_ROTATE    "\xEE\x84\x8D"
#define ICON_UI_SCALE     "\xEE\x84\x8E"
#define ICON_UI_GRID      "\xEE\x84\x8F"
#define ICON_UI_SNAP      "\xEE\x84\x90"
#define ICON_UI_REFRESH   "\xEE\x84\x91"
#define ICON_UI_EYE       "\xEE\x84\x92"
#define ICON_UI_BACK      "\xEE\x84\x93"
#define ICON_UI_FILTER    "\xEE\x84\x94"
#define ICON_UI_SEARCH    "\xEE\x84\x95"
#define ICON_UI_SAVE      "\xEE\x84\x96"
#define ICON_UI_TEXT3D    "\xEE\x84\x97"
#define ICON_UI_CANVAS    "\xEE\x84\x98"
#define ICON_UI_WARN      "\xEE\x84\x99"
#define ICON_UI_ERROR     "\xEE\x84\x9A"
#define ICON_UI_INFO      "\xEE\x84\x9B"
#define ICON_UI_PLUS      "\xEE\x84\x9C"
#define ICON_UI_TRASH     "\xEE\x84\x9D"
#define ICON_UI_BOX       "\xEE\x84\x9E"
#define ICON_UI_CAMERA    "\xEE\x84\x9F"

struct AssetIconInfo {
  std::string svgPath;
  ImVec4 accentColor = ImVec4(0.39f, 0.45f, 0.55f, 1.0f);
  std::string badgeLabel;
  bool isImagePreviewable = false;
  bool isModelPreviewable = false;
  bool isVideoPreviewable = false;
};

// Centralized SVG icon loader, font-glyph baker, and asset extension resolver.
// Parses SVG files with nanosvg, rasterizes to RGBA pixels at the requested
// size, and uploads to Vulkan with proper linear subresource rowPitch handling.
class SvgIconManager {
public:
  SvgIconManager() = default;
  ~SvgIconManager() = default;

  // Must be called once after Vulkan is initialised.
  void Init(VulkanHandler *vkHandler);

  // Bake crisp monochrome UI SVG icons into the given ImFont (PUA U+E100..U+E11F)
  // so docked ImGui window tabs, menus, and buttons can render inline icons.
  // Call after AddFontFromFileTTF/AddFontDefault and before first NewFrame().
  static void BakeFontIcons(ImFontAtlas *atlas, ImFont *font,
                            float fontSizePx = 15.0f);

  // Resolve extension-aware SVG path, accent color, and badge text for any
  // file or folder (case-insensitive).
  static AssetIconInfo ResolveAssetIcon(const std::filesystem::path &path,
                                        bool isDirectory,
                                        bool isOpen = false);

  // Upload raw RGBA8888 pixel buffer (width x height) to an ImTextureID.
  ImTextureID UploadRgbaPixels(const unsigned char *pixels, int width,
                               int height);

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

  // Resolve relative SVG asset path across working directories.
  static std::string ResolveSvgFilePath(const std::string &svgPath);

  // Rasterize SVG file to RGBA pixel buffer. Caller owns the returned
  // pointer (free with free()). Returns nullptr on failure.
  static unsigned char *RasterizeSvg(const std::string &path, int size);

private:
  VulkanHandler *vulkan_ = nullptr;

  // Cache key: "path:size"
  std::unordered_map<std::string, ImTextureID> cache_;
};
